/* Per UDP Flow byte counter*/

#include "dist.hh"

#include "counter.hh"
#include "stub.counter.hh"
#include "subnet_key.hh"

/*
 * Microbenchmark tests for Control objects
 *
 * i) Blocking updates and read
 * ii) Non-blocking updates
 * iii) Non-blocking reads
 * iv) Non-blocking updates and read
 *
 */

extern MwMap<SubnetKey, Counter> g_subnet_byte_count;

static int subnet = 32;

int init(int param) {
  if (subnet <= 0 || subnet > 32) {
    DEBUG_ERR("subnet should be between 1 and 32");
    return -1;
  }

  subnet = param;
  return 0;
}

int packet_processing(struct rte_mbuf *mbuf) {
  struct ipv4_hdr *iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr *,
                                                 sizeof(struct ether_hdr));
  struct tcp_hdr *tcph =
      (struct tcp_hdr *)((u_char *)iph +
                         ((iph->version_ihl & IPV4_HDR_IHL_MASK) << 2));

  SubnetKey key(ntohl(iph->dst_addr), subnet, _LCAN_DST);

  MwRef<Counter> counter = g_subnet_byte_count.get(&key);

  // stale_cache
  counter->inc(1);

  return 0;  // passing all traffic to next hop (whatever)
}

void report_counter_list() {
  uint64_t total = 0;
  uint64_t keys = 0;

  MwIter<Counter> *iter = g_subnet_byte_count.get_local_iterator();
  if (!iter) {
    DEBUG_ERR("Cannot create iterator for g_subnet_counter_map");
    return;
  }

  DEBUG_APP("=== Create ip counter report ===");
  while (iter->next()) {
    const SubnetKey *subnet_key = dynamic_cast<const SubnetKey *>(iter->key);
    const MwRef<Counter> counter = *(iter->value);
    int count = counter->get();
    total += count;
    keys++;
  }
  DEBUG_APP("=================================");
  DEBUG_APP("Total number of keys: " << keys);
  DEBUG_APP("Total number of ip packets: " << total);
  g_subnet_byte_count.release_local_iterator(iter);
}

Application *create_application() {
  Application *app = new Application();
  app->set_init_func(init);
  app->set_packet_func(packet_processing);
  app->set_background_func(report_counter_list);
  return app;
}
