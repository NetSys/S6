/* Per UDP Flow byte counter*/

#include "dist.hh"

#include "counter.hh"
#include "ip_key.hh"
#include "stub.counter.hh"

extern MwMap<IPKey, Counter> g_srcip_byte_count;

static int packet_processing(struct rte_mbuf* mbuf) {
  IPKey* key = IPKey::create_key_dst(mbuf);
  if (key) {
    // So is constructor automatically called when this gets accessed?
    MwRef<Counter> counter = g_srcip_byte_count.get(key);
    counter->inc_and_get(1);
  }
  delete key;

  return 0;  // passing all traffic to next hop (whatever)
}

void report_counter_list() {
  DEBUG_APP("=================================");
  DEBUG_APP("=== Create ip counter report ===");
  DEBUG_APP("=================================");
}

Application* create_application() {
  Application* app = new Application();
  app->set_packet_func(packet_processing);
  app->set_background_func(report_counter_list);

  // g_srcip_byte_count.set_locality_hashing();
  // g_srcip_byte_count.set_locality_static(RPC_WORKER_ID);
  return app;
}
