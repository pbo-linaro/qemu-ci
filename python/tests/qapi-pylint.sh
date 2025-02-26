#!/bin/sh -e
SETUPTOOLS_USE_DISTUTILS=stdlib python3 -m pylint \
                                --rcfile=../scripts/qapi/pylintrc \
                                ../scripts/qapi/
