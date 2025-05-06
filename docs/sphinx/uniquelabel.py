# coding=utf-8
#
# Copyright (c) 2025 Linaro
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Sphinx extension to create a unique label by concatenating
# the name of the origin document with the label text.
#
# Sphinx requires that labels within documents are unique across
# the whole manual. This is because the "create a hyperlink" directive
# specifies only the name of the label, not a filename+label.
# Some Sphinx versions will warn about duplicate labels, but
# even if there is no warning there is still an ambiguity and no
# guarantee that the hyperlink will be created to the right target.
#
# For QEMU this is awkward, because we have various .rst.inc fragments
# which we include into multiple .rst files. If you define a label in
# the .rst.inc file then it will be a duplicate label.
#
# The uniquelabel directive is our fix for this: it creates a label
# whose name includes the name of the top level .rst file. This is then
# unique even if the .rst.inc file is included in multiple places, and
# when we create a hyperlink we can explicitly specify which label we
# are targeting.
#
# Concretely, if you have a foo/bar.rst and a foo/baz.rst that
# both include wat.rst.inc, then in wat.rst.inc you can write
# .. uniquelabel:: mylabel
# and it will be as if you had written a reference label:
# .. _foo/bar-mylabel
# or
# .. _foo/baz-mylabel
# depending on which file included wat.rst.inc, and you can link to
# whichever one you intend via any of the usual markup, e.g.
# `documentation of the thing in bar <foo/bar-mylabel>`.

"""uniquelabel is a Sphinx extension that implements the uniquelabel directive"""

from docutils import nodes
from docutils.statemachine import ViewList
from docutils.parsers.rst import directives, Directive
import sphinx

__version__ = '1.0'

class UniqueLabelDocDirective(Directive):
    """Create a unique label by including the docname"""
    required_arguments = 1
    optional_arguments = 0
    has_content = False

    def run(self):
        env = self.state.document.settings.env
        label = self.arguments[0]

        refline = ".. _" + env.docname + "-" + label + ":"

        rstlist = ViewList()
        rstlist.append(refline, "generated text", 0)

        node = nodes.paragraph()
        self.state.nested_parse(rstlist, 0, node)
        return node.children

def setup(app):
    """ Register uniquelabel directive with Sphinx"""
    app.add_directive('uniquelabel', UniqueLabelDocDirective)

    return dict(
        version = __version__,
        parallel_read_safe = True,
        parallel_write_safe = True
    )
