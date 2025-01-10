"""
Golang QAPI generator
"""

# Copyright (c) 2025 Red Hat Inc.
#
# Authors:
#  Victor Toso <victortoso@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

# Just for type hint on self
from __future__ import annotations

import os, textwrap
from typing import List, Optional, Tuple

from .schema import (
    QAPISchema,
    QAPISchemaAlternateType,
    QAPISchemaBranches,
    QAPISchemaEnumMember,
    QAPISchemaFeature,
    QAPISchemaIfCond,
    QAPISchemaObjectType,
    QAPISchemaObjectTypeMember,
    QAPISchemaType,
    QAPISchemaVariants,
    QAPISchemaVisitor,
)
from .source import QAPISourceInfo

FOUR_SPACES = "    "

TEMPLATE_ENUM = """
{maindoc}
type {name} string

const (
{fields}
)
"""

TEMPLATE_HELPER = """
// Creates a decoder that errors on unknown Fields
// Returns nil if successfully decoded @from payload to @into type
// Returns error if failed to decode @from payload to @into type
func StrictDecode(into interface{}, from []byte) error {
    dec := json.NewDecoder(strings.NewReader(string(from)))
    dec.DisallowUnknownFields()

    if err := dec.Decode(into); err != nil {
        return err
    }
    return nil
}

// This helper is used to move struct's fields into a map.
// This function is useful to merge JSON objects.
func unwrapToMap(m map[string]any, data any) error {
    if bytes, err := json.Marshal(&data); err != nil {
        return fmt.Errorf("unwrapToMap: %s", err)
    } else if err := json.Unmarshal(bytes, &m); err != nil {
        return fmt.Errorf("unwrapToMap: %s, data=%s", err, string(bytes))
    }
    return nil
}
"""

TEMPLATE_ALTERNATE = """
// Only implemented on Alternate types that can take JSON NULL as value.
//
// This is a helper for the marshalling code. It should return true only when
// the Alternate is empty (no members are set), otherwise it returns false and
// the member set to be Marshalled.
type AbsentAlternate interface {
    ToAnyOrAbsent() (any, bool)
}
"""

TEMPLATE_ALTERNATE_CHECK_INVALID_JSON_NULL = """
    // Check for json-null first
    if string(data) == "null" {{
        return errors.New(`null not supported for {name}`)
    }}"""

TEMPLATE_ALTERNATE_NULLABLE_CHECK = """
        }} else if s.{var_name} != nil {{
            return *s.{var_name}, false"""

TEMPLATE_ALTERNATE_MARSHAL_CHECK = """
    if s.{var_name} != nil {{
        return json.Marshal(s.{var_name})
    }} else """

TEMPLATE_ALTERNATE_UNMARSHAL_CHECK = """
    // Check for {var_type}
    {{
        s.{var_name} = new({var_type})
        if err := StrictDecode(s.{var_name}, data); err == nil {{
            return nil
        }}
        s.{var_name} = nil
    }}

"""

TEMPLATE_ALTERNATE_NULLABLE_MARSHAL_CHECK = """
    if s.IsNull {
        return []byte("null"), nil
    } else """

TEMPLATE_ALTERNATE_NULLABLE_UNMARSHAL_CHECK = """
    // Check for json-null first
    if string(data) == "null" {
        s.IsNull = true
        return nil
    }"""

TEMPLATE_ALTERNATE_NULLABLE = """
func (s *{name}) ToAnyOrAbsent() (any, bool) {{
    if s != nil {{
        if s.IsNull {{
            return nil, false
{absent_check_fields}
        }}
    }}

    return nil, true
}}
"""

TEMPLATE_ALTERNATE_METHODS = """
func (s {name}) MarshalJSON() ([]byte, error) {{
{marshal_check_fields}
    return {marshal_return_default}
}}

func (s *{name}) UnmarshalJSON(data []byte) error {{
{unmarshal_check_fields}
    return fmt.Errorf("Can't convert to {name}: %s", string(data))
}}
"""


TEMPLATE_STRUCT_WITH_NULLABLE_MARSHAL = """
func (s {type_name}) MarshalJSON() ([]byte, error) {{
    m := make(map[string]any)
{map_members}{map_special}
    return json.Marshal(&m)
}}

func (s *{type_name}) UnmarshalJSON(data []byte) error {{
    tmp := {struct}{{}}

    if err := json.Unmarshal(data, &tmp); err != nil {{
        return err
    }}

{set_members}{set_special}
    return nil
}}
"""


TEMPLATE_UNION_CHECK_VARIANT_FIELD = """
    if s.{field} != nil && err == nil {{
        if len(bytes) != 0 {{
            err = errors.New(`multiple variant fields set`)
        }} else if err = unwrapToMap(m, s.{field}); err == nil {{
            m["{discriminator}"] = {go_enum_value}
            bytes, err = json.Marshal(m)
        }}
    }}
"""

TEMPLATE_UNION_CHECK_UNBRANCHED_FIELD = """
    if s.{field} && err == nil {{
        if len(bytes) != 0 {{
            err = errors.New(`multiple variant fields set`)
        }} else {{
            m["{discriminator}"] = {go_enum_value}
            bytes, err = json.Marshal(m)
        }}
    }}
"""

TEMPLATE_UNION_DRIVER_VARIANT_CASE = """
    case {go_enum_value}:
        s.{field} = new({member_type})
        if err := json.Unmarshal(data, s.{field}); err != nil {{
            s.{field} = nil
            return err
        }}"""

TEMPLATE_UNION_DRIVER_UNBRANCHED_CASE = """
    case {go_enum_value}:
        s.{field} = true
"""

TEMPLATE_UNION_METHODS = """
func (s {type_name}) MarshalJSON() ([]byte, error) {{
    var bytes []byte
    var err error
    m := make(map[string]any)
    {{
        type Alias {type_name}
        v := Alias(s)
        unwrapToMap(m, &v)
    }}
{check_fields}
    if err != nil {{
        return nil, fmt.Errorf("marshal {type_name} due:'%s' struct='%+v'", err, s)
    }} else if len(bytes) == 0 {{
        return nil, fmt.Errorf("marshal {type_name} unsupported, struct='%+v'", s)
    }}
    return bytes, nil
}}

func (s *{type_name}) UnmarshalJSON(data []byte) error {{
{base_type_def}
    tmp := struct {{
        {base_type_name}
    }}{{}}

    if err := json.Unmarshal(data, &tmp); err != nil {{
        return err
    }}
{base_type_assign_unmarshal}
    switch tmp.{discriminator} {{
{driver_cases}
    default:
        return fmt.Errorf("unmarshal {type_name} received unrecognized value '%s'",
            tmp.{discriminator})
    }}
    return nil
}}
"""


# Takes the documentation object of a specific type and returns
# that type's documentation and its member's docs.
def qapi_to_golang_struct_docs(doc: QAPIDoc) -> (str, Dict[str, str]):
    if doc is None:
        return "", {}

    cmt = "// "
    fmt = textwrap.TextWrapper(
        width=70, initial_indent=cmt, subsequent_indent=cmt
    )
    main = fmt.fill(doc.body.text)

    for section in doc.sections:
        # TODO is not a relevant section to Go applications
        if section.tag in ["TODO"]:
            continue

        if main != "":
            # Give empty line as space for the tag.
            main += "\n//\n"

        tag = "" if section.tag is None else f"{section.tag}: "
        text = section.text.replace("  ", " ")
        main += fmt.fill(f"{tag}{text}")

    fields = {}
    for key, value in doc.args.items():
        if len(value.text) > 0:
            fields[key] = " ".join(value.text.replace("\n", " ").split())

    return main, fields


def gen_golang(schema: QAPISchema, output_dir: str, prefix: str) -> None:
    vis = QAPISchemaGenGolangVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)


def qapi_name_is_base(name: str) -> bool:
    return qapi_name_is_object(name) and name.endswith("-base")


def qapi_name_is_object(name: str) -> bool:
    return name.startswith("q_obj_")


def qapi_base_name_to_parent(name: str) -> str:
    if qapi_name_is_base(name):
        name = name[6:-5]
    return name


def qapi_to_field_name(name: str) -> str:
    return name.title().replace("_", "").replace("-", "")


def qapi_to_field_name_enum(name: str) -> str:
    return name.title().replace("-", "")


def qapi_to_go_type_name(name: str) -> str:
    # We want to keep CamelCase for Golang types. We want to avoid removing
    # already set CameCase names while fixing uppercase ones, eg:
    # 1) q_obj_SocketAddress_base -> SocketAddressBase
    # 2) q_obj_WATCHDOG-arg -> WatchdogArg

    if qapi_name_is_object(name):
        # Remove q_obj_ prefix
        name = name[6:]

    # Handle CamelCase
    words = list(name.replace("_", "-").split("-"))
    name = words[0]
    if name.islower() or name.isupper():
        name = name.title()

    name += "".join(word.title() for word in words[1:])

    return name


def qapi_schema_type_to_go_type(qapitype: str) -> str:
    schema_types_to_go = {
        "str": "string",
        "null": "nil",
        "bool": "bool",
        "number": "float64",
        "size": "uint64",
        "int": "int64",
        "int8": "int8",
        "int16": "int16",
        "int32": "int32",
        "int64": "int64",
        "uint8": "uint8",
        "uint16": "uint16",
        "uint32": "uint32",
        "uint64": "uint64",
        "any": "any",
        "QType": "QType",
    }

    prefix = ""
    if qapitype.endswith("List"):
        prefix = "[]"
        qapitype = qapitype[:-4]

    qapitype = schema_types_to_go.get(qapitype, qapitype)
    return prefix + qapitype


# Helper for Alternate generation
def qapi_field_to_alternate_go_field(
    member_name: str, type_name: str
) -> Tuple[str, str, str]:
    # Nothing to generate on null types. We update some
    # variables to handle json-null on marshalling methods.
    if type_name == "null":
        return "IsNull", "bool", ""

    # On Alternates, fields are optional represented in Go as pointer
    return (
        qapi_to_field_name(member_name),
        qapi_schema_type_to_go_type(type_name),
        "*",
    )


def fetch_indent_blocks_over_args(
    args: List[dict[str:str]],
) -> Tuple[int, int]:
    maxname, maxtype = 0, 0
    blocks: tuple(int, int) = []
    for arg in args:
        if "comment" in arg or "doc" in arg:
            blocks.append((maxname, maxtype))
            maxname, maxtype = 0, 0

            if "comment" in arg:
                # They are single blocks
                continue

        if "type" not in arg:
            # Embed type are on top of the struct and the following
            # fields do not consider it for formatting
            blocks.append((maxname, maxtype))
            maxname, maxtype = 0, 0
            continue

        maxname = max(maxname, len(arg.get("name", "")))
        maxtype = max(maxtype, len(arg.get("type", "")))

    blocks.append((maxname, maxtype))
    return blocks


def fetch_indent_blocks_over_enum_with_docs(
    name: str, members: List[QAPISchemaEnumMember], docfields: Dict[str, str]
) -> Tuple[int]:
    maxname = 0
    blocks: List[int] = [0]
    for member in members:
        # For simplicity, every time we have doc, we add a new indent block
        hasdoc = member.name is not None and member.name in docfields

        enum_name = f"{name}{qapi_to_field_name_enum(member.name)}"
        maxname = (
            max(maxname, len(enum_name)) if not hasdoc else len(enum_name)
        )

        if hasdoc:
            blocks.append(maxname)
        else:
            blocks[-1] = maxname

    return blocks


# Helper function for boxed or self contained structures.
def generate_struct_type(
    type_name,
    type_doc: str = "",
    args: List[dict[str:str]] = None,
    indent: int = 0,
) -> str:
    base_indent = FOUR_SPACES * indent

    with_type = ""
    if type_name != "":
        with_type = f"\n{base_indent}type {type_name}"

    if type_doc != "":
        # Append line jump only if type_doc exists
        type_doc = f"\n{type_doc}"

    if args is None:
        # No args, early return
        return f"""{type_doc}{with_type} struct{{}}"""

    # The logic below is to generate fields of the struct.
    # We have to be mindful of the different indentation possibilities between
    # $var_name $var_type $var_tag that are vertically indented with gofmt.
    #
    # So, we first have to iterate over all args and find all indent blocks
    # by calculating the spaces between (1) member and type and between (2)
    # the type and tag. (1) and (2) is the tuple present in List returned
    # by the helper function fetch_indent_blocks_over_args.
    inner_indent = base_indent + FOUR_SPACES
    doc_indent = inner_indent + "// "
    fmt = textwrap.TextWrapper(
        width=70, initial_indent=doc_indent, subsequent_indent=doc_indent
    )

    indent_block = iter(fetch_indent_blocks_over_args(args))
    maxname, maxtype = next(indent_block)
    members = " {\n"
    for index, arg in enumerate(args):
        if "comment" in arg:
            maxname, maxtype = next(indent_block)
            members += f"""    // {arg["comment"]}\n"""
            # comments are single blocks, so we can skip to next arg
            continue

        name2type = ""
        if "doc" in arg:
            maxname, maxtype = next(indent_block)
            members += fmt.fill(arg["doc"])
            members += "\n"

        name = arg["name"]
        if "type" in arg:
            namelen = len(name)
            name2type = " " * max(1, (maxname - namelen + 1))

        type2tag = ""
        if "tag" in arg:
            typelen = len(arg["type"])
            type2tag = " " * max(1, (maxtype - typelen + 1))

        gotype = arg.get("type", "")
        tag = arg.get("tag", "")
        members += (
            f"""{inner_indent}{name}{name2type}{gotype}{type2tag}{tag}\n"""
        )

    members += f"{base_indent}}}\n"
    return f"""{type_doc}{with_type} struct{members}"""


def get_struct_field(
    self: QAPISchemaGenGolangVisitor,
    qapi_name: str,
    qapi_type_name: str,
    field_doc: str,
    within_nullable_struct: bool,
    is_optional: bool,
    is_variant: bool,
) -> Tuple[dict[str:str], bool]:
    field = qapi_to_field_name(qapi_name)
    member_type = qapi_schema_type_to_go_type(qapi_type_name)
    is_nullable = False

    optional = ""
    if is_optional:
        if member_type in self.accept_null_types:
            is_nullable = True
        else:
            optional = ",omitempty"

    # Use pointer to type when field is optional
    isptr = "*" if is_optional and member_type[0] not in "*[" else ""

    if within_nullable_struct:
        # Within a struct which has a field of type that can hold JSON NULL,
        # we have to _not_ use a pointer, otherwise the Marshal methods are
        # not called.
        isptr = "" if member_type in self.accept_null_types else isptr

    fieldtag = (
        '`json:"-"`' if is_variant else f'`json:"{qapi_name}{optional}"`'
    )
    arg = {
        "name": f"{field}",
        "type": f"{isptr}{member_type}",
        "tag": f"{fieldtag}",
    }
    if field_doc != "":
        arg["doc"] = field_doc

    return arg, is_nullable


# This helper is used whithin a struct that has members that accept JSON NULL.
def map_and_set(
    is_nullable: bool, field: str, field_is_optional: bool, name: str
) -> Tuple[str, str]:
    mapstr = ""
    setstr = ""
    if is_nullable:
        mapstr = f"""
    if val, absent := s.{field}.ToAnyOrAbsent(); !absent {{
        m["{name}"] = val
    }}
"""
        setstr += f"""
    if _, absent := (&tmp.{field}).ToAnyOrAbsent(); !absent {{
        s.{field} = &tmp.{field}
    }}
"""
    elif field_is_optional:
        mapstr = f"""
    if s.{field} != nil {{
        m["{name}"] = s.{field}
    }}
"""
        setstr = f"""    s.{field} = tmp.{field}\n"""
    else:
        mapstr = f"""    m["{name}"] = s.{field}\n"""
        setstr = f"""    s.{field} = tmp.{field}\n"""

    return mapstr, setstr


def recursive_base_nullable(
    self: QAPISchemaGenGolangVisitor, base: Optional[QAPISchemaObjectType]
) -> Tuple[List[dict[str:str]], str, str, str, str]:
    fields: List[dict[str:str]] = []
    map_members = ""
    set_members = ""
    map_special = ""
    set_special = ""

    if not base:
        return fields, map_members, set_members, map_special, set_special

    doc = self.docmap.get(base.name, None)
    _, docfields = qapi_to_golang_struct_docs(doc)

    if base.base is not None:
        embed_base = self.schema.lookup_entity(base.base.name)
        (
            fields,
            map_members,
            set_members,
            map_special,
            set_special,
        ) = recursive_base_nullable(self, embed_base)

    for member in base.local_members:
        field_doc = docfields.get(member.name, "")
        field, _ = get_struct_field(
            self,
            member.name,
            member.type.name,
            field_doc,
            True,
            member.optional,
            False,
        )
        fields.append(field)

        member_type = qapi_schema_type_to_go_type(member.type.name)
        nullable = member_type in self.accept_null_types
        field_name = qapi_to_field_name(member.name)
        tomap, toset = map_and_set(
            nullable, field_name, member.optional, member.name
        )
        if nullable:
            map_special += tomap
            set_special += toset
        else:
            map_members += tomap
            set_members += toset

    return fields, map_members, set_members, map_special, set_special


# Helper function. This is executed when the QAPI schema has members
# that could accept JSON NULL (e.g: StrOrNull in QEMU"s QAPI schema).
# This struct will need to be extended with Marshal/Unmarshal methods to
# properly handle such atypical members.
#
# Only the Marshallaing methods are generated but we do need to iterate over
# all the members to properly set/check them in those methods.
def struct_with_nullable_generate_marshal(
    self: QAPISchemaGenGolangVisitor,
    name: str,
    base: Optional[QAPISchemaObjectType],
    members: List[QAPISchemaObjectTypeMember],
    variants: Optional[QAPISchemaVariants],
) -> str:
    (
        fields,
        map_members,
        set_members,
        map_special,
        set_special,
    ) = recursive_base_nullable(self, base)

    doc = self.docmap.get(name, None)
    _, docfields = qapi_to_golang_struct_docs(doc)

    if members:
        for member in members:
            field_doc = docfields.get(member.name, "")
            field, _ = get_struct_field(
                self,
                member.name,
                member.type.name,
                field_doc,
                True,
                member.optional,
                False,
            )
            fields.append(field)

            member_type = qapi_schema_type_to_go_type(member.type.name)
            nullable = member_type in self.accept_null_types
            tomap, toset = map_and_set(
                nullable,
                qapi_to_field_name(member.name),
                member.optional,
                member.name,
            )
            if nullable:
                map_special += tomap
                set_special += toset
            else:
                map_members += tomap
                set_members += toset

    if variants:
        for variant in variants.variants:
            if variant.type.is_implicit():
                continue

            field, _ = get_struct_field(
                self,
                variant.name,
                variant.type.name,
                True,
                variant.optional,
                True,
            )
            fields.append(field)

            member_type = qapi_schema_type_to_go_type(variant.type.name)
            nullable = member_type in self.accept_null_types
            tomap, toset = map_and_set(
                nullable,
                qapi_to_field_name(variant.name),
                variant.optional,
                variant.name,
            )
            if nullable:
                map_special += tomap
                set_special += toset
            else:
                map_members += tomap
                set_members += toset

    type_name = qapi_to_go_type_name(name)
    struct = generate_struct_type("", args=fields, indent=1)
    return string_to_code(
        TEMPLATE_STRUCT_WITH_NULLABLE_MARSHAL.format(
            struct=struct[1:-1],
            type_name=type_name,
            map_members=map_members,
            map_special=map_special,
            set_members=set_members,
            set_special=set_special,
        )
    )


def recursive_base(
    self: QAPISchemaGenGolangVisitor,
    base: Optional[QAPISchemaObjectType],
    discriminator: Optional[str] = None,
) -> Tuple[List[dict[str:str]], bool]:
    fields: List[dict[str:str]] = []
    with_nullable = False

    if not base:
        return fields, with_nullable

    if base.base is not None:
        embed_base = self.schema.lookup_entity(base.base.name)
        fields, with_nullable = recursive_base(self, embed_base, discriminator)

    doc = self.docmap.get(qapi_base_name_to_parent(base.name), None)
    _, docfields = qapi_to_golang_struct_docs(doc)

    for member in base.local_members:
        if discriminator and member.name == discriminator:
            continue

        field_doc = docfields.get(member.name, "")
        field, nullable = get_struct_field(
            self,
            member.name,
            member.type.name,
            field_doc,
            False,
            member.optional,
            False,
        )
        fields.append(field)
        with_nullable = True if nullable else with_nullable

    return fields, with_nullable


# Helper function that is used for most of QAPI types
def qapi_to_golang_struct(
    self: QAPISchemaGenGolangVisitor,
    name: str,
    info: Optional[QAPISourceInfo],
    __: QAPISchemaIfCond,
    ___: List[QAPISchemaFeature],
    base: Optional[QAPISchemaObjectType],
    members: List[QAPISchemaObjectTypeMember],
    variants: Optional[QAPISchemaVariants],
    indent: int = 0,
    doc_enabled: bool = True,
) -> str:
    discriminator = None if not variants else variants.tag_member.name
    fields, with_nullable = recursive_base(self, base, discriminator)

    doc = self.docmap.get(name, None)
    type_doc, docfields = qapi_to_golang_struct_docs(doc)
    if not doc_enabled:
        type_doc = ""

    if members:
        for member in members:
            field_doc = docfields.get(member.name, "") if doc_enabled else ""
            field, nullable = get_struct_field(
                self,
                member.name,
                member.type.name,
                field_doc,
                False,
                member.optional,
                False,
            )
            fields.append(field)
            with_nullable = True if nullable else with_nullable

    exists = {}
    if variants:
        fields.append({"comment": "Variants fields"})
        for variant in variants.variants:
            if variant.type.is_implicit():
                continue

            exists[variant.name] = True
            field_doc = docfields.get(variant.name, "") if doc_enabled else ""
            field, nullable = get_struct_field(
                self,
                variant.name,
                variant.type.name,
                field_doc,
                False,
                True,
                True,
            )
            fields.append(field)
            with_nullable = True if nullable else with_nullable

    if info.defn_meta == "union" and variants:
        enum_name = variants.tag_member.type.name
        enum_obj = self.schema.lookup_entity(enum_name)
        if len(exists) != len(enum_obj.members):
            fields.append({"comment": "Unbranched enum fields"})
            for member in enum_obj.members:
                if member.name in exists:
                    continue

                field_doc = (
                    docfields.get(member.name, "") if doc_enabled else ""
                )
                field, nullable = get_struct_field(
                    self, member.name, "bool", field_doc, False, False, True
                )
                fields.append(field)
                with_nullable = True if nullable else with_nullable

    type_name = qapi_to_go_type_name(name)
    content = string_to_code(
        generate_struct_type(
            type_name, type_doc=type_doc, args=fields, indent=indent
        )
    )
    if with_nullable:
        content += struct_with_nullable_generate_marshal(
            self, name, base, members, variants
        )
    return content


def qapi_to_golang_methods_union(
    self: QAPISchemaGenGolangVisitor,
    name: str,
    base: Optional[QAPISchemaObjectType],
    variants: Optional[QAPISchemaVariants],
) -> str:
    type_name = qapi_to_go_type_name(name)

    assert base
    base_type_assign_unmarshal = ""
    base_type_name = qapi_to_go_type_name(base.name)
    base_type_def = qapi_to_golang_struct(
        self,
        base.name,
        base.info,
        base.ifcond,
        base.features,
        base.base,
        base.members,
        base.branches,
        indent=1,
        doc_enabled=False,
    )

    discriminator = qapi_to_field_name(variants.tag_member.name)
    for member in base.local_members:
        field = qapi_to_field_name(member.name)
        if field == discriminator:
            continue
        base_type_assign_unmarshal += f"""
    s.{field} = tmp.{field}"""

    driver_cases = ""
    check_fields = ""
    exists = {}
    enum_name = variants.tag_member.type.name
    if variants:
        for var in variants.variants:
            if var.type.is_implicit():
                continue

            field = qapi_to_field_name(var.name)
            enum_value = qapi_to_field_name_enum(var.name)
            member_type = qapi_schema_type_to_go_type(var.type.name)
            go_enum_value = f"""{enum_name}{enum_value}"""
            exists[go_enum_value] = True

            check_fields += TEMPLATE_UNION_CHECK_VARIANT_FIELD.format(
                field=field,
                discriminator=variants.tag_member.name,
                go_enum_value=go_enum_value,
            )
            driver_cases += TEMPLATE_UNION_DRIVER_VARIANT_CASE.format(
                go_enum_value=go_enum_value,
                field=field,
                member_type=member_type,
            )

    enum_obj = self.schema.lookup_entity(enum_name)
    if len(exists) != len(enum_obj.members):
        for member in enum_obj.members:
            value = qapi_to_field_name_enum(member.name)
            go_enum_value = f"""{enum_name}{value}"""

            if go_enum_value in exists:
                continue

            field = qapi_to_field_name(member.name)

            check_fields += TEMPLATE_UNION_CHECK_UNBRANCHED_FIELD.format(
                field=field,
                discriminator=variants.tag_member.name,
                go_enum_value=go_enum_value,
            )
            driver_cases += TEMPLATE_UNION_DRIVER_UNBRANCHED_CASE.format(
                go_enum_value=go_enum_value,
                field=field,
            )

    return string_to_code(
        TEMPLATE_UNION_METHODS.format(
            type_name=type_name,
            check_fields=check_fields[1:],
            base_type_def=base_type_def[1:],
            base_type_name=base_type_name,
            base_type_assign_unmarshal=base_type_assign_unmarshal,
            discriminator=discriminator,
            driver_cases=driver_cases[1:],
        )
    )


def generate_template_alternate(
    self: QAPISchemaGenGolangVisitor,
    name: str,
    variants: Optional[QAPISchemaVariants],
) -> str:
    absent_check_fields = ""
    args: List[dict[str:str]] = []
    nullable = name in self.accept_null_types
    if nullable:
        # Examples in QEMU QAPI schema: StrOrNull and BlockdevRefOrNull
        marshal_return_default = """[]byte("{}"), nil"""
        marshal_check_fields = TEMPLATE_ALTERNATE_NULLABLE_MARSHAL_CHECK[1:]
        unmarshal_check_fields = TEMPLATE_ALTERNATE_NULLABLE_UNMARSHAL_CHECK
    else:
        marshal_return_default = f'nil, errors.New("{name} has empty fields")'
        marshal_check_fields = ""
        unmarshal_check_fields = (
            TEMPLATE_ALTERNATE_CHECK_INVALID_JSON_NULL.format(name=name)
        )

    doc = self.docmap.get(name, None)
    content, docfields = qapi_to_golang_struct_docs(doc)
    if variants:
        for var in variants.variants:
            var_name, var_type, isptr = qapi_field_to_alternate_go_field(
                var.name, var.type.name
            )
            args.append(
                {
                    "name": f"{var_name}",
                    "type": f"{isptr}{var_type}",
                    "doc": docfields.get(var.name, ""),
                }
            )
            # Null is special, handled first
            if var.type.name == "null":
                assert nullable
                continue

            if nullable:
                absent_check_fields += string_to_code(
                    TEMPLATE_ALTERNATE_NULLABLE_CHECK[1:].format(
                        var_name=var_name
                    )
                )
            skip_indent = 1 + len(FOUR_SPACES)
            if marshal_check_fields == "":
                skip_indent = 1
            marshal_check_fields += TEMPLATE_ALTERNATE_MARSHAL_CHECK[
                skip_indent:
            ].format(var_name=var_name)
            unmarshal_check_fields += TEMPLATE_ALTERNATE_UNMARSHAL_CHECK[
                :-1
            ].format(var_name=var_name, var_type=var_type)

    content += string_to_code(generate_struct_type(name, args=args))
    if nullable:
        content += string_to_code(
            TEMPLATE_ALTERNATE_NULLABLE.format(
                name=name, absent_check_fields=absent_check_fields[:-1]
            )
        )
    content += string_to_code(
        TEMPLATE_ALTERNATE_METHODS.format(
            name=name,
            marshal_check_fields=marshal_check_fields[:-6],
            marshal_return_default=marshal_return_default,
            unmarshal_check_fields=unmarshal_check_fields[1:],
        )
    )
    return "\n" + content


def generate_content_from_dict(data: dict[str, str]) -> str:
    content = ""

    for name in sorted(data):
        content += data[name]

    return content.replace("\n\n\n", "\n\n")


def string_to_code(text: str) -> str:
    DOUBLE_BACKTICK = "``"
    result = ""
    for line in text.splitlines():
        # replace left four spaces with tabs
        limit = len(line) - len(line.lstrip())
        result += line[:limit].replace(FOUR_SPACES, "\t")

        # work with the rest of the line
        if line[limit : limit + 2] == "//":
            # gofmt tool does not like comments with backticks.
            result += line[limit:].replace(DOUBLE_BACKTICK, '"')
        else:
            result += line[limit:]
        result += "\n"

    return result


class QAPISchemaGenGolangVisitor(QAPISchemaVisitor):
    # pylint: disable=too-many-arguments
    def __init__(self, _: str):
        super().__init__()
        types = (
            "alternate",
            "enum",
            "helper",
            "struct",
            "union",
        )
        self.target = dict.fromkeys(types, "")
        self.schema: QAPISchema
        self.golang_package_name = "qapi"
        self.enums: dict[str, str] = {}
        self.alternates: dict[str, str] = {}
        self.structs: dict[str, str] = {}
        self.unions: dict[str, str] = {}
        self.accept_null_types = []
        self.docmap = {}

    def visit_begin(self, schema: QAPISchema) -> None:
        self.schema = schema

        # We need to be aware of any types that accept JSON NULL
        for name, entity in self.schema._entity_dict.items():
            if not isinstance(entity, QAPISchemaAlternateType):
                # Assume that only Alternate types accept JSON NULL
                continue

            for var in entity.alternatives.variants:
                if var.type.name == "null":
                    self.accept_null_types.append(name)
                    break

        # iterate once in schema.docs to map doc objects to its name
        for doc in schema.docs:
            if doc.symbol is None:
                continue
            self.docmap[doc.symbol] = doc

        # Every Go file needs to reference its package name
        # and most have some imports too.
        for target in self.target:
            self.target[target] = f"package {self.golang_package_name}"

            imports = "\n"
            if target == "struct":
                imports += """
import "encoding/json"
"""
            elif target == "helper":
                imports += """
import (
    "encoding/json"
    "fmt"
    "strings"
)
"""
            else:
                imports += """
import (
    "encoding/json"
    "errors"
    "fmt"
)
"""
            if target != "enum":
                self.target[target] += string_to_code(imports)

        self.target["helper"] += string_to_code(TEMPLATE_HELPER)
        self.target["alternate"] += string_to_code(TEMPLATE_ALTERNATE)

    def visit_end(self) -> None:
        del self.schema
        self.target["enum"] += generate_content_from_dict(self.enums)
        self.target["alternate"] += generate_content_from_dict(self.alternates)
        self.target["struct"] += generate_content_from_dict(self.structs)
        self.target["union"] += generate_content_from_dict(self.unions)

    def visit_object_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        base: Optional[QAPISchemaObjectType],
        members: List[QAPISchemaObjectTypeMember],
        branches: Optional[QAPISchemaBranches],
    ) -> None:
        # Do not handle anything besides struct and unions.
        if (
            name == self.schema.the_empty_object_type.name
            or not isinstance(name, str)
            or info.defn_meta not in ["struct", "union"]
        ):
            return

        # Base structs are embed
        if qapi_name_is_base(name):
            return

        # visit all inner objects as well, they are not going to be
        # called by python's generator.
        if branches:
            for branch in branches.variants:
                assert isinstance(branch.type, QAPISchemaObjectType)
                self.visit_object_type(
                    self,
                    branch.type.name,
                    branch.type.info,
                    branch.type.ifcond,
                    branch.type.base,
                    branch.type.local_members,
                    branch.type.branches,
                )

        # Save generated Go code to be written later
        if info.defn_meta == "struct":
            assert name not in self.structs
            self.structs[name] = string_to_code(
                qapi_to_golang_struct(
                    self, name, info, ifcond, features, base, members, branches
                )
            )
        else:
            assert name not in self.unions
            self.unions[name] = qapi_to_golang_struct(
                self, name, info, ifcond, features, base, members, branches
            )
            self.unions[name] += qapi_to_golang_methods_union(
                self, name, base, branches
            )

    def visit_alternate_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        variants: QAPISchemaVariants,
    ) -> None:
        assert name not in self.alternates
        self.alternates[name] = generate_template_alternate(
            self, name, variants
        )

    def visit_enum_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        members: List[QAPISchemaEnumMember],
        prefix: Optional[str],
    ) -> None:
        assert name not in self.enums
        doc = self.docmap.get(name, None)
        maindoc, docfields = qapi_to_golang_struct_docs(doc)

        # The logic below is to generate QAPI enums as blocks of Go consts
        # each with its own type for type safety inside Go applications.
        #
        # Block of const() blocks are vertically indented so we have to
        # first iterate over all names to calculate space between
        # $var_name and $var_type. This is achieved by helper function
        # @fetch_indent_blocks_over_enum_with_docs()
        #
        # A new indentation block is defined by empty line or a comment.

        indent_block = iter(
            fetch_indent_blocks_over_enum_with_docs(name, members, docfields)
        )
        maxname = next(indent_block)
        fields = ""
        for index, member in enumerate(members):
            # For simplicity, every time we have doc, we go to next indent block
            hasdoc = member.name is not None and member.name in docfields

            if hasdoc:
                maxname = next(indent_block)

            enum_name = f"{name}{qapi_to_field_name_enum(member.name)}"
            name2type = " " * (maxname - len(enum_name) + 1)

            if hasdoc:
                docstr = (
                    textwrap.TextWrapper(width=80)
                    .fill(docfields[member.name])
                    .replace("\n", "\n\t// ")
                )
                fields += f"""\t// {docstr}\n"""

            fields += f"""\t{enum_name}{name2type}{name} = "{member.name}"\n"""

        self.enums[name] = TEMPLATE_ENUM.format(
            maindoc=maindoc, name=name, fields=fields[:-1]
        )

    def visit_array_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        element_type: QAPISchemaType,
    ) -> None:
        pass

    def visit_command(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        arg_type: Optional[QAPISchemaObjectType],
        ret_type: Optional[QAPISchemaType],
        gen: bool,
        success_response: bool,
        boxed: bool,
        allow_oob: bool,
        allow_preconfig: bool,
        coroutine: bool,
    ) -> None:
        pass

    def visit_event(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        arg_type: Optional[QAPISchemaObjectType],
        boxed: bool,
    ) -> None:
        pass

    def write(self, output_dir: str) -> None:
        for module_name, content in self.target.items():
            go_module = module_name + "s.go"
            go_dir = "go"
            pathname = os.path.join(output_dir, go_dir, go_module)
            odir = os.path.dirname(pathname)
            os.makedirs(odir, exist_ok=True)

            with open(pathname, "w", encoding="utf8") as outfile:
                outfile.write(content)
