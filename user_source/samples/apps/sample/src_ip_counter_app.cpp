/* Per UDP Flow byte counter*/

#include "dist.hh"

#include "counter.hh"
#include "ip_key.hh"
#include "stub.counter.hh"

extern MwMap<IPKey, Counter> g_srcip_byte_count;

static int packet_processing(struct rte_mbuf* mbuf) {
  IPKey* key = IPKey::create_key_src(mbuf);
  if (key) {
    // So is constructor automatically called when this gets accessed?
    MwRef<Counter> counter = g_srcip_byte_count.get(key);
#if 1
    // normal rpc -> go and wait to get result
    counter->inc_and_get(1);
#endif

#if 0
		// write_behind
		counter->inc(1);
#endif

#if 0
		// stale_cache
		int c = counter->get();
		(void) c;
#endif

#if 0
		// write_behind + stale_cache
		counter->inc(1);
		int c = counter->get();
		(void) c;
#endif
  }
  delete key;

  return 0;  // passing all traffic to next hop (whatever)
}

static void report_counter_list() {
  MwIter<Counter>* iter = g_srcip_byte_count.get_local_iterator();
  if (!iter) {
    DEBUG_ERR("Cannot create iterator for g_udp_counter_map");
    return;
  }

  DEBUG_APP("=== Create ip counter report ===");
  DEBUG_APP("IPKey\tCounter");
  while (iter->next()) {
    const IPKey* ipkey = dynamic_cast<const IPKey*>(iter->key);
    const MwRef<Counter> counter = *(iter->value);
    int count = counter->get();
    DEBUG_APP(*ipkey << "\t" << count);
  }
  DEBUG_APP("=================================");

  g_srcip_byte_count.release_local_iterator(iter);
}

Application* create_application() {
  Application* app = new Application();
  app->set_packet_func(packet_processing);
  app->set_background_func(report_counter_list);
  /*
  //g_srcip_byte_count.set_locality_static(RPC_WORKER_ID);
  g_srcip_byte_count.set_locality_hashing();
  */
  return app;
}
