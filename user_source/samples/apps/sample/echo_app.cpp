#include "dist.hh"

int packet_processing(struct rte_mbuf *mbuf) {
  static uint64_t pkts = 0;
  pkts++;

  return 1;  // forward
};

Application *create_application() {
  Application *app = new Application();
  app->set_packet_func(packet_processing);
  return app;
};
