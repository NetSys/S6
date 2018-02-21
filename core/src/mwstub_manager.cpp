#include "mwstub_manager.hh"
#include "controlbus.hh"
#include "d_object.hh"
#include "d_routine.hh"
#include "key_space.hh"
#include "mem_pool.hh"
#include "message.hh"
#include "mw_skeleton.hh"
#include "mw_stub.hh"
#include "time.hh"
#include "worker.hh"

#include "stub_factory.hh"

#define COMMUTATIVE 0

struct StrictReturn {
  uint32_t size;
  void *data;
};

struct CacheReturn {
  bool need_update;
  uint32_t size;
  uint64_t last_update_tsc;
  void *data;
};

class PRADSStat;
class Asset;
class MaliciousServer;

static uint64_t hz = 0;

#define CACHE_TIMEOUT 1000  // in ms
#define CACHE_TIMEOUT_HZ (CACHE_TIMEOUT * hz / 1.0E+3)

#define RPC_SEND_TIMEOUT 10  // in ms
#define RPC_SEND_TIMEOUT_HZ (CACHE_TIMEOUT * hz / 1.0E+3)

static void init_hz() {
  hz = get_tsc_freq();
}

MwStubManager::MwStubManager(uint32_t node_id, Worker *worker,
                             DroutineScheduler *sch, ControlBus *cbus,
                             KeySpace *key_space, MemPool *mp) {
  this->node_id = node_id;
  this->key_space = key_space;
  this->scheduler = sch;
  this->mp = mp;

  // XXX HOPE TO REMOVE!!!
  this->worker = worker;
  this->cbus = cbus;

  init_hz();

  TAILQ_INIT(&aggr_list);
#if 0
	// for dynamic scaling
	for (int i = 0; i < _DADTCnt; i++) {
		skeleton_export_reserve[i] = false;
		skeleton_map_lock[i] = false;
	}
#endif
};

int MwStubManager::force_scaling(int max_objects) {
  return 0;
}

MwStubBase *MwStubManager::get(int map_id, const Key *key) {
  MwStubMap &mwstub_map = mwstub_map_arr[map_id];
  MwStubBase *ref;

  auto iter = mwstub_map.find(key);
  if (iter == mwstub_map.end()) {
    ref = (MwStubBase *)StubFactory::GetMwStubBase(map_id, key, this);
    if (!ref) {
      errno = -ENOMEM;
      return nullptr;
    }
    mwstub_map[key->clone()] = ref;
  } else {
    ref = iter->second;
  }

  return ref;
}

MwStubBase *MwStubManager::create(int map_id, const Key *key, RefState &state) {
  return get(map_id, key);
}

MwStubBase *MwStubManager::lookup(int map_id, const Key *key) {
  return get(map_id, key);
}

void MwStubManager::release(int map_id, const Key *key) {
  MwStubMap &mwstub_map = mwstub_map_arr[map_id];

  auto iter = mwstub_map.find(key);
  if (iter == mwstub_map.end())
    DEBUG_ERR("No key exist for rcp ref\n");

  // XXX: no delete mwstub. When do we remove them?
  // mwstub_map.erase(iter);
}

void MwStubManager::teardown(bool force) {
  if (!force)
    DEBUG_ERR("graceful teardown is not yet implemented");

  for (int i = 0; i < ADTCnt; i++) {
    MwStubMap &mwstub_map = mwstub_map_arr[i];
    for (auto iter = mwstub_map.begin(); iter != mwstub_map.end();) {
      delete iter->first;
      delete iter->second;
      mwstub_map.erase(iter++);
    }
  }

  for (int i = 0; i < ADTCnt; i++) {
    MWSkeletonMap &mw_sk_map = mw_skeleton_map_arr[i];
    for (auto iter = mw_sk_map.begin(); iter != mw_sk_map.end();) {
      delete iter->first;
      delete iter->second;
      mw_sk_map.erase(iter++);
    }
  }
}

void MwStubManager::set_strict_return(int d_idx, uint32_t arg_size,
                                      void *data) {
  StrictReturn *strict = new StrictReturn();
  strict->size = arg_size;
  strict->data = malloc(strict->size);
  memcpy(strict->data, data, arg_size);

  strict_ret_map[d_idx] = strict;

  scheduler->notify_to_wake_up(d_idx);
};

void MwStubManager::set_cache_return(int map_id, const Key *key, int method_id,
                                     uint32_t arg_size, void *data) {
  CacheReturn *cache = nullptr;
  auto it = cache_ret_map[map_id].find(VKey(key, method_id));
  if (it == cache_ret_map[map_id].end()) {
    cache = new CacheReturn();
    cache->size = arg_size;
    cache->data = malloc(arg_size);
    memcpy(cache->data, data, arg_size);
    cache_ret_map[map_id][VKey(key->clone(), method_id)] = cache;
  } else {
    cache = it->second;
    if (cache->size == arg_size) {
      memcpy(cache->data, data, arg_size);
    } else {
      cache->size = arg_size;
      free(cache->data);
      cache->data = malloc(arg_size);
      memcpy(cache->data, data, arg_size);
    }
  }

  cache->need_update = false;
  cache->last_update_tsc = get_cur_rdtsc();
  scheduler->notify_to_wake_up(map_id, key, method_id);
}

CacheReturn *MwStubManager::get_cache_return(int map_id, const Key *key,
                                             int method_id) {
  auto it = cache_ret_map[map_id].find(VKey(key, method_id));
  if (it != cache_ret_map[map_id].end()) {
    CacheReturn *cache = it->second;
    if (get_cur_rdtsc() - cache->last_update_tsc > CACHE_TIMEOUT_HZ) {
      cache->need_update = true;
    }
    return it->second;
  }

  return nullptr;
}

MWSkeleton *MwStubManager::get_mw_skeleton(int map_id, const Key *key) {
  MWSkeletonMap &mw_skeleton_map = mw_skeleton_map_arr[map_id];
  MWSkeleton *skeleton = nullptr;

  auto iter = mw_skeleton_map.find(key);
  if (iter == mw_skeleton_map.end()) {
    void *obj = mp->malloc(__global_dobj_size[map_id]);
    if (!obj) {
      DEBUG_ERR("Fail to malloc");
      assert(0);
      return nullptr;
    }

    skeleton = (MWSkeleton *)StubFactory::GetMWSkeleton(map_id, key, obj,
                                                        true /* new obj */);
    if (!skeleton) {
      errno = -ENOMEM;
      return nullptr;
    }
    mw_skeleton_map[key->clone()] = skeleton;
  } else {
    skeleton = iter->second;
  }

  return skeleton;
}

// called locally or remotely
void MwStubManager::execute_rpc(int map_id, const Key *key, uint32_t flag,
                                uint32_t method_id, void *args, void **ret,
                                uint32_t *ret_size) {
  MWSkeleton *skeleton = get_mw_skeleton(map_id, key);
  assert(skeleton);
  skeleton->exec(method_id, args, ret, ret_size);
  return;
}

inline bool MwStubManager::send_rpc_message(WorkerID to, int map_id,
                                            const Key *key, uint32_t flag,
                                            uint32_t method_id, void *args,
                                            uint32_t args_size) {
  int cur_routine_idx = scheduler->get_cur_routine_idx();
  MessageBuffer *m =
      create_mw_rpc_request(cbus, node_id, to, cur_routine_idx, map_id, key,
                            flag, method_id, args, args_size);

  worker->send_message(to, m);
  return true;
}

inline void MwStubManager::request_stale_rpc(WorkerID to, int map_id,
                                             const Key *key, uint32_t flag,
                                             uint32_t method_id, void *ret,
                                             uint32_t ret_size) {
  uint32_t _ret_size = 0;
  void *_ret = nullptr;

  CacheReturn *cache = get_cache_return(map_id, key, method_id);
  while (!cache) {
    // XXX: multiple rpc messages during cached item is not exist
    send_rpc_message(to, map_id, key, flag, method_id, nullptr, 0);
    scheduler->yield_block(map_id, key, method_id);
    cache = get_cache_return(map_id, key, method_id);
  }

  if (cache->need_update)
    send_rpc_message(to, map_id, key, flag, method_id, nullptr, 0);

  assert(ret_size == cache->size);
  memcpy(ret, cache->data, cache->size);
}

inline void init_mw_rpc_request_multi_asim(ControlBus *cbus, RPCBuf *rpc_buf,
                                           WorkerID from, WorkerID to) {
  MessageBuffer *mb = cbus->init_message(rpc_buf->buf, RPC_MSG_BUF_SIZE);
  Message *m = (Message *)(mb->buf + mb->body_offset);
  m->mtype = MSG_MW_RPC_REQUEST_MULTI_ASYM;
  m->from_id = from;
  m->to_id = to;

  RPCRequestMultiAsym *rpc_request = (RPCRequestMultiAsym *)(void *)m->buf;
  rpc_request->count = 0;
  rpc_buf->offset = sizeof(MessageBuffer) + mb->body_offset + sizeof(Message) +
                    sizeof(RPCRequestMultiAsym);
  rpc_buf->init_tsc = get_cur_rdtsc();

  // DEBUG_ERR("init rpc request " << mb << " offset " << rpc_buf->offset);
}

RPCRequest *MwStubManager::get_rpc_behind_message(WorkerID to, int msg_size) {
  RPCBuf *rpc_buf = &rpc_behind_buf[to];
  RPCRequest *rpc = nullptr;

  // initialize buffers
  if (rpc_buf->offset == 0)
    init_mw_rpc_request_multi_asim(cbus, rpc_buf, node_id, to);

  // cannot fill msg_size: flush buff
  if (rpc_buf->offset + msg_size >= RPC_MSG_BUF_SIZE ||
      get_cur_rdtsc() - rpc_buf->init_tsc > RPC_SEND_TIMEOUT_HZ) {
    MessageBuffer *mb = (MessageBuffer *)(void *)rpc_buf->buf;
    Message *m = (Message *)(mb->buf + mb->body_offset);
    RPCRequestMultiAsym *rpc_multi = (RPCRequestMultiAsym *)(void *)m->buf;

    if (rpc_multi->count == 0)
      assert(0);

    mb->body_size = rpc_buf->offset - sizeof(MessageBuffer);
    worker->send_message(to, mb);

    // DEBUG_ERR("Send RPC " << mb->body_size);
    init_mw_rpc_request_multi_asim(cbus, rpc_buf, node_id, to);
  }

  rpc = (RPCRequest *)(rpc_buf->buf + rpc_buf->offset);

  MessageBuffer *mb = (MessageBuffer *)(void *)rpc_buf->buf;
  Message *m = (Message *)(mb->buf + mb->body_offset);
  RPCRequestMultiAsym *rpc_multi = (RPCRequestMultiAsym *)(void *)m->buf;

  rpc_multi->count++;
  rpc_buf->offset += msg_size;

  return rpc;
}

inline void MwStubManager::request_behind_rpc(WorkerID to, int map_id,
                                              const Key *key, uint32_t flag,
                                              uint32_t method_id, void *args,
                                              uint32_t args_size,
                                              uint8_t mode) {
  if (mode == RPC_BATCHING) {
    int d_idx = scheduler->get_cur_routine_idx();
    uint32_t key_size = key->get_key_size();
    int msg_size = sizeof(RPCRequest) + key_size + args_size;

    RPCRequest *m = get_rpc_behind_message(to, msg_size);
    assert(m);

    fill_mw_rpc_request(m, d_idx, map_id, key_size, key, flag, method_id, args,
                        args_size);
  } else if (mode == RPC_ZIPPING) {
    assert(0);
  } else {
    send_rpc_message(to, map_id, key, flag, method_id, args, args_size);
  }
}

inline void MwStubManager::request_strict_rpc(WorkerID to, int map_id,
                                              const Key *key, uint32_t flag,
                                              uint32_t method_id, void *args,
                                              uint32_t args_size, void *ret,
                                              uint32_t ret_size) {
  uint32_t _ret_size = 0;
  void *_ret = nullptr;

  bool sent =
      send_rpc_message(to, map_id, key, flag, method_id, args, args_size);
  if (!sent)
    return;

  if (ret_size <= 0)
    return;

  // XXX Use *ret rather than doing other malloc
  int d_idx = scheduler->get_cur_routine_idx();

  auto it = strict_ret_map.find(d_idx);
  while (it == strict_ret_map.end()) {
    scheduler->yield_block(d_idx);
    it = strict_ret_map.find(d_idx);
  }

  StrictReturn *strict = it->second;

  assert(ret_size = strict->size);
  memcpy(ret, strict->data, strict->size);

  strict_ret_map.erase(it);
  free(strict->data);
  delete strict;
}

void MwStubManager::check_to_push_aggregation() {
#if COMMUTATIVE
  MWSkeleton *skeleton, *next;
  for (skeleton = TAILQ_FIRST(&aggr_list); skeleton != NULL; skeleton = next) {
    next = TAILQ_NEXT(skeleton, aggr_elem);

    if (get_cur_rdtsc() - skeleton->last_updated > CACHE_TIMEOUT_HZ) {
      TAILQ_REMOVE(&aggr_list, skeleton, aggr_elem);
      skeleton->in_list = false;
      skeleton->last_updated = get_cur_rdtsc();

      int map_id = skeleton->_map_id;
      const Key *key = skeleton->_key;
      WorkerID to = key_space->get_manager_of(map_id, key);

      if (map_id == 1) {
        MessageBuffer *mb = create_mw_aggr_request(
            cbus, node_id, to, map_id, key, skeleton->_obj, sizeof(PRADSStat));
        worker->send_message(to, mb);

        PRADSStat *local = (PRADSStat *)skeleton->_obj;
        local->_init();
      } else if (map_id == 0) {
        static int print = 0;
        if (print++ < 10)
          DEBUG_ERR("Asset size " << sizeof(Asset));

        MessageBuffer *mb = create_mw_aggr_request(
            cbus, node_id, to, map_id, key, skeleton->_obj, sizeof(Asset));
        worker->send_message(to, mb);

        Asset *local = (Asset *)skeleton->_obj;
        local->_init();
      }
    } else
      break;
  }
#endif
}

void MwStubManager::request_rpc(int map_id, const Key *key, uint32_t flag,
                                uint32_t method_id, void *args,
                                uint32_t args_size, void *ret,
                                uint32_t ret_size) {
  WorkerID to = key_space->get_manager_of(map_id, key);

  // execute in local skeleton
  if (to == -1 || to == this->node_id) {
    uint32_t _ret_size = 0;

    uint8_t _ret[ret_size];
    void *_ret_p = (void *)_ret;

    execute_rpc(map_id, key, flag, method_id, args, &_ret_p, &_ret_size);
    assert(ret_size == _ret_size);
    if (ret_size) {
      memcpy(ret, _ret, ret_size);
    }
    return;
  }

#if COMMUTATIVE
  /* XXXXX Need to fix after deadline */
  // if (map_id == 1 /* g_prads_stat */ ||
  //	map_id == 0 /* g_asset */	||
  //	map_id == 24 /* g_malicious_server_map */) {
  if (map_id == 1 /* g_prads_stat */
      || map_id == 0 /* g_asset */) {
    // locally update
    MWSkeleton *skeleton = get_mw_skeleton(map_id, key);
    assert(skeleton);
    skeleton->exec(method_id, args, &ret, &ret_size);

    if (skeleton->last_updated == 0)
      skeleton->last_updated = get_cur_rdtsc();

    if (skeleton->in_list) {
      TAILQ_REMOVE(&aggr_list, skeleton, aggr_elem);
      skeleton->in_list = false;
    }

    if (get_cur_rdtsc() - skeleton->last_updated > CACHE_TIMEOUT_HZ) {
      skeleton->last_updated = get_cur_rdtsc();

      if (map_id == 1) {
        MessageBuffer *mb = create_mw_aggr_request(
            cbus, node_id, to, map_id, key, skeleton->_obj, sizeof(PRADSStat));
        worker->send_message(to, mb);

        PRADSStat *local = (PRADSStat *)skeleton->_obj;
        local->_init();
      } else if (map_id == 0) {
        MessageBuffer *mb = create_mw_aggr_request(
            cbus, node_id, to, map_id, key, skeleton->_obj, sizeof(Asset));
        worker->send_message(to, mb);

        Asset *local = (Asset *)skeleton->_obj;
        local->_init();
      }
    } else {
      skeleton->in_list = true;
      TAILQ_INSERT_TAIL(&aggr_list, skeleton, aggr_elem);
    }

    return;
  }
#endif

  if (flag & _FLAG_STALE) {
    assert(args_size == 0);
    request_stale_rpc(to, map_id, key, flag, method_id, ret, ret_size);

  } else if (flag & _FLAG_BEHIND) {
    assert(ret_size == 0);
    request_behind_rpc(
        to, map_id, key, flag, method_id, args, args_size,
        //		RPC_DEFAULT);
        RPC_BATCHING); /* FIX ME - choose rpc optimization as parameter */
  } else {
    request_strict_rpc(to, map_id, key, flag, method_id, args, args_size, ret,
                       ret_size);
  }

  return;
}

void MwStubManager::aggregate(int map_id, const Key *key, MWObject *obj) {
  MWSkeleton *skeleton = get_mw_skeleton(map_id, key);
  assert(skeleton);

#if COMMUTATIVE
  if (map_id == 1) {
    PRADSStat *global = (PRADSStat *)skeleton->_obj;
    global->_add(*(PRADSStat *)obj);
  } else if (map_id == 0) {
    Asset *global = (Asset *)skeleton->_obj;
    // global->_add(*((Asset *) obj)); //XXX FIXME
  } else if (map_id == 24) {
    MaliciousServer *global = (MaliciousServer *)skeleton->_obj;
    // global->_add(*((MaliciousServer *) obj)); //XXX FIXME
  }
#endif
}

int MwStubManager::create_local_iterator(int map_id) {
#if 0
	if (skeleton_map_lock[map_id])
		return -1;
#endif
  MapIterator *iter = nullptr;
  int iter_idx;
  for (iter_idx = 0; iter_idx < MAX_ITERATOR_CNT; iter_idx++) {
    if (!skeleton_iterator[map_id][iter_idx].is_valid) {
      iter = &skeleton_iterator[map_id][iter_idx];
      break;
    }
  }

  if (!iter)
    return -1;

  MWSkeletonMap &mw_skeleton_map = mw_skeleton_map_arr[map_id];

  iter->is_valid = true;
  iter->iter = mw_skeleton_map.begin();

  return iter_idx;
}

const Key *MwStubManager::get_local_next_key(int map_id, int itidx) {
  MWSkeletonMap &mw_skeleton_map = mw_skeleton_map_arr[map_id];

  MapIterator *iter = &skeleton_iterator[map_id][itidx];
  if (iter->iter == mw_skeleton_map.end()) {
    return nullptr;
  }

  const Key *key = iter->iter->first;
  iter->iter++;
  return key;
}

MwStubBase *MwStubManager::get_local_next_item(int map_id, int itidx) {
  MWSkeletonMap &mw_skeleton_map = mw_skeleton_map_arr[map_id];

  MapIterator *iter = &skeleton_iterator[map_id][itidx];
  if (iter->iter == mw_skeleton_map.end())
    return nullptr;

  MwStubBase *ref = lookup(map_id, iter->iter->first);
  iter->iter++;
  return ref;
}

MwStubPair MwStubManager::get_local_next_pair(int map_id, int itidx) {
  MWSkeletonMap &mw_skeleton_map = mw_skeleton_map_arr[map_id];

  MapIterator *iter = &skeleton_iterator[map_id][itidx];
  if (iter->iter == mw_skeleton_map.end())
    return {nullptr, nullptr};

  const Key *key = iter->iter->first;
  MwStubBase *ref = lookup(map_id, key);
  iter->iter++;
  return {key, ref};
}

void MwStubManager::release_local_iterator(int map_id, int itidx) {
  MapIterator *iter = &skeleton_iterator[map_id][itidx];
  iter->is_valid = false;
}

#if 0
int MwStubManager::export_objects(int map_id, 
		uint32_t (*pair_count)[MAX_WORKER_CNT], 
		MWSkeletonPair *(*pairs)[MAX_WORKER_CNT])
{
	// init to nothing
	for (int i = 0; i < MAX_WORKER_CNT; i++) {
		(*pair_count)[i] = 0;
		(*pairs)[i] = nullptr;
	}
	
	for (int iter_idx = 0; iter_idx < MAX_ITERATOR_CNT; iter_idx++) {
		// if a iterator exist, stop exporting!
		if (skeleton_iterator[map_id][iter_idx].is_valid) {
			skeleton_export_reserve[map_id] = true;
			return -1;
		}
	}

	skeleton_map_lock[map_id] = true;

	// allocate a buffer for store exported bytestream
	MWSkeletonMap &mw_skeleton_map = mw_skeleton_map_arr[map_id];
	
	uint32_t max_pair_count = mw_skeleton_map.size();
	for (int i = 0; i < MAX_WORKER_CNT; i++) {
		(*pairs)[i] = (MWSkeletonPair *)malloc(sizeof(MWSkeletonPair) * 
				max_pair_count);
		if (!(*pairs)[i]) { 
			DEBUG_ERR("Fail to allocate buffer for exported object poiters ");
			return -1;
		}
	}

	for (auto iter = mw_skeleton_map.begin(); 
			iter != mw_skeleton_map.end();) {
		
		WorkerID wid = key_space->get_manager_of(map_id, iter->first);
		if (wid != -1 && wid != node_id) {

			MWSkeletonPair *_data = (*pairs)[wid];
			if ((*pair_count)[wid] + 1 >= max_pair_count) {
				DEBUG_ERR("Allocated buffer size is too small");
				assert(0);
			}

			MWSkeletonPair *pair = &_data[(*pair_count)[wid]];
			pair->key = iter->first;
			pair->obj_size = iter->second->get_size();
			pair->obj_ref = iter->second;
		
			(*pair_count)[wid]++;

			mw_skeleton_map.erase(iter++);
		} else {
			iter++;
		}
	}
	
	skeleton_map_lock[map_id] = false;

	return 0;
}
	
void MwStubManager::import_objects(int map_id, uint32_t key_size, 
		uint32_t obj_count, uint32_t data_size, char *data) 
{
	DEBUG_ERR("importing objects");
	
	skeleton_map_lock[map_id] = true;
	
	MWSkeletonMap &mw_skeleton_map = mw_skeleton_map_arr[map_id];
	
	int offset = 0;
	for (int i = 0; i < (int) obj_count; i++) {
		const Key *key = (const Key *)(data + offset);
		offset += key_size;
		
		// Finally remove error from key conflict 
		// after implementing correctscaling process
		auto iter = mw_skeleton_map.find(key);
		if (iter != mw_skeleton_map.end())
			DEBUG_ERR("Conflict on integrating key " << *key);
			
		uint32_t obj_size = *(uint32_t *)(data + offset);
		offset += sizeof(uint32_t);

		//MWSkeleton *skeleton = (MWSkeleton *) StubFactory::
		//	GetMWSkeleton(map_id, key); 
		MWSkeleton *skeleton = (MWSkeleton *) StubFactory::
			GetMWSkeleton(map_id, key, obj_size, data + offset); 
		if (!skeleton) {
			DEBUG_ERR("Fail to import objects");
			assert(0);
			return;
		}
		offset += sizeof(uint32_t);

		mw_skeleton_map[key->clone()] = skeleton;
	}

	skeleton_map_lock[map_id] = false;
	
	DEBUG_ERR("Finish importing objects");
}
#endif
