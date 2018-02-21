#ifndef _DISTREF_MW_STUB_HH_
#define _DISTREF_MW_STUB_HH_

#include <sys/queue.h>
#include <unordered_map>

#include "key.hh"
#include "worker_config.hh"

class MwStubBase;
class MWSkeleton;
class MWObject;
class Worker;
class KeySpace;
class DroutineScheduler;
class ControlBus;
class Connector;
class MemPool;

class RPCRequest;
class MessageBuffer;

struct StrictReturn;
struct CacheReturn;
struct RefState;

#define RPC_MSG_BUF_SIZE 2800
#define MAX_ITERATOR_CNT 10

enum RPC_W_MODE {
  RPC_DEFAULT = 0,
  RPC_BATCHING,
  RPC_ZIPPING,
};

struct RPCBuf {
  uint64_t init_tsc;
  uint32_t offset;
  uint8_t buf[RPC_MSG_BUF_SIZE];
};

typedef std::unordered_map<const Key *, MwStubBase *, _dr_key_hash,
                           _dr_key_equal_to>
    MwStubMap;
typedef std::unordered_map<const Key *, MWSkeleton *, _dr_key_hash,
                           _dr_key_equal_to>
    MWSkeletonMap;

struct MapIterator {
  bool is_valid = false;
  typename MWSkeletonMap::iterator iter;
};

struct MwStubPair {
  const Key *key;
  MwStubBase *ref;
};

struct MWSkeletonPair {
  const Key *key;
  uint32_t obj_size;
  MWSkeleton *obj_ref;
};

class MwStubManager {
  int ADTCnt = _MAX_DMAPS;
  MwStubMap mwstub_map_arr[_MAX_DMAPS];

  MWSkeletonMap mw_skeleton_map_arr[_MAX_DMAPS];
  MapIterator skeleton_iterator[_MAX_DMAPS][MAX_ITERATOR_CNT];

  TAILQ_HEAD(aggr_head, MWSkeleton) aggr_list;

#if 0
	bool skeleton_export_reserve[_MAX_DMAPS];
	bool skeleton_map_lock[_MAX_DMAPS];
#endif
  std::unordered_map<int, StrictReturn *> strict_ret_map;
  std::unordered_map<VKey, CacheReturn *, _dr_vkey_hash, _dr_vkey_equal_to>
      cache_ret_map[_MAX_DMAPS];

  struct RPCBuf rpc_behind_buf[MAX_WORKER_CNT] = {};

  int node_id;
  Worker *worker = nullptr;
  ControlBus *cbus = nullptr;
  DroutineScheduler *scheduler = nullptr;

  KeySpace *key_space = nullptr;
  ;
  MemPool *mp = nullptr;

  struct {
    bool dmz_to_scaling_on = false;
    bool dmz_to_quiescent_on = false;
    bool on = false;
  } scaling;

  MWSkeleton *get_mw_skeleton(int map_id, const Key *key);

  inline bool send_rpc_message(WorkerID to, int map_id, const Key *key,
                               uint32_t flag, uint32_t method_id, void *args,
                               uint32_t args_size);

  CacheReturn *get_cache_return(int map_id, const Key *key, int method_id);
  RPCRequest *get_rpc_behind_message(WorkerID to, int msg_size);

  // might blocking until satisfying wake-up condition
  // by get_*_return() respectively
  inline void request_stale_rpc(WorkerID to, int map_id, const Key *key,
                                uint32_t flag, uint32_t method_id, void *ret,
                                uint32_t ret_size);
  inline void request_behind_rpc(WorkerID to, int map_id, const Key *key,
                                 uint32_t flag, uint32_t method_id, void *args,
                                 uint32_t args_size, uint8_t mode);
  inline void request_strict_rpc(WorkerID to, int map_id, const Key *key,
                                 uint32_t flag, uint32_t method_id, void *args,
                                 uint32_t args_size, void *ret,
                                 uint32_t ret_size);

 public:
  MwStubManager(uint32_t node_id, Worker *worker, DroutineScheduler *sch,
                ControlBus *cbus, KeySpace *key_space, MemPool *mp);

  ~MwStubManager(){};

  void teardown(bool force);

  void set_keyspace(KeySpace *key_space) { this->key_space = key_space; }

  void set_dmz_to_scaling_on() { this->scaling.dmz_to_scaling_on = true; }

  void set_dmz_to_scaling_off() { this->scaling.dmz_to_scaling_on = false; }

  void set_dmz_to_quiescent_on() { this->scaling.dmz_to_quiescent_on = true; }

  void set_dmz_to_quiescent_off() { this->scaling.dmz_to_quiescent_on = false; }

  void set_scaling_on() { this->scaling.on = true; }

  void set_scaling_off() { this->scaling.on = false; }

  int force_scaling(int max_objects);

  MwStubBase *create(int map_id, const Key *key, RefState &state);
  MwStubBase *get(int map_id, const Key *key);
  MwStubBase *lookup(int map_id, const Key *key);
  void release(int map_id, const Key *key);

  void execute_rpc(int map_id, const Key *key, uint32_t flag,
                   uint32_t method_id, void *args, void **ret,
                   uint32_t *ret_size);
  void request_rpc(int map_id, const Key *key, uint32_t flag,
                   uint32_t method_id, void *args, uint32_t args_size,
                   void *ret, uint32_t ret_size);
  void check_to_push_aggregation();
  void aggregate(int map_id, const Key *key, MWObject *obj);

  int create_local_iterator(int map_id);
  const Key *get_local_next_key(int map_id, int itidx);
  MwStubBase *get_local_next_item(int map_id, int itidx);
  MwStubPair get_local_next_pair(int map_id, int itidx);
  void release_local_iterator(int map_id, int itidx);

  // satisfying wake-up condition locally/remotely
  // previously blocked by get_*_return() respectively
  void set_strict_return(int d_idx, uint32_t arg_size, void *data);
  void set_cache_return(int map_id, const Key *key, int method_id,
                        uint32_t arg_size, void *data);
#if 0
	int export_objects(int map_id, uint32_t (*pair_count)[MAX_WORKER_CNT], 
			MWSkeletonPair *(*pairs)[MAX_WORKER_CNT]);
	void import_objects(int map_id, uint32_t key_size, uint32_t obj_count, 
			uint32_t data_size, char *data);
#endif
};

#endif
