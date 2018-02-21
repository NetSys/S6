#include "dist.hh"

#include "counter.hh"
#include "ip_key.hh"
#include "stub.counter.hh"
#include "subnet_key.hh"
#include "udp_key.hh"

#define MAX_USER_BYTES 5000

extern MwMap<SubnetKey, Counter> g_subnet_byte_count;  // shared 255
extern MwMap<IPKey, Counter> g_source_byte_count;      // shared 64K

static int packet_processing(struct rte_mbuf* mbuf) {
  SubnetKey* subnet_key = SubnetKey::create_key_src(mbuf, 30);
  MwRef<Counter> subnet_counter = g_subnet_byte_count.get(subnet_key);
  subnet_counter->inc(64);
  delete subnet_key;
#if 0
	IPKey* src_ipkey = IPKey::create_key_dst(mbuf);
	MwRef<Counter> src_counter = g_source_byte_count.create(src_ipkey); 
	src_counter->inc(1);
	delete src_ipkey;
#endif
  return -1;
}

Application* create_application() {
  Application* app = new Application();
  app->set_packet_func(packet_processing);

  /*
     g_subnet_byte_count.set_locality_hashing();	// shared 255
  //		g_source_byte_count.set_locality_hashing();	// shared 64K
  */
  return app;
}
