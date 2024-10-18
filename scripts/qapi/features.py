"""
QAPI types generator

Copyright 2024 Red Hat

This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.
"""

from typing import List, Optional

from .common import c_enum_const, mcgen, c_name
from .gen import QAPISchemaMonolithicCVisitor
from .schema import (
    QAPISchema,
    QAPISchemaFeature,
)
from .source import QAPISourceInfo


class QAPISchemaGenFeatureVisitor(QAPISchemaMonolithicCVisitor):

    def __init__(self, prefix: str):
        super().__init__(
            prefix, 'qapi-features',
            ' * Schema-defined QAPI features',
            __doc__)

        self.features = {}

    def visit_begin(self, schema: QAPISchema):
        self.features = schema._feature_dict

    def visit_end(self) -> None:
        features = [
            self.features[f]
            for f in QAPISchemaFeature.SPECIAL_NAMES
        ]

        features.extend(
            sorted(
                filter(lambda f: not f.is_special(),
                       self.features.values()),
                key=lambda f: f.name)
        )

        self._genh.add("typedef enum {\n")
        for f in features:
            self._genh.add(f"    {c_enum_const('qapi_feature', f.name)}")
            if f.name in QAPISchemaFeature.SPECIAL_NAMES:
                self._genh.add(f" = {c_enum_const('qapi', f.name)},\n" )
            else:
                self._genh.add(",\n")

        self._genh.add("} " + c_name('QapiFeature') + ";\n")

def gen_features(schema: QAPISchema,
                 output_dir: str,
                 prefix: str) -> None:
    vis = QAPISchemaGenFeatureVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)
