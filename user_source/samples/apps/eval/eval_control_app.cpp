/* Per UDP Flow byte counter*/

#include "dist.hh"

#include "ids_config.hh"
#include "ids_stat.hh"
#include "sha1_key.hh"
#include "stub.ids_config.hh"
#include "stub.ids_stat.hh"

/*
 * Microbenchmark tests for Control objects
 *
 */

// XXX Currently we need a different map to access differnt class of objects,
// even if there is 'a single instance of object' of that class :(
extern SwMap<SHA1Key, IDSConfig> g_ids_config;
extern MwMap<SHA1Key, IDSStat> g_ids_stat;

SHA1Key *g_key = new SHA1Key("EvalControlAccess");

int packet_processing(struct rte_mbuf *mbuf) {
  const SwRef<IDSConfig> config = g_ids_config.lookup_const(g_key);
  if (!config)
    return 0;

  MwRef<IDSStat> stat = g_ids_stat.get(g_key);
  if (config->is_pass(mbuf))
    stat->inc_pass(1);
  else
    stat->inc_fail(1);

  return 0;  // passing all traffic to next hop - how to implement?
}

// To do - getting parameters
void update_config() {
  DEBUG_APP("==== Update configurations  ====");

  SwRef<IDSConfig> config = g_ids_config.lookup(g_key);
  config->update();

  DEBUG_APP("=================================");
}

// To do - init/deinit function
Application *create_application() {
  Application *app = new Application();
  app->set_packet_func(packet_processing);
  app->set_background_func(update_config);

  /*
     g_ids_config.set_locality_static(RPC_WORKER_ID);
     g_ids_stat.set_locality_static(RPC_WORKER_ID);
     */

  return app;
}
