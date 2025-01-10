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
from typing import List, Optional

from .schema import (
    QAPISchema,
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


TEMPLATE_ENUM = """
{maindoc}
type {name} string

const (
{fields}
)
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


def qapi_to_field_name_enum(name: str) -> str:
    return name.title().replace("-", "")


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


def generate_content_from_dict(data: dict[str, str]) -> str:
    content = ""

    for name in sorted(data):
        content += data[name]

    return content.replace("\n\n\n", "\n\n")


class QAPISchemaGenGolangVisitor(QAPISchemaVisitor):
    # pylint: disable=too-many-arguments
    def __init__(self, _: str):
        super().__init__()
        types = ("enum",)
        self.target = dict.fromkeys(types, "")
        self.schema: QAPISchema
        self.golang_package_name = "qapi"
        self.enums: dict[str, str] = {}
        self.docmap = {}

    def visit_begin(self, schema: QAPISchema) -> None:
        self.schema = schema

        # iterate once in schema.docs to map doc objects to its name
        for doc in schema.docs:
            if doc.symbol is None:
                continue
            self.docmap[doc.symbol] = doc

        # Every Go file needs to reference its package name
        for target in self.target:
            self.target[target] = f"package {self.golang_package_name}"

    def visit_end(self) -> None:
        del self.schema
        self.target["enum"] += generate_content_from_dict(self.enums)

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
        pass

    def visit_alternate_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        variants: QAPISchemaVariants,
    ) -> None:
        pass

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
