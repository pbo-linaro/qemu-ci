#!/bin/sh -e
# SPDX-License-Identifier: LGPL-2.1-or-later

python3 -m flake8 ../scripts/qapi/ \
        ../docs/sphinx/qapidoc.py \
        ../docs/sphinx/qapi_domain.py
