#!/usr/bin/env bash

set -euo pipefail

# avocado will keep in cache incomplete images, and there is no way to force it
# to redownload it... So we do this checking ourselves here.

if [ ! -d $HOME/avocado ]; then
    echo "missing $HOME/avocado"
    exit 1
fi

err=0
for expected in $(find $HOME/avocado -type f | grep CHECKSUM | sort); do
    file=$(echo $expected | sed -e 's/-CHECKSUM//')
    hash_type=$(cat $expected | cut -f 1 -d ' ')
    if [ ! -f "$file" ]; then
        echo $file is missing
        err=1
        continue
    fi
    hash=$(${hash_type}sum $file | cut -f 1 -d ' ')
    if ! diff <(cat $expected) <(echo $hash_type $hash); then
        echo $file has hash mismatch - delete
        rm -f $file
        err=1
    fi
done
exit $err
