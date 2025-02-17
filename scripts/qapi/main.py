# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.

"""
QAPI Generator

This is the main entry point for generating C code from the QAPI schema.
"""

import argparse
from importlib import import_module
import sys

from .error import QAPIError
from .backend import invalid_prefix_char


def import_class_from_string(path):
    module_path, _, class_name = path.rpartition('.')
    mod = import_module(module_path)
    klass = getattr(mod, class_name)
    return klass


def main() -> int:
    """
    gapi-gen executable entry point.
    Expects arguments via sys.argv, see --help for details.

    :return: int, 0 on success, 1 on failure.
    """
    parser = argparse.ArgumentParser(
        description='Generate code from a QAPI schema')
    parser.add_argument('-b', '--builtins', action='store_true',
                        help="generate code for built-in types")
    parser.add_argument('-o', '--output-dir', action='store',
                        default='',
                        help="write output to directory OUTPUT_DIR")
    parser.add_argument('-p', '--prefix', action='store',
                        default='',
                        help="prefix for symbols")
    parser.add_argument('-u', '--unmask-non-abi-names', action='store_true',
                        dest='unmask',
                        help="expose non-ABI names in introspection")
    parser.add_argument('-k', '--backend', default="qapi.backend.QAPICBackend",
                        help="Python module name for code generator")

    # Option --suppress-tracing exists so we can avoid solving build system
    # problems.  TODO Drop it when we no longer need it.
    parser.add_argument('--suppress-tracing', action='store_true',
                        help="suppress adding trace events to qmp marshals")

    parser.add_argument('schema', action='store')
    args = parser.parse_args()

    funny_char = invalid_prefix_char(args.prefix)
    if funny_char:
        msg = f"funny character '{funny_char}' in argument of --prefix"
        print(f"{sys.argv[0]}: {msg}", file=sys.stderr)
        return 1

    backendclass = import_class_from_string(args.backend)
    try:
        backend = backendclass()
        backend.run(args.schema,
                    output_dir=args.output_dir,
                    prefix=args.prefix,
                    unmask=args.unmask,
                    builtins=args.builtins,
                    gen_tracing=not args.suppress_tracing)
    except QAPIError as err:
        print(err, file=sys.stderr)
        return 1
    return 0
