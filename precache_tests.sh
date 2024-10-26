#!/usr/bin/env bash

set -euo pipefail

./configure
ninja -C build precache-functional

# avocado fetch assets will sometimes fail with exception, but without any error
# code. In this case, either an item is missing, or it's in the cache but with a
# bad hash. In the second case, avocado does not redownload it... :(
while true; do
    make -C build check-avocado SPEED=slow |& tee avocado.log
    echo -----------------------------------------
    if ! ./check_avocado_cache.sh; then
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
