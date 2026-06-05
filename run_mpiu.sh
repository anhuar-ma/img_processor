mpirun --prtemca oob_tcp_if_include tailscale0 --mca btl_tcp_if_include tailscale0 --mca btl_tcp_disable_family 6 --hostfile machinefile execute.sh --tranforms vc
