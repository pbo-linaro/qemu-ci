#!/usr/bin/env bash

set -euo pipefail

check_avocado_cache()
{
    # avocado will keep in cache incomplete images, and there is no way to force
    # it to redownload it... So we do this checking ourselves here.

    if [ ! -d $HOME/avocado ]; then
        echo "missing $HOME/avocado"
        return 1
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
            rm -rf "$(dirname "$file")"
            err=1
        fi
    done
    return $err
}

./configure
ninja -C build precache-functional -k 0 ||
ninja -C build -j1 precache-functional -k 0

# avocado fetch assets will sometimes fail with exception, but without any error
# code. In this case, either an item is missing, or it's in the cache but with a
# bad hash. In the second case, avocado does not redownload it... :(
while true; do
    make -C build check-avocado SPEED=slow |& tee avocado.log
    echo -----------------------------------------
    if ! check_avocado_cache; then
        echo "avocado cache has missing items"
        continue
    fi
    if grep -A 20 -i Traceback avocado.log; then
        echo "exception while running avocado"
        continue
    fi
    echo "avocado cache is now ready"
    break
done
