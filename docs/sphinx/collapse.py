"""
Adds a collapsible section to an HTML page using a details_ element.

.. _details: https://developer.mozilla.org/en-US/docs/Web/HTML/Element/details

Modified (for formatting, vendoring and removing dependencies) from
sphinx_toolbox.collapse, originally by Dominic Davis-Foster
<dominic@davis-foster.co.uk>

See https://github.com/sphinx-toolbox/sphinx-toolbox/tree/master

"""

#
#  Copyright © 2021 Dominic Davis-Foster <dominic@davis-foster.co.uk>
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be
#  included in all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
#  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
#  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
#  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
#  DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
#  OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
#  OR OTHER DEALINGS IN THE SOFTWARE.
#

# stdlib
from typing import (
    Any,
    ClassVar,
    Dict,
    Optional,
    Sequence,
)

# 3rd party
from docutils import nodes
from docutils.parsers.rst import directives
from docutils.parsers.rst.roles import set_classes

from sphinx.application import Sphinx
from sphinx.util.docutils import SphinxDirective
from sphinx.writers.html import HTMLTranslator


__all__ = (
    "CollapseDirective",
    "CollapseNode",
    "visit_collapse_node",
    "depart_collapse_node",
    "setup",
)


def flag(argument: Any) -> bool:
    """
    Check for a valid flag option (no argument) and return :py:obj:`True`.

    Used in the ``option_spec`` of directives.

    .. seealso::

       :class:`docutils.parsers.rst.directives.flag`, which returns
       :py:obj:`None` instead of :py:obj:`True`.

    :raises: :exc:`ValueError` if an argument is given.
    """
    if argument and argument.strip():
        raise ValueError(f"No argument is allowed; {argument!r} supplied")
    else:
        return True


class CollapseDirective(SphinxDirective):
    """
    A Sphinx directive to add a collapsible section to an HTML page
    using a details_ element.

    .. _details:
       https://developer.mozilla.org/en-US/docs/Web/HTML/Element/details
    """

    final_argument_whitespace: ClassVar[bool] = True
    has_content: ClassVar[bool] = True

    # The label
    required_arguments: ClassVar[int] = 1

    option_spec = {
        "class": directives.class_option,
        "name": directives.unchanged,
        "open": flag,
    }

    def run(self) -> Sequence[nodes.Node]:
        """
        Process the content of the directive.
        """

        set_classes(self.options)
        self.assert_has_content()

        text = "\n".join(self.content)
        label = self.arguments[0]

        collapse_node = CollapseNode(text, label, **self.options)

        self.add_name(collapse_node)

        collapse_node["classes"].append(f"summary-{nodes.make_id(label)}")

        self.state.nested_parse(
            self.content, self.content_offset, collapse_node
        )

        return [collapse_node]


class CollapseNode(nodes.Body, nodes.Element):
    """
    Node that represents a collapsible section.

    :param rawsource:
    :param label:
    """

    def __init__(
        self,
        rawsource: str = "",
        label: Optional[str] = None,
        *children: Any,
        **attributes: Any,
    ) -> None:
        super().__init__(rawsource, *children, **attributes)
        self.label = label


def visit_collapse_node(translator: HTMLTranslator, node: CollapseNode) -> None:
    """
    Visit a :class:`~.CollapseNode`.

    :param translator:
    :param node: The node being visited.
    """

    tag_parts = ["details"]

    if names := node.get("names", None):
        tag_parts.append(f'name="{" ".join(names)}"')

    if classes := node.get("classes", None):
        tag_parts.append(f'class="{" ".join(classes)}"')

    if node.attributes.get("open", False):
        tag_parts.append("open")

    translator.body.append(
        f"<{' '.join(tag_parts)}>\n<summary>{node.label}</summary>"
    )
    translator.context.append("</details>")


def depart_collapse_node(
    translator: HTMLTranslator, node: CollapseNode
) -> None:
    """
    Depart a :class:`~.CollapseNode`.

    :param translator:
    :param node: The node being visited.
    """
    translator.body.append(translator.context.pop())


def setup(app: Sphinx) -> Dict[str, Any]:
    """
    Setup :mod:`sphinx_toolbox.collapse`.

    :param app: The Sphinx application.
    """
    app.add_directive("collapse", CollapseDirective)
    app.add_node(
        CollapseNode,
        html=(visit_collapse_node, depart_collapse_node),
        latex=(lambda *args, **kwargs: None, lambda *args, **kwargs: None),
    )

    return {
        "parallel_read_safe": True,
        "version": "3.5.0",
    }
