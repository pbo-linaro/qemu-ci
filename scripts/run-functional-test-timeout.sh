#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

script_dir=$(dirname $(readlink -f $0))
run=$script_dir/run-functional-test.sh

# default to 20 minutes by default
QEMU_TEST_TIMEOUT=${QEMU_TEST_TIMEOUT:-1200}

try()
{
    exit_code=0
    timeout --foreground -k 1 $QEMU_TEST_TIMEOUT $run "$@" || exit_code=$?
    if [ $exit_code == 124 ] || [ $exit_code == 137 ]; then
        # timeout
        return
    fi
    exit $exit_code
}

# retry 3 times
try "$@"
try "$@"
try "$@"

echo "test timeout after 3 tries" 1>&2
exit 1
