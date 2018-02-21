#! /bin/bash
set -e

center_ens4="172.31.33.140"
mcgee_ens4="172.31.41.148"
soda_ens4="172.31.41.77"
hodoo_ens4="172.31.41.61"

S6_HOME="${BESS_HOME:-$HOME/S6}"

# Protect private key
chmod 600 ${S6_HOME}/s6ctl/host_config/id_rsa

# Enable state-channel connectivity
sudo dhclient -v ens4
sudo route add -host ${center_ens4} dev ens4
sudo route add -host ${mcgee_ens4} dev ens4
sudo route add -host ${soda_ens4} dev ens4
sudo route add -host ${hodoo_ens4} dev ens4
