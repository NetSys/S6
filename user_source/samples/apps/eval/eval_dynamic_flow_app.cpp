/* Per Flow bye counter */
#include <rte_config.h>
#include <rte_ip.h>
#include <rte_tcp.h>

#include "dist.hh"

#include "ip_key.hh"
#include "stub.udp_counter.hh"
#include "stub.udp_counter_128B.hh"
#include "stub.udp_counter_16KB.hh"
#include "stub.udp_counter_1KB.hh"
#include "stub.udp_counter_256B.hh"
#include "stub.udp_counter_2KB.hh"
#include "stub.udp_counter_32KB.hh"
#include "stub.udp_counter_4KB.hh"
#include "stub.udp_counter_512B.hh"
#include "stub.udp_counter_64B.hh"
#include "stub.udp_counter_8KB.hh"

/*
 * Microbenchmark tests for per-flow objects
 *
 * i) Per-flow object scalability
 * ii) Dynamic migration
 *
 */

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20
#define TCP_FLAG_ECE 0x40
#define TCP_FALG_CWR 0x80

// Per-flow objects (Single writer object accessed by per-destiniation ip)
extern SwMap<IPKey, UDPCounter> g_sw_counter_map;
extern SwMap<IPKey, UDPCounter64B> g_sw_counter_64b_map;
extern SwMap<IPKey, UDPCounter128B> g_sw_counter_128b_map;
extern SwMap<IPKey, UDPCounter256B> g_sw_counter_256b_map;
extern SwMap<IPKey, UDPCounter512B> g_sw_counter_512b_map;
extern SwMap<IPKey, UDPCounter1KB> g_sw_counter_1kb_map;
extern SwMap<IPKey, UDPCounter2KB> g_sw_counter_2kb_map;
extern SwMap<IPKey, UDPCounter4KB> g_sw_counter_4kb_map;
extern SwMap<IPKey, UDPCounter8KB> g_sw_counter_8kb_map;
extern SwMap<IPKey, UDPCounter16KB> g_sw_counter_16kb_map;
extern SwMap<IPKey, UDPCounter32KB> g_sw_counter_32kb_map;

static int obj_size = 32;

static int init(int param) {
  obj_size = param;
  return 0;
}

static int packet_processing(struct rte_mbuf *mbuf) {
  RefState state;

  struct ipv4_hdr *iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr *,
                                                 sizeof(struct ether_hdr));

  struct tcp_hdr *tcph =
      (struct tcp_hdr *)((u_char *)iph +
                         ((iph->version_ihl & IPV4_HDR_IHL_MASK) << 2));

  IPKey key(ntohl(iph->dst_addr), _LCAN_DST);

  switch (obj_size) {
    case 32: {
      SwRef<UDPCounter> counter;
      counter = g_sw_counter_map.create(&key, state);

      counter->inc_pkt_cnt();

      if (tcph->tcp_flags & TCP_FLAG_FIN)
        g_sw_counter_map.remove(counter);
    } break;
    case 64: {
      SwRef<UDPCounter64B> counter;
      counter = g_sw_counter_64b_map.create(&key, state);

      counter->inc_pkt_cnt();

      if (tcph->tcp_flags & TCP_FLAG_FIN)
        g_sw_counter_64b_map.remove(counter);
    } break;
    case 128: {
      SwRef<UDPCounter128B> counter;

      counter = g_sw_counter_128b_map.create(&key, state);

      counter->inc_pkt_cnt();

      if (tcph->tcp_flags & TCP_FLAG_FIN)
        g_sw_counter_128b_map.remove(counter);
    } break;
    case 256: {
      SwRef<UDPCounter256B> counter;
      counter = g_sw_counter_256b_map.create(&key, state);

      counter->inc_pkt_cnt();

      if (tcph->tcp_flags & TCP_FLAG_FIN)
        g_sw_counter_256b_map.remove(counter);
    } break;
    case 512: {
      SwRef<UDPCounter512B> counter;

      counter = g_sw_counter_512b_map.create(&key, state);

      counter->inc_pkt_cnt();

      if (tcph->tcp_flags & TCP_FLAG_FIN)
        g_sw_counter_512b_map.remove(counter);
    } break;
    case 1024: {
      SwRef<UDPCounter1KB> counter;

      counter = g_sw_counter_1kb_map.create(&key, state);

      counter->inc_pkt_cnt();

      if (tcph->tcp_flags & TCP_FLAG_FIN)
        g_sw_counter_1kb_map.remove(counter);
    } break;
    case 2048: {
      SwRef<UDPCounter2KB> counter;

      counter = g_sw_counter_2kb_map.create(&key, state);

      counter->inc_pkt_cnt();

      if (tcph->tcp_flags & TCP_FLAG_FIN)
        g_sw_counter_2kb_map.remove(counter);
    } break;
    case 4096: {
      SwRef<UDPCounter4KB> counter;

      counter = g_sw_counter_4kb_map.create(&key, state);

      counter->inc_pkt_cnt();

      if (tcph->tcp_flags & TCP_FLAG_FIN)
        g_sw_counter_4kb_map.remove(counter);
    } break;
    case 8192: {
      SwRef<UDPCounter8KB> counter;

      counter = g_sw_counter_8kb_map.create(&key, state);

      counter->inc_pkt_cnt();

      if (tcph->tcp_flags & TCP_FLAG_FIN)
        g_sw_counter_8kb_map.remove(counter);
    } break;
    case 16384: {
      SwRef<UDPCounter16KB> counter;

      counter = g_sw_counter_16kb_map.create(&key, state);

      counter->inc_pkt_cnt();

      if (tcph->tcp_flags & TCP_FLAG_FIN)
        g_sw_counter_16kb_map.remove(counter);
    } break;
    case 32768: {
      SwRef<UDPCounter32KB> counter;

      counter = g_sw_counter_32kb_map.create(&key, state);

      counter->inc_pkt_cnt();

      if (tcph->tcp_flags & TCP_FLAG_FIN)
        g_sw_counter_32kb_map.remove(counter);
    } break;
    default:
      DEBUG_ERR("No known object_size for serving this" << obj_size);
      exit(EXIT_FAILURE);
  }

  return 0;  // passing all traffic to next hop (whatever)
}

Application *create_application() {
  Application *app = new Application();
  app->set_init_func(init);
  app->set_packet_func(packet_processing);
  return app;
}
