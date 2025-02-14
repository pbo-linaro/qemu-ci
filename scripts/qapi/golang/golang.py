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

import os, shutil, textwrap
from typing import List, Optional, Tuple

from ..schema import (
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
from ..source import QAPISourceInfo

FOUR_SPACES = "    "

TEMPLATE_GENERATED_HEADER = """
/*
 * Copyright 2025 Red Hat, Inc.
 * SPDX-License-Identifier: (MIT-0 and GPL-2.0-or-later)
 */

/****************************************************************************
 * THIS CODE HAS BEEN GENERATED. DO NOT CHANGE IT DIRECTLY                  *
 ****************************************************************************/
package {package_name}
"""

TEMPLATE_GO_IMPORTS = """
import (
{imports}
)
"""

TEMPLATE_ENUM = """
type {name} string

const (
{fields}
)
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
        if err := strictDecode(s.{var_name}, data); err == nil {{
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
    is_optional: bool,
    is_variant: bool,
) -> dict[str:str]:
    field = qapi_to_field_name(qapi_name)
    member_type = qapi_schema_type_to_go_type(qapi_type_name)

    optional = ""
    if is_optional:
        if member_type not in self.accept_null_types:
            optional = ",omitempty"

    # Use pointer to type when field is optional
    isptr = "*" if is_optional and member_type[0] not in "*[" else ""

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

    return arg


def recursive_base(
    self: QAPISchemaGenGolangVisitor,
    base: Optional[QAPISchemaObjectType],
) -> List[dict[str:str]]:
    fields: List[dict[str:str]] = []

    if not base:
        return fields

    if base.base is not None:
        embed_base = self.schema.lookup_entity(base.base.name)
        fields = recursive_base(self, embed_base)

    doc = self.docmap.get(base.name, None)
    _, docfields = qapi_to_golang_struct_docs(doc)

    for member in base.local_members:
        field_doc = docfields.get(member.name, "")
        field = get_struct_field(
            self,
            member.name,
            member.type.name,
            field_doc,
            member.optional,
            False,
        )
        fields.append(field)

    return fields


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
    fields = recursive_base(self, base)

    doc = self.docmap.get(name, None)
    type_doc, docfields = qapi_to_golang_struct_docs(doc)
    if not doc_enabled:
        type_doc = ""

    if members:
        for member in members:
            field_doc = docfields.get(member.name, "") if doc_enabled else ""
            field = get_struct_field(
                self,
                member.name,
                member.type.name,
                field_doc,
                member.optional,
                False,
            )
            fields.append(field)

    exists = {}
    if variants:
        fields.append({"comment": "Variants fields"})
        for variant in variants.variants:
            if variant.type.is_implicit():
                continue

            exists[variant.name] = True
            field_doc = docfields.get(variant.name, "") if doc_enabled else ""
            field = get_struct_field(
                self,
                variant.name,
                variant.type.name,
                field_doc,
                True,
                True,
            )
            fields.append(field)

    type_name = qapi_to_go_type_name(name)
    content = string_to_code(
        generate_struct_type(
            type_name, type_doc=type_doc, args=fields, indent=indent
        )
    )
    return content


def generate_template_alternate(
    self: QAPISchemaGenGolangVisitor,
    name: str,
    variants: Optional[QAPISchemaVariants],
) -> str:
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


def generate_template_imports(words: List[str]) -> str:
    if len(words) == 0:
        return ""

    if len(words) == 1:
        return '\nimport "{words[0]}"\n'

    return TEMPLATE_GO_IMPORTS.format(
        imports="\n".join(f'\t"{w}"' for w in words)
    )


class QAPISchemaGenGolangVisitor(QAPISchemaVisitor):
    # pylint: disable=too-many-arguments
    def __init__(self, _: str):
        super().__init__()
        gofiles = ("protocol.go", "utils.go")
        # Map each qapi type to the necessary Go imports
        types = {
            "alternate": ["encoding/json", "errors", "fmt"],
            "enum": [],
            "struct": ["encoding/json"],
        }

        self.schema: QAPISchema
        self.golang_package_name = "qapi"
        self.duplicate = list(gofiles)
        self.enums: dict[str, str] = {}
        self.alternates: dict[str, str] = {}
        self.structs: dict[str, str] = {}
        self.accept_null_types = []
        self.docmap = {}

        self.types = dict.fromkeys(types, "")
        self.types_import = types

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

        for qapitype, imports in self.types_import.items():
            self.types[qapitype] = TEMPLATE_GENERATED_HEADER[1:].format(
                package_name=self.golang_package_name
            )
            self.types[qapitype] += generate_template_imports(imports)

    def visit_end(self) -> None:
        del self.schema
        self.types["enum"] += generate_content_from_dict(self.enums)
        self.types["alternate"] += generate_content_from_dict(self.alternates)
        self.types["struct"] += generate_content_from_dict(self.structs)

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
        # Do not handle anything besides struct.
        if (
            name == self.schema.the_empty_object_type.name
            or not isinstance(name, str)
            or info.defn_meta not in ["struct"]
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

        if maindoc != "":
            maindoc = f"\n{maindoc}"

        self.enums[name] = maindoc + TEMPLATE_ENUM.format(
            name=name, fields=fields[:-1]
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

    def write(self, outdir: str) -> None:
        godir = "go"
        targetpath = os.path.join(outdir, godir)
        os.makedirs(targetpath, exist_ok=True)

        # Content to be copied over
        srcdir = os.path.dirname(os.path.realpath(__file__))
        for filename in self.duplicate:
            srcpath = os.path.join(srcdir, filename)
            dstpath = os.path.join(targetpath, filename)
            shutil.copyfile(srcpath, dstpath)

        # Types to be generated
        for qapitype, content in self.types.items():
            gofile = f"gen_type_{qapitype}.go"
            pathname = os.path.join(targetpath, gofile)

            with open(pathname, "w", encoding="utf8") as outfile:
                outfile.write(content)
