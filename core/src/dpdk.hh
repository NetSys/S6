#ifndef _DISTREF_DPDK_HH_
#define _DISTREF_DPDK_HH_

#include <string>

#define MAX_RTE_ARGV 16

void init_dpdk(const int rte_argc, const char **rte_argv);
uint16_t dpdk_send_pkt(struct rte_mbuf *bufs);
uint16_t dpdk_send_pkts(struct rte_mbuf **bufs, const uint16_t buf_size);
uint16_t dpdk_receive_pkts(struct rte_mbuf **bufs, const uint16_t batch_size);

#endif /* _DISTREF_DPDK_HH_ */
