/* Per UDP Flow byte counter*/

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
  IPKey key(ntohl(iph->dst_addr), _LCAN_DST);

  switch (obj_size) {
    case 32: {
      SwRef<UDPCounter> counter = g_sw_counter_map.create(&key, state);
      counter->inc_pkt_cnt();
    } break;
    case 64: {
      SwRef<UDPCounter64B> counter = g_sw_counter_64b_map.create(&key, state);
      counter->inc_pkt_cnt();
    } break;
    case 128: {
      SwRef<UDPCounter128B> counter = g_sw_counter_128b_map.create(&key, state);
      counter->inc_pkt_cnt();
    } break;
    case 256: {
      SwRef<UDPCounter256B> counter = g_sw_counter_256b_map.create(&key, state);
      counter->inc_pkt_cnt();
    } break;
    case 512: {
      SwRef<UDPCounter512B> counter = g_sw_counter_512b_map.create(&key, state);
      counter->inc_pkt_cnt();
    } break;
    case 1024: {
      SwRef<UDPCounter1KB> counter = g_sw_counter_1kb_map.create(&key, state);
      counter->inc_pkt_cnt();
    } break;
    case 2048: {
      SwRef<UDPCounter2KB> counter = g_sw_counter_2kb_map.create(&key, state);
      counter->inc_pkt_cnt();
    } break;
    case 4096: {
      SwRef<UDPCounter4KB> counter = g_sw_counter_4kb_map.create(&key, state);
      counter->inc_pkt_cnt();
    } break;
    case 8192: {
      SwRef<UDPCounter8KB> counter = g_sw_counter_8kb_map.create(&key, state);
      counter->inc_pkt_cnt();
    } break;
    case 16384: {
      SwRef<UDPCounter16KB> counter = g_sw_counter_16kb_map.create(&key, state);
      counter->inc_pkt_cnt();
    } break;
    case 32768: {
      SwRef<UDPCounter32KB> counter = g_sw_counter_32kb_map.create(&key, state);
      counter->inc_pkt_cnt();
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

  // Objects are load balanced as like NFV load balancer
  // Mostly stay in locally, unless scaling process happens
  // g_sw_counter_map.set_locality_load_balanced();
  // g_sw_counter_map.set_locality_local();
  return app;
}
