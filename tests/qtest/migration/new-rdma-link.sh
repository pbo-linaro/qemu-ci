#!/bin/bash

# Copied from blktests
get_ipv4_addr() {
	ip -4 -o addr show dev "$1" |
		sed -n 's/.*[[:blank:]]inet[[:blank:]]*\([^[:blank:]/]*\).*/\1/p'
}

has_soft_rdma() {
	rdma link | grep -q " netdev $1[[:blank:]]*\$"
}

start_soft_rdma() {
	local type

	modprobe rdma_rxe || return $?
	type=rxe
	(
		cd /sys/class/net &&
			for i in *; do
				[ -e "$i" ] || continue
				[ "$i" = "lo" ] && continue
				[ "$(<"$i/addr_len")" = 6 ] || continue
				[ "$(<"$i/carrier")" = 1 ] || continue
				has_soft_rdma "$i" && break
				rdma link add "${i}_$type" type $type netdev "$i" && break
			done
		has_soft_rdma "$i" && echo $i
	)

}

rxe_link=$(start_soft_rdma)
[[ "$rxe_link" ]] && get_ipv4_addr $rxe_link
