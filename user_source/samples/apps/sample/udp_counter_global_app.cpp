/* Per UDP Flow byte counter*/

#include "dist.hh"

#include "stub.udp_counter.hh"
#include "udp_counter.hh"
#include "udp_key.hh"

extern SwMap<UDPKey, UDPCounter> g_udp_counter_map;

static int packet_processing(struct rte_mbuf* mbuf) {
  UDPKey* key = UDPKey::create_key(mbuf);
  if (key) {
    // So is constructor automatically called when this gets accessed?
    SwRef<UDPCounter> counter = g_udp_counter_map.get(key);
    counter->inc_pkt_cnt();
    counter->add_bytes(mbuf->buf_len);
  }
  return 0;  // passing all traffic to next hop (whatever)
}

static void background() {
  DEBUG_APP("============================================");
  DEBUG_APP("Running Background Function");
  DEBUG_APP("============================================");
}

Application* create_application() {
  Application* app = new Application();
  app->set_packet_func(packet_processing);
  app->set_background_func(background);

  //		g_udp_counter_map.set_locality_load_balanced();
  return app;
}
