"""
Sphinx cross-version compatibility goop
"""

import re
from typing import (
    Any,
    Callable,
    Optional,
    Type,
)

from docutils import nodes
from docutils.nodes import Element, Node, Text

import sphinx
from sphinx import addnodes
from sphinx.environment import BuildEnvironment
from sphinx.roles import XRefRole
from sphinx.util import docfields
from sphinx.util.docutils import (
    ReferenceRole,
    SphinxDirective,
    switch_source_input,
)
from sphinx.util.nodes import nested_parse_with_titles
from sphinx.util.typing import TextlikeNode


MAKE_XREF_WORKAROUND = sphinx.version_info[:3] < (4, 1, 0)


space_node: Callable[[str], Node]
keyword_node: Callable[[str, str], Node]

if sphinx.version_info[:3] >= (4, 0, 0):
    space_node = addnodes.desc_sig_space
    keyword_node = addnodes.desc_sig_keyword
else:
    space_node = Text
    keyword_node = addnodes.desc_annotation


def nested_parse(directive: SphinxDirective, content_node: Element) -> None:
    """
    This helper preserves error parsing context across sphinx versions.
    """

    # necessary so that the child nodes get the right source/line set
    content_node.document = directive.state.document

    try:
        # Modern sphinx (6.2.0+) supports proper offsetting for
        # nested parse error context management
        nested_parse_with_titles(
            directive.state,
            directive.content,
            content_node,
            content_offset=directive.content_offset,  # type: ignore[call-arg]
        )
    except TypeError:
        # No content_offset argument. Fall back to SSI method.
        with switch_source_input(directive.state, directive.content):
            nested_parse_with_titles(
                directive.state, directive.content, content_node
            )


class CompatFieldMixin:
    """
    Compatibility workaround for Sphinx versions prior to 4.1.0.

    Older sphinx versions do not use the domain's XRefRole for parsing
    and formatting cross-references, so we need to perform this magick
    ourselves to avoid needing to write the parser/formatter in two
    separate places.

    This workaround isn't brick-for-brick compatible with modern Sphinx
    versions, because we do not have access to the parent directive's
    state during this parsing like we do in more modern versions.

    It's no worse than what pre-Sphinx 4.1.0 does, so... oh well!
    """

    def make_xref(
        self,
        rolename: str,
        domain: str,
        target: str,
        innernode: Type[TextlikeNode] = addnodes.literal_emphasis,
        contnode: Optional[Node] = None,
        env: Optional[BuildEnvironment] = None,
        *args: Any,
        **kwargs: Any,
    ) -> Node:
        print("Using compat make_xref")

        assert env
        if not rolename:
            return contnode or innernode(target, target)

        # Get the role instance, but don't *execute it* - we lack the
        # correct state to do so. Instead, we'll just use its public
        # methods to do our reference formatting, and emulate the rest.
        role = env.get_domain(domain).roles[rolename]
        assert isinstance(role, XRefRole)

        # XRefRole features not supported by this compatibility shim;
        # these were not supported in Sphinx 3.x either, so nothing of
        # value is really lost.
        assert not target.startswith("!")
        assert not re.match(ReferenceRole.explicit_title_re, target)
        assert not role.lowercase
        assert not role.fix_parens

        # Code below based mostly on sphinx.roles.XRefRole; run() and
        # create_xref_node()
        options = {
            "refdoc": env.docname,
            "refdomain": domain,
            "reftype": rolename,
            "refexplicit": False,
            "refwarn": role.warn_dangling,
        }
        refnode = role.nodeclass(target, **options)
        title, target = role.process_link(env, refnode, False, target, target)
        refnode["reftarget"] = target
        classes = ["xref", domain, f"{domain}-{rolename}"]
        refnode += role.innernodeclass(target, title, classes=classes)
        result_nodes, messages = role.result_nodes(
            None,  # FIXME - normally self.inliner.document ...
            env,
            refnode,
            is_ref=True,
        )
        return nodes.inline(target, "", *result_nodes)


class CompatField(CompatFieldMixin, docfields.Field):
    pass


class CompatGroupedField(CompatFieldMixin, docfields.GroupedField):
    pass


class CompatTypedField(CompatFieldMixin, docfields.TypedField):
    pass


if not MAKE_XREF_WORKAROUND:
    Field = docfields.Field
    GroupedField = docfields.GroupedField
    TypedField = docfields.TypedField
else:
    Field = CompatField
    GroupedField = CompatGroupedField
    TypedField = CompatTypedField
