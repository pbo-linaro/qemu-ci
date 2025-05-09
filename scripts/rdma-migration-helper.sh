#!/bin/bash

# Copied from blktests
get_ipv4_addr()
{
    ip -4 -o addr show dev "$1" |
        sed -n 's/.*[[:blank:]]inet[[:blank:]]*\([^[:blank:]/]*\).*/\1/p' |
        head -1 | tr -d '\n'
}

get_ipv6_addr() {
    ipv6=$(ip -6 -o addr show dev "$1" |
        sed -n 's/.*[[:blank:]]inet6[[:blank:]]*\([^[:blank:]/]*\).*/\1/p' |
        head -1 | tr -d '\n')

    [ $? -eq 0 ] || return
    echo -n "[$ipv6%$1]"
}

# existing rdma interfaces
rdma_interfaces()
{
    rdma link show | sed -nE 's/^link .* netdev ([^ ]+).*$/\1 /p'
}

# existing valid ipv4 interfaces
ipv4_interfaces()
{
    ip -o addr show | awk '/inet / {print $2}' | grep -v -w lo
}

ipv6_interfaces()
{
    ip -o addr show | awk '/inet6 / {print $2}' | sort -u | grep -v -w lo
}

rdma_rxe_detect()
{
    for r in $(rdma_interfaces)
    do
        "$IP_FAMILY"_interfaces | grep -qw $r && get_"$IP_FAMILY"_addr $r && return
    done

    return 1
}

rdma_rxe_setup()
{
    for i in $("$IP_FAMILY"_interfaces)
    do
        rdma_interfaces | grep -qw $i && continue
        rdma link add "${i}_rxe" type rxe netdev "$i" && {
            echo "Setup new rdma/rxe ${i}_rxe for $i with $(get_"$IP_FAMILY"_addr $i)"
            return
        }
    done

    echo "Failed to setup any new rdma/rxe link" >&2
    return 1
}

rdma_rxe_clean()
{
    modprobe -r rdma_rxe
}

IP_FAMILY=${IP_FAMILY:-ipv4}
if [ "$IP_FAMILY" != "ipv6" ] && [ "$IP_FAMILY" != "ipv4" ]; then
    echo "Unknown ip family '$IP_FAMILY', only ipv4 or ipv6 is supported." >&2
    exit 1
fi

operation=${1:-detect}

command -v rdma >/dev/null || {
    echo "Command 'rdma' is not available, please install it first." >&2
    exit 1
}

if [ "$operation" == "setup" ] || [ "$operation" == "clean" ]; then
    [ "$UID" == 0 ] || {
        echo "Root privilege is required to setup/clean a rdma/rxe link" >&2
        exit 1
    }
    rdma_rxe_"$operation"
elif [ "$operation" == "detect" ]; then
    rdma_rxe_detect
else
    echo "Usage: $0 [setup | detect | clean]"
fi
