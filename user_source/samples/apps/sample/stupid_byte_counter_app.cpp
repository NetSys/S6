/**
 * This is Justine's silly test application to try to understand the code.
 **/

#include "dist.hh"

#include "counter.hh"
#include "ip_key.hh"
#include "stub.counter.hh"

// 50KB because this is a toy app, should be like 5GB in 'real' ISP
#define MAX_USER_BYTES 5000

extern MwMap<IPKey, Counter> g_user_byte_count;

static int packet_processing(struct rte_mbuf* mbuf) {
  // Some byte counter per user
  // get size of packet
  // increase byte counter
  // if bytes >= 5GB, drop packet.
  IPKey* ipkey = IPKey::create_key_src(mbuf);

  // So is constructor automatically called when this gets accessed?
  MwRef<Counter> ctr = g_user_byte_count.get(ipkey);
  if (ctr->get() >= MAX_USER_BYTES) {
    // drop packet
    std::cout << "[JMS] TOO MANY BYTES " << *ipkey << " " << mbuf->buf_len
              << " " << ctr->get() << std::endl;
  } else {
    std::cout << "[JMS] ADDING " << *ipkey << " " << mbuf->buf_len << " "
              << ctr->get() << std::endl;
    ctr->inc(mbuf->buf_len);
  }

  return -1;
}

Application* create_application() {
  Application* app = new Application();
  app->set_packet_func(packet_processing);

  // g_user_byte_count.set_locality_hashing();
  return app;
}
