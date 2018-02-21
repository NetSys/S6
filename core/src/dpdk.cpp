#include "dpdk.hh"

#include <assert.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include "log.hh"

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

#define RX_RING_SIZE 128
#define TX_RING_SIZE 512

static const struct rte_eth_conf port_conf_default() {
  struct rte_eth_conf ret = rte_eth_conf();
  ret.rxmode.max_rx_pkt_len = ETHER_MAX_LEN;
  return ret;
}

static inline int init_port(uint8_t port, struct rte_mempool *mbuf_pool) {
  struct rte_eth_conf port_conf = port_conf_default();
  const uint16_t rx_rings = 1;
  const uint16_t tx_rings = 1;
  int retval;
  uint16_t q;

  /* Configrue the Ethernet device. */
  retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
  if (retval != 0)
    return retval;

  /* Allocate and set up 1 TX queue per Ethernet port. */
  for (q = 0; q < tx_rings; q++) {
    retval = rte_eth_tx_queue_setup(port, q, TX_RING_SIZE,
                                    rte_eth_dev_socket_id(port), NULL);
    if (retval < 0)
      return retval;
  }

  /* Allocate and set up 1 RX queue per Ethernet port. */
  for (q = 0; q < rx_rings; q++) {
    retval = rte_eth_rx_queue_setup(
        port, q, RX_RING_SIZE, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval < 0)
      return retval;
  }

  /* Start the Ethernet port. */
  retval = rte_eth_dev_start(port);
  if (retval < 0)
    return retval;

  /* Display the port MAC address. */
  struct ether_addr addr;
  rte_eth_macaddr_get(port, &addr);
  printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8
         " %02" PRIx8 " %02" PRIx8 "\n",
         (unsigned)port, addr.addr_bytes[0], addr.addr_bytes[1],
         addr.addr_bytes[2], addr.addr_bytes[3], addr.addr_bytes[4],
         addr.addr_bytes[5]);

  /* Enable RX in promiscuous mode for the Ethernet device. */
  rte_eth_promiscuous_enable(port);

  return 0;
}

/* Creates a new mempool in memory to hold the mbufs. */
static struct rte_mempool *init_mempool(unsigned mbuf_count) {
  struct rte_mempool *mbuf_pool;
  mbuf_pool =
      rte_pktmbuf_pool_create("MBUF_POOL", mbuf_count, MBUF_CACHE_SIZE, 0,
                              RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
  if (mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

  return mbuf_pool;
}

// void init_dpdk(const std::string &prog_name) {
void init_dpdk(int rte_argc, const char **rte_argv) {
  unsigned nb_ports;
  uint8_t portid;
  struct rte_mempool *mbuf_pool;

  assert(rte_argc <= MAX_RTE_ARGV);
  int ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
  if (ret < 0) {
    DEBUG_ERR("rte_eal_init() failed: ret = " << ret);
    exit(EXIT_FAILURE);
  }

  /* Check the number of ports. */
  nb_ports = rte_eth_dev_count();
  if (nb_ports < 1)
    rte_exit(EXIT_FAILURE, "Error: There are no available port");

  mbuf_pool = init_mempool(NUM_MBUFS * nb_ports);

  for (portid = 0; portid < nb_ports; portid++)
    if (init_port(portid, mbuf_pool) != 0)
      rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu8 "\n", portid);
}

uint16_t dpdk_send_pkt(struct rte_mbuf *bufs) {
  int pid = 0, qid = 0; /*FIXME: Need to be fixed */
  const uint16_t tx = rte_eth_tx_burst(pid, qid, &bufs, 1);
  return tx;
}

uint16_t dpdk_send_pkts(struct rte_mbuf **bufs, const uint16_t buf_size) {
  int pid = 0, qid = 0; /*FIXME: Need to be fixed */
  const uint16_t tx = rte_eth_tx_burst(pid, qid, bufs, buf_size);
  return tx;
}

uint16_t dpdk_receive_pkts(struct rte_mbuf **bufs, const uint16_t batch_size) {
  int pid = 0, qid = 0; /*FIXME: Need to be fixed */
  const uint16_t rx = rte_eth_rx_burst(pid, qid, bufs, batch_size);
  return rx;
}
