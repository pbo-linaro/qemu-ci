#!/bin/bash

# Copied from blktests
get_ipv4_addr()
{
    ip -4 -o addr show dev "$1" |
        sed -n 's/.*[[:blank:]]inet[[:blank:]]*\([^[:blank:]/]*\).*/\1/p' |
        tr -d '\n'
}

has_soft_rdma()
{
    rdma link | grep -q " netdev $1[[:blank:]]*\$"
}

rdma_rxe_setup_detect()
{
    (
        cd /sys/class/net &&
            for i in *; do
                [ -e "$i" ] || continue
                [ "$i" = "lo" ] && continue
                [ "$(<"$i/addr_len")" = 6 ] || continue
                [ "$(<"$i/carrier")" = 1 ] || continue

                has_soft_rdma "$i" && break
                [ "$operation" = "setup" ] &&
                    rdma link add "${i}_rxe" type rxe netdev "$i" && break
            done
        has_soft_rdma "$i" || return
        get_ipv4_addr "$i"
    )
}

operation=${1:-setup}

if [ "$operation" == "setup" ] || [ "$operation" == "detect" ]; then
    rdma_rxe_setup_detect
else
    echo "Usage: $0 [setup | detect]"
fi
