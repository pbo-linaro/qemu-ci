==========================
QAPI Golang code generator
==========================

..
   Copyright (C) 2025 Red Hat, Inc.

   This work is licensed under the terms of the GNU GPL, version 2 or
   later.  See the COPYING file in the top-level directory.


Introduction
============

This document provides information of how the generated Go code maps
with the QAPI specification, clarifying design decisions when needed.


Scope of the generated Go code
==============================

The scope is to provide data structures that can interpret and be used
to generate valid QMP messages. These data structures are generated
from a QAPI schema and should be able to handle QMP messages from the
same schema.

We also provide interfaces for Commands and Events which allows an
abstraction for client and server applications with the possibility of
custom back end implantations.

The generated Go code is a Go module with data structs that uses Go
standard library ``encoding/json``, implementing its field tags and
Marshal interface whenever needed.


QAPI Documentation
==================

The documentation included in QAPI schema such as type and type's
fields information, comments, examples and more, they are converted
and embed in the Go generated source code. Metadata information that
might not be relevant to developers are excluded (e.g: TODOs)


QAPI types to Go structs
========================

Enum
----

Enums are mapped as strings in Go, using a specified string type per
Enum to help with type safety in the Go application.

::

    { 'enum': 'HostMemPolicy',
      'data': [ 'default', 'preferred', 'bind', 'interleave' ] }

.. code-block:: go

    // Host memory policy types
    //
    // Since: 2.1
    type HostMemPolicy string

    const (
        // restore default policy, remove any nondefault policy
        HostMemPolicyDefault HostMemPolicy = "default"
        // set the preferred host nodes for allocation
        HostMemPolicyPreferred HostMemPolicy = "preferred"
        // a strict policy that restricts memory allocation to the host nodes specified
        HostMemPolicyBind HostMemPolicy = "bind"
        // memory allocations are interleaved across the set of host nodes specified
        HostMemPolicyInterleave HostMemPolicy = "interleave"
    )


Struct
------

The mapping between a QAPI struct in Go struct is very straightforward.
 - Each member of the QAPI struct has its own field in a Go struct.
 - Optional members are pointers type with 'omitempty' field tag set

One important design decision was to _not_ embed base struct, copying
the base members to the original struct. This reduces the complexity
for the Go application.

::

    { 'struct': 'BlockExportOptionsNbdBase',
      'data': { '*name': 'str', '*description': 'str' } }

    { 'struct': 'BlockExportOptionsNbd',
      'base': 'BlockExportOptionsNbdBase',
      'data': { '*bitmaps': ['BlockDirtyBitmapOrStr'],
                '*allocation-depth': 'bool' } }

.. code-block:: go

    // An NBD block export (distinct options used in the NBD branch of
    // block-export-add).
    //
    // Since: 5.2
    type BlockExportOptionsNbd struct {
        // Export name. If unspecified, the @device parameter is used as
        // the export name. (Since 2.12)
        Name *string `json:"name,omitempty"`
        // Free-form description of the export, up to 4096 bytes. (Since
        // 5.0)
        Description *string `json:"description,omitempty"`
        // Also export each of the named dirty bitmaps reachable from
        // @device, so the NBD client can use NBD_OPT_SET_META_CONTEXT
        // with the metadata context name "qemu:dirty-bitmap:BITMAP" to
        // inspect each bitmap. Since 7.1 bitmap may be specified by
        // node/name pair.
        Bitmaps []BlockDirtyBitmapOrStr `json:"bitmaps,omitempty"`
        // Also export the allocation depth map for @device, so the NBD
        // client can use NBD_OPT_SET_META_CONTEXT with the metadata
        // context name "qemu:allocation-depth" to inspect allocation
        // details. (since 5.2)
        AllocationDepth *bool `json:"allocation-depth,omitempty"`
    }


Union
-----

Unions in QAPI are bounded to a Enum type which provides all possible
branches of the union. The most important caveat here is that the Union
does not need to have a complex type implemented for all possible
branches of the Enum. Receiving a enum value of a empty branch is valid.

The generated Go struct will then define a field for each
Enum value. The type for Enum values of empty branch is bool. Only one
field can be set at time.

::

    { 'union': 'ImageInfoSpecificQCow2Encryption',
      'base': 'ImageInfoSpecificQCow2EncryptionBase',
      'discriminator': 'format',
      'data': { 'luks': 'QCryptoBlockInfoLUKS' } }

    { 'struct': 'ImageInfoSpecificQCow2EncryptionBase',
      'data': { 'format': 'BlockdevQcow2EncryptionFormat'}}

    { 'enum': 'BlockdevQcow2EncryptionFormat',
      'data': [ 'aes', 'luks' ] }

.. code-block:: go

    type ImageInfoSpecificQCow2Encryption struct {
        // Variants fields
        Luks *QCryptoBlockInfoLUKS `json:"-"`
        // Empty branched enum fields
        Aes bool `json:"-"`
    }

    func (s ImageInfoSpecificQCow2Encryption) MarshalJSON() ([]byte, error) {
        // ...
        // Logic for branched Enum
        if s.Luks != nil && err == nil {
            if len(bytes) != 0 {
                err = errors.New(`multiple variant fields set`)
            } else if err = unwrapToMap(m, s.Luks); err == nil {
                m["format"] = BlockdevQcow2EncryptionFormatLuks
                bytes, err = json.Marshal(m)
            }
        }

        // Logic for unbranched Enum
        if s.Aes && err == nil {
            if len(bytes) != 0 {
                err = errors.New(`multiple variant fields set`)
            } else {
                m["format"] = BlockdevQcow2EncryptionFormatAes
                bytes, err = json.Marshal(m)
            }
        }

        // ...
        // Handle errors
    }


    func (s *ImageInfoSpecificQCow2Encryption) UnmarshalJSON(data []byte) error {
        // ...

        switch tmp.Format {
        case BlockdevQcow2EncryptionFormatLuks:
            s.Luks = new(QCryptoBlockInfoLUKS)
            if err := json.Unmarshal(data, s.Luks); err != nil {
                s.Luks = nil
                return err
            }
        case BlockdevQcow2EncryptionFormatAes:
            s.Aes = true

        default:
            return fmt.Errorf("error: unmarshal: ImageInfoSpecificQCow2Encryption: received unrecognized value: '%s'",
                tmp.Format)
        }
        return nil
    }


Alternate
---------

Like Unions, alternates can have branches. Unlike Unions, they don't
have a discriminator field and each branch should be a different class
of Type entirely (e.g: You can't have two branches of type int in one
Alternate).

While the marshalling is similar to Unions, the unmarshalling uses a
try-and-error approach, trying to fit the data payload in one of the
Alternate fields.

The biggest caveat is handling Alternates that can take JSON Null as
value. The issue lies on ``encoding/json`` library limitation where
unmarshalling JSON Null data to a Go struct which has the 'omitempty'
field as it will bypass the Marshal interface. The same happens when
marshalling, if the field tag 'omitempty' is used, a nil pointer would
never be translated to null JSON value. The problem here is that we do
use pointer to type plus ``omitempty`` field to express a QAPI
optional member.

In order to handle JSON Null, the generator needs to do the following:
  - Read the QAPI schema prior to generate any code and cache
    all alternate types that can take JSON Null
  - For all Go structs that should be considered optional and they type
    are one of those alternates, do not set ``omitempty`` and implement
    Marshal interface for this Go struct, to properly handle JSON Null
  - In the Alternate, uses a boolean 'IsNull' to express a JSON Null
    and implement the AbsentAlternate interface, to help structs know
    if a given Alternate type should be considered Absent (not set) or
    any other possible Value, including JSON Null.

::

    { 'alternate': 'BlockdevRefOrNull',
      'data': { 'definition': 'BlockdevOptions',
                'reference': 'str',
                'null': 'null' } }

.. code-block:: go

    // Reference to a block device.
    //
    // Since: 2.9
    type BlockdevRefOrNull struct {
        // defines a new block device inline
        Definition *BlockdevOptions
        // references the ID of an existing block device. An empty string
        // means that no block device should be referenced. Deprecated;
        // use null instead.
        Reference *string
        // No block device should be referenced (since 2.10)
        IsNull bool
    }

    func (s *BlockdevRefOrNull) ToAnyOrAbsent() (any, bool) {
        if s != nil {
            if s.IsNull {
                return nil, false
            } else if s.Definition != nil {
                return *s.Definition, false
            } else if s.Reference != nil {
                return *s.Reference, false
            }
        }

        return nil, true
    }

    func (s BlockdevRefOrNull) MarshalJSON() ([]byte, error) {
        if s.IsNull {
            return []byte("null"), nil
        } else if s.Definition != nil {
            return json.Marshal(s.Definition)
        } else if s.Reference != nil {
            return json.Marshal(s.Reference)
        }
        return []byte("{}"), nil
    }

    func (s *BlockdevRefOrNull) UnmarshalJSON(data []byte) error {
        // Check for json-null first
        if string(data) == "null" {
            s.IsNull = true
            return nil
        }
        // Check for BlockdevOptions
        {
            s.Definition = new(BlockdevOptions)
            if err := StrictDecode(s.Definition, data); err == nil {
                return nil
            }
            s.Definition = nil
        }

        // Check for string
        {
            s.Reference = new(string)
            if err := StrictDecode(s.Reference, data); err == nil {
                return nil
            }
            s.Reference = nil
        }

        return fmt.Errorf("Can't convert to BlockdevRefOrNull: %s", string(data))
    }


Event
-----

Each event is mapped to its own struct with.

::

    { 'event': 'SHUTDOWN',
      'data': { 'guest': 'bool',
                'reason': 'ShutdownCause' } }

.. code-block:: go

    // Emitted when the virtual machine has shut down, indicating that
    // qemu is about to exit.
    //
    // .. note:: If the command-line option "-no-shutdown" has been
    // specified, qemu will not exit, and a STOP event will eventually
    // follow the SHUTDOWN event.
    //
    // Since: 0.12
    //
    // .. qmp-example::    <- { "event": "SHUTDOWN",      "data": {
    // "guest": true, "reason": "guest-shutdown" },      "timestamp": {
    // "seconds": 1267040730, "microseconds": 682951 } }
    type ShutdownEvent struct {
        // If true, the shutdown was triggered by a guest request (such as
        // a guest-initiated ACPI shutdown request or other hardware-
        // specific action) rather than a host request (such as sending
        // qemu a SIGINT). (since 2.10)
        Guest bool `json:"guest"`
        // The @ShutdownCause which resulted in the SHUTDOWN. (since 4.0)
        Reason ShutdownCause `json:"reason"`
    }


Command
-------

Each commands is mapped to its own struct. If the command has a boxed
data struct, the option struct will be embed in the command struct.

The return value is always a well defined type and it is part of first
layer unmarshalling type, Message.

::

   { 'command': 'set_password',
     'boxed': true,
     'data': 'SetPasswordOptions' }
     
    { 'union': 'SetPasswordOptions',
      'base': { 'protocol': 'DisplayProtocol',
                'password': 'str',
                '*connected': 'SetPasswordAction' },
      'discriminator': 'protocol',
      'data': { 'vnc': 'SetPasswordOptionsVnc' } }

.. code-block:: go

    // Set the password of a remote display server.
    // Errors:   - If Spice is not enabled, DeviceNotFound
    //
    // Since: 0.14
    //
    // .. qmp-example::    -> { "execute": "set_password", "arguments": {
    // "protocol": "vnc",                           "password": "secret" }
    // }   <- { "return": {} }
    type SetPasswordCommand struct {
        SetPasswordOptions
    }

Now an example of a command without boxed type.

::

    { 'command': 'set_link',
      'data': {'name': 'str', 'up': 'bool'} }

.. code-block:: go

    // Sets the link status of a virtual network adapter.
    //
    // Errors:   - If @name is not a valid network device, DeviceNotFound
    //
    // Since: 0.14
    //
    // .. note:: Not all network adapters support setting link status.
    // This command will succeed even if the network adapter does not
    // support link status notification.  .. qmp-example::    -> {
    // "execute": "set_link",      "arguments": { "name": "e1000.0", "up":
    // false } }   <- { "return": {} }
    type SetLinkCommand struct {
        // the device name of the virtual network adapter
        Name string `json:"name"`
        // true to set the link status to be up
        Up bool `json:"up"`
    }

Known issues
============

- Type names might not follow proper Go convention. Andrea suggested an
  annotation to the QAPI schema that could solve it.
  https://lists.gnu.org/archive/html/qemu-devel/2022-05/msg00127.html
