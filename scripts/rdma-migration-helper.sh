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

command -v rdma >/dev/null || {
    echo "Command 'rdma' is not available, please install it first." >&2
    exit 1
}

if [ "$operation" == "setup" ] || [ "$operation" == "detect" ]; then
    rdma_rxe_setup_detect
elif [ "$operation" == "clean" ]; then
    modprobe -r rdma_rxe
else
    echo "Usage: $0 [setup | detect | clean]"
fi
