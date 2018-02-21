#! /bin/bash

BESS_HOME="${BESS_HOME:-$HOME/bess}"
BESS_CTL=$BESS_HOME/bin/bessctl

$BESS_CTL daemon reset 2> /dev/null
