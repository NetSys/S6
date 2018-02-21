#include "dist.hh"

#include "asset_context.hh"
#include "ip_key.hh"
#include "stub.asset_context.hh"

extern MwMap<IPKey, S6AssetContext> g_src_asset_map;
extern MwMap<IPKey, S6AssetContext> g_dst_asset_map;

static S6Asset* find_asset(struct rte_mbuf* mbuf) {
  S6Asset* asset = new S6Asset();

  asset->type |= _AT_OS_FLAG;
  asset->os_id = random() % OS_CNT;

  asset->type |= _AT_SERVICE_FLAG;
  asset->service_id = random() % SERVICE_CNT;

  return asset;
};

static void remove_asset(S6Asset* asset) {
  delete asset;
};

static int packet_processing(struct rte_mbuf* mbuf) {
  S6Asset* new_asset = find_asset(mbuf);
  if (!new_asset)
    return -1;

  IPKey* src_ipkey = IPKey::create_key_src(mbuf);
  IPKey* dst_ipkey = IPKey::create_key_dst(mbuf);

  // Stationary state management
  MwRef<S6AssetContext> src_asset_list = g_src_asset_map.get(src_ipkey);
  src_asset_list->update(*new_asset);

  MwRef<S6AssetContext> dst_asset_list = g_dst_asset_map.get(dst_ipkey);
  dst_asset_list->update(*new_asset);

  // XXX Solving initialization overhead
  // if (!asset_list->get_ip())
  //	asset_list->init(ipkey->get_ip());

  remove_asset(new_asset);

  return -1;
};

static void report_asset_list() {
  MwIter<S6AssetContext>* iter = g_dst_asset_map.get_local_iterator();
  if (!iter) {
    DEBUG_ERR("Cannot create iterator for g_asset_map");
    return;
  }

  DEBUG_APP("=== Create asset list report");
  while (iter->next()) {
    const IPKey* ipkey = dynamic_cast<const IPKey*>(iter->key);
    const MwRef<S6AssetContext> asset_list = *(iter->value);
    asset_list->print();
  }

  DEBUG_APP("===========================");

  g_dst_asset_map.release_local_iterator(iter);
  return;
};

Application* create_application() {
  Application* app = new Application();
  app->set_packet_func(packet_processing);
  app->set_background_func(report_asset_list);

  /*
  //g_asset_map.set_locality_static(RPC_WORKER_ID);
  g_src_asset_map.set_locality_load_balanced();
  g_dst_asset_map.set_locality_hashing();
  */
  return app;
};
