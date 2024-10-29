#!/usr/bin/env bash

# This script reads qemu plugin header file and find name of functions following
# QEMU_PLUGIN_API keyword.
# Function name has to be on the line following QEMU_PLUGIN_API.
# We check number of functions found match occurences of QEMU_PLUGIN_API.

set -euo pipefail

plugin_header=$1

read_qemu_plugin_symbols()
{
    grep -A 1 '^QEMU_PLUGIN_API' $plugin_header |
    grep qemu_plugin_ |
    sed -e 's/(.*//' -e 's/.*qemu_plugin/qemu_plugin/'
}

if [ "$(grep -c '^QEMU_PLUGIN_API' $plugin_header)" != \
     "$(read_qemu_plugin_symbols | wc -l | sed -e 's/ //g')" ]; then
     echo "mismatch between QEMU_PLUGIN_API occurences and number of symbols"
     exit 1
fi

cat << EOF
{
$(read_qemu_plugin_symbols | sed -e 's/$/;/')
};
EOF
