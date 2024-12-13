########################
QAPI Transmogrifier Test
########################

This is a test render of the QEMU QMP reference manual using the new
"transmogrifier" generator in qapidoc.py in conjunction with the
qapi-domain.py sphinx extension.

Some notable features:

 * Every QAPI definition visible below is available to be
   cross-referenced from anywhere else in the Sphinx docs; for example
   ```blockdev-add``` will render to `blockdev-add`.

 * There are type-specific cross-referencing roles available for
   alternates, commands, events, enums, structs, unions and modules. for
   example, ``:qapi:cmd:`block-dirty-bitmap-add``` resolves to
   :qapi:cmd:`block-dirty-bitmap-add`, and only works for commands. The
   roles available are ``cmd``, ``alt``, ``event``, ``enum``,
   ``struct``, ``union``, and ``mod``; with two meta-roles available:
   ``obj`` for absolutely any QAPI definition, and ``type`` for
   everything except commands, events, and modules.

 * There is a new `qapi-index` page which can be linked to with
   ```qapi-index```. There, you can browse a list of all QAPI
   definitions by type or alphabetically.

 * QAPI definitions are also added to the existing `genindex` page.

 * All member/argument/return types are now cross-references to that
   type's definition. `chardev-add` is a good example.

 * This work-in-progress version does not perform any inlining.

 * This work-in-progress version actually also ignores branches entirely
   right now!

 * This version currently does not "prune" unnecessary docs.

 * This version does not add undocumented members or return values.

 * This version does not handle ifcond for anything other than top-level
   entity definitions.

 * This version renders sections in precisely the order they appear in
   source, even if that winds up looking silly.


.. contents::
   :depth: 2

.. qapi-doc:: qapi/qapi-schema.json
   :transmogrify:
