#include <iostream>
#include <thread>

#include "dist.hh"

#include "counter.hh"
#include "flow_key.hh"
#include "stub.counter.hh"
#include "stub.tcp_flow.hh"
#include "tcp_flow.hh"

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20
#define TCP_FLAG_ECE 0x40
#define TCP_FALG_CWR 0x80

extern SwMap<FlowKey, TCPFlow> g_tcp_flow_map;

static int packet_processing(struct rte_mbuf *mbuf) {
  FlowKey *fkey = FlowKey::create_key(mbuf);

  // Migrationalble state management
  SwRef<TCPFlow> flow = g_tcp_flow_map.lookup(fkey);
  if (!flow) {
    RefState state;
    flow = g_tcp_flow_map.create(fkey, state);

    if (state.created) {
#if 1  // With TCP packet generator
      struct ipv4_hdr *iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr *,
                                                     sizeof(struct ether_hdr));
      struct tcp_hdr *tcph =
          (struct tcp_hdr *)((u_char *)iph +
                             ((iph->version_ihl & IPV4_HDR_IHL_MASK) << 2));
      if ((tcph->tcp_flags & TCP_FLAG_SYN) &&
          (tcph->tcp_flags & TCP_FLAG_ACK)) {
        flow->init_s2c(mbuf);
      } else if (tcph->tcp_flags & TCP_FLAG_SYN) {
        flow->init_c2s(mbuf);
      }
#else
      if (!flow->is_set()) {
        flow->init_c2s(packet);
        flow->init_s2c(packet);
      }
#endif
    }
  }

  flow->update_context(mbuf);

  return -1;
};

Application *create_application() {
  Application *app = new Application();
  app->set_packet_func(packet_processing);
  /*
        g_tcp_flow_map.set_locality_load_balanced();
        */
  return app;
};
