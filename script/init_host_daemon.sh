#! /bin/bash

BESS_HOME="${BESS_HOME:-$HOME/bess}"
BESS_CTL=$BESS_HOME/bin/bessctl

S6_HOME="${S6_HOME:-$HOME/S6}"

OPEN_BESS_CONFIG=$S6_HOME/s6ctl/bess_config/udpgen_multi_source.bess

if [ $# -eq 1 ] ; then
	OPEN_BESS_CONFIG=$1
  echo "RUN bessconfig: $OPEN_BESS_CONFIG"
fi

$BESS_CTL daemon reset 2> /dev/null
RET=$?

if  [ ! $RET -eq 0 ] ; then
	$BESS_CTL daemon start
fi

$BESS_CTL run file $OPEN_BESS_CONFIG
