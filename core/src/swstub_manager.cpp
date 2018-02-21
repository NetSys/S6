#include <cassert>

#include "d_reference.hh"
#include "d_routine.hh"
#include "log.hh"
#include "stub_factory.hh"
#include "sw_stub.hh"
#include "swstub_manager.hh"

int active_references = 0;

struct SwStubInfo {
  bool reserve_rw_expired;  // reserve to expire current rw accessibility
  bool is_blocked;          // access is now blocking to make SwStub remotely

  int version;
  WorkerID created_from;
  int local_rw_cnt;

  SwStubBase *ref = nullptr;
};

struct SWDeadObjInfo {
  int version;
  WorkerID created_from;
  int local_rw_cnt;

  SwStubBase *ref = nullptr;
};

struct SwStubROInfo {
  bool is_blocked;  // access is now blocking to make SwStub remotely

  int version;
  int local_ro_cnt;

  SwStubBase *ref = nullptr;
};

inline static void reset_swstub_info(SwStubInfo *swstub_info) {
  swstub_info->is_blocked = false;
  swstub_info->reserve_rw_expired = false;

  if (swstub_info->ref)
    delete swstub_info->ref;
  swstub_info->ref = nullptr;

  swstub_info->version = -1;
  swstub_info->local_rw_cnt = 0;
}

SWDeadObjInfo *create_deadobj_from_swstub(SwStubInfo *swstub_info) {
  SWDeadObjInfo *deadobj_info = new SWDeadObjInfo();
  if (!deadobj_info) {
    errno = -ENOMEM;
    assert(0);
  }

  deadobj_info->created_from = swstub_info->created_from;
  deadobj_info->version = swstub_info->version;
  deadobj_info->local_rw_cnt = swstub_info->local_rw_cnt;
  deadobj_info->ref = swstub_info->ref;

  return deadobj_info;
}

inline static void delete_all_swstub_info(MemPool *mp,
                                          SwStubInfo *swstub_info) {
  active_references--;

  if (swstub_info->ref == nullptr) {
    delete swstub_info;
    return;
  }

  if (swstub_info->ref->_obj == nullptr) {
    delete swstub_info->ref;
    delete swstub_info;
    return;
  }

  mp->free(swstub_info->ref->_obj);
  delete swstub_info->ref;
  delete swstub_info;
}

inline static void delete_all_swstub_ro_info(SwStubROInfo *swstub_info) {
  delete swstub_info->ref;
  delete swstub_info;
}

inline static void delete_all_deadobj_info(MemPool *mp,
                                           SWDeadObjInfo *deadobj_info) {
  mp->free(deadobj_info->ref->_obj);
  delete deadobj_info->ref;
  delete deadobj_info;
}

inline static void notify_ro_ref_is_ready(DroutineScheduler *scheduler,
                                          int map_id, const Key *key) {
  if (scheduler)
    scheduler->notify_to_wake_up(map_id, key, SW_RO_BLOCK);
}

inline static void wait_ro_ref_is_ready(DroutineScheduler *scheduler,
                                        int map_id, const Key *key) {
  if (scheduler)
    scheduler->yield_block(map_id, key, SW_RO_BLOCK);
}

inline static void notify_rw_ref_is_ready(DroutineScheduler *scheduler,
                                          int map_id, const Key *key) {
  if (scheduler)
    scheduler->notify_to_wake_up(map_id, key, SW_RW_BLOCK);
}

inline static void wait_rw_ref_is_ready(DroutineScheduler *scheduler,
                                        int map_id, const Key *key) {
  if (scheduler)
    scheduler->yield_block(map_id, key, SW_RW_BLOCK);
}

void SwStubManager::teardown(bool force) {
  if (!force)
    DEBUG_ERR("graceful teardown is not yet implemented");

  for (int i = 0; i < ADT_cnt; i++) {
    SwStubROMap &swstub_map = swstub_ro_map_arr[i];
    for (auto it = swstub_map.begin(); it != swstub_map.end();) {
      const Key *key = it->first;
      SwStubROInfo *info = it->second;

      it = swstub_map.erase(it);

      delete key;
      delete_all_swstub_ro_info(info);
    }
  }

  for (int i = 0; i < ADT_cnt; i++) {
    SwStubMap &swstub_map = swstub_rw_map_arr[i];
    for (auto it = swstub_map.begin(); it != swstub_map.end();) {
      const Key *key = it->first;
      SwStubInfo *info = it->second;

      it = swstub_map.erase(it);

      delete key;
      delete_all_swstub_info(mp, info);
    }
  }

  for (int i = 0; i < ADT_cnt; i++) {
    SWDeadObjTable &table = deadobj_table_arr[i];
    for (auto tit = table.begin(); tit != table.end(); ++tit) {
      const Key *key = tit->first;

      SWDeadObjMap &map = tit->second;
      for (auto mit = map.begin(); mit != map.end();) {
        SWDeadObjInfo *info = mit->second;
        mit = map.erase(mit);
        delete_all_deadobj_info(mp, info);
      }

      delete key;
    }
  }
}

SwStubInfo *SwStubManager::get_swstub_info(int map_id, const Key *key) {
  SwStubMap &swstub_map = swstub_rw_map_arr[map_id];

  SwStubMap::iterator it = swstub_map.find(key);
  if (it == swstub_map.end())
    return nullptr;

  return it->second;
}

SwStubInfo *SwStubManager::create_swstub_info(int map_id, const Key *key) {
  SwStubMap &swstub_map = swstub_rw_map_arr[map_id];

  SwStubInfo *swstub_info = new SwStubInfo();
  if (!swstub_info) {
    errno = -ENOMEM;
    return nullptr;
  }

  reset_swstub_info(swstub_info);

  swstub_map[key->clone()] = swstub_info;

  return swstub_info;
}

void SwStubManager::erase_swstub_info(int map_id, const Key *key, int version) {
  SwStubMap &swstub_map = swstub_rw_map_arr[map_id];

  SwStubMap::iterator it = swstub_map.find(key);
  if (it == swstub_map.end())
    assert(0);
  assert(it->second->version == version);

  const Key *it_key = it->first;
  SwStubInfo *swstub_info = it->second;

  swstub_map.erase(it);
  delete it_key;
}

SwStubROInfo *SwStubManager::get_swstub_ro_info(int map_id, const Key *key) {
  SwStubROMap &swstub_map = swstub_ro_map_arr[map_id];

  SwStubROMap::iterator it = swstub_map.find(key);
  if (it == swstub_map.end())
    return nullptr;

  return it->second;
}

SwStubROInfo *SwStubManager::get_swstub_ro_info(int map_id, const Key *key,
                                                int version) {
  SwStubROMap &swstub_map = swstub_ro_map_arr[map_id];

  SwStubROMap::iterator it = swstub_map.find(key);
  if (it == swstub_map.end() || it->second->version != version) {
    // FIXME: check dead object reference map
    return nullptr;
  }

  return it->second;
}

SwStubROInfo *SwStubManager::create_swstub_ro_info(int map_id, const Key *key) {
  SwStubROMap &swstub_map = swstub_ro_map_arr[map_id];

  SwStubROInfo *swstub_info = new SwStubROInfo();
  if (!swstub_info) {
    errno = -ENOMEM;
    return nullptr;
  }

  swstub_info->is_blocked = false;
  swstub_info->version = -1;
  swstub_info->local_ro_cnt = 0;
  swstub_info->ref = nullptr;

  const Key *ckey = key->clone();
  swstub_map[ckey] = swstub_info;

  return swstub_info;
}

void SwStubManager::erase_swstub_ro_info(int map_id, const Key *key,
                                         int version) {
  SwStubROMap &swstub_map = swstub_ro_map_arr[map_id];

  SwStubROMap::iterator it = swstub_map.find(key);
  if (it == swstub_map.end())
    assert(0);
  assert(it->second->version == version);

  const Key *it_key = it->first;
  SwStubROInfo *swstub_info = it->second;

  swstub_map.erase(it);
  delete it_key;
}

SWDeadObjInfo *SwStubManager::get_deadobj_info(int map_id, const Key *key,
                                               int version) {
  SWDeadObjTable &cache_table = deadobj_table_arr[map_id];

  SWDeadObjTable::iterator it_table = cache_table.find(key);
  if (it_table == cache_table.end())
    return nullptr;

  SWDeadObjMap &cache_map = it_table->second;

  SWDeadObjMap::iterator it_map = cache_map.find(version);
  if (it_map == cache_map.end())
    return nullptr;

  return it_map->second;
}

void SwStubManager::insert_deadobj_info(int map_id, const Key *key,
                                        SWDeadObjInfo *deadobj_info) {
  SWDeadObjTable &cache_t = deadobj_table_arr[map_id];
  cache_t[key->clone()][deadobj_info->version] = deadobj_info;
  return;
}

void SwStubManager::erase_deadobj_info(int map_id, const Key *key,
                                       int version) {
  SWDeadObjTable &cache_table = deadobj_table_arr[map_id];

  SWDeadObjTable::iterator it_table = cache_table.find(key);
  if (it_table == cache_table.end())
    assert(0);

  SWDeadObjMap &cache_map = it_table->second;

  SWDeadObjMap::iterator it_map = cache_map.find(version);
  if (it_map == cache_map.end())
    assert(0);

  cache_map.erase(it_map);

  delete_all_deadobj_info(mp, it_map->second);

  if (cache_map.empty()) {
    cache_table.erase(it_table);
    delete it_table->first;
  }
}

SwStubBase *SwStubManager::create_rwref(int map_id, const Key *key,
                                        int &version, RefState &state,
                                        WorkerID &created_from) {
  // request the ownership to the serializer
  void *obj;
  int ret = swobj_manager->local_create_object(map_id, key, version, &obj,
                                               created_from, state);
  if (ret < 0) {
    return nullptr;
  }
  assert(version >= 0);

  static int counter = 0;
  active_references++;

  // if (counter++ % 1000 == 0)
  //	DEBUG_ERR("number of active references " << active_references);

  if (obj) {
    state.created = false;
    DEBUG_DEV("SET SWREF " << *key << " with existing object ver." << version);
    return StubFactory::GetSwStubBase(map_id, key, version, obj,
                                      false /* is new */);
  } else {
    state.created = true;
    DEBUG_DEV("SET SWREF " << *key << " with object creation ver." << version);

    void *obj = mp->malloc(__global_dobj_size[map_id]);
    if (!obj) {
      DEBUG_ERR("Fail to malloc");
      assert(0);
      return nullptr;
    }
    return StubFactory::GetSwStubBase(map_id, key, version, obj,
                                      true /* is new */);
  }
}

SwStubBase *SwStubManager::lookup_rwref(int map_id, const Key *key,
                                        int &version, WorkerID &created_from) {
  void *obj;
  int ret = swobj_manager->local_lookup_object(map_id, key, version, &obj,
                                               created_from);
  if (ret < 0) {
    return nullptr;
  }

  if (obj) {
    DEBUG_DEV("SET SWREF " << *key << " with existing object ver." << version);
    return StubFactory::GetSwStubBase(map_id, key, version, obj,
                                      false /* is new object? */);
  } else {
    return nullptr;
  }
}

int SwStubManager::delete_rwref(int map_id, const Key *key,
                                SwStubInfo *swstub_info) {
  DEBUG_DEV("delete with version " << swstub_info->version << " Local RW_CNT "
                                   << swstub_info->local_rw_cnt);

  if (swstub_info->local_rw_cnt == 0) {
    erase_swstub_info(map_id, key, swstub_info->version);
    delete_all_swstub_info(mp, swstub_info);
  } else {
    SWDeadObjInfo *deadobj_info = create_deadobj_from_swstub(swstub_info);
    insert_deadobj_info(map_id, key, deadobj_info);
    erase_swstub_info(map_id, key, swstub_info->version);
    delete swstub_info;
  }
  return 0;
}

SwStubBase *SwStubManager::create_roref(int map_id, const Key *key,
                                        int &version) {
  int ret = swobj_manager->local_create_cache(map_id, key, version);
  if (ret < 0) {
    return nullptr;
  }
  assert(version >= 0);

  DEBUG_DEV("SET SWREF for RO" << *key << " with object creation ver."
                               << version);
  return StubFactory::GetSwStubBase_RO(map_id, key, version, this);
}

int SwStubManager::delete_roref(int map_id, const Key *key,
                                SwStubROInfo *swstub_info) {
  DEBUG_DEV("delete read only reference with version " << swstub_info->version);

  erase_swstub_ro_info(map_id, key, swstub_info->version);
  delete_all_swstub_ro_info(swstub_info);

  return 0;
}

/*
 * Return SwStub states - alive, accessible, transferrable
 * */

bool SwStubManager::is_obj_alive(int map_id, const Key *key, int version) {
  SwStubInfo *swstub_info = get_swstub_info(map_id, key);
  if (!swstub_info || swstub_info->version != version)
    return false;

  return true;
}

bool SwStubManager::is_ro_accessible(int map_id, const Key *key, int version) {
  if (is_obj_alive(map_id, key, version))
    return true;

  SwStubROInfo *swstub_info = get_swstub_ro_info(map_id, key);
  if (!swstub_info || swstub_info->version != version)
    return false;

  return true;
}

SwStubBase *SwStubManager::create(int map_id, const Key *key, RefState &state) {
  SwStubInfo *swstub_info = nullptr;

  state.created = false;
  state.in_local = true;

  while (!swstub_info) {
    swstub_info = get_swstub_info(map_id, key);
    if (!swstub_info)
      swstub_info = create_swstub_info(map_id, key);

    if (swstub_info->ref)
      break;

    if (!swstub_info->is_blocked) {
      // only a single microthread can go enter here at a time
      swstub_info->is_blocked = true;

      int version;
      WorkerID created_from;
      // this could be blocked
      SwStubBase *ref = create_rwref(map_id, key, version, state, created_from);
      if (!ref)
        return nullptr;

      swstub_info->created_from = created_from;
      swstub_info->ref = ref;
      swstub_info->is_blocked = false;
      swstub_info->version = version;

      // wake up other micro-threads
      notify_rw_ref_is_ready(scheduler, map_id, key);
    } else {
      // there exist a forward request from another micro-threads
      // so yield until previous requests are ready
      wait_rw_ref_is_ready(scheduler, map_id, key);
      swstub_info = nullptr;
    }
  }  // while loop until object is available

  assert(swstub_info->ref);
  swstub_info->local_rw_cnt++;

  return swstub_info->ref;
}

SwStubBase *SwStubManager::get(int map_id, const Key *key) {
  RefState state;
  return create(map_id, key, state);
}

SwStubBase *SwStubManager::lookup(int map_id, const Key *key) {
  SwStubInfo *swstub_info = nullptr;

  while (!swstub_info) {
    swstub_info = get_swstub_info(map_id, key);
    if (!swstub_info)
      swstub_info = create_swstub_info(map_id, key);

    if (swstub_info->ref)
      break;

    if (!swstub_info->is_blocked) {
      // only a single microthread can go enter here at a time
      swstub_info->is_blocked = true;

      int version;
      WorkerID created_from;
      // this could be blocked
      SwStubBase *ref = lookup_rwref(map_id, key, version, created_from);
      if (!ref) {
        erase_swstub_info(map_id, key, swstub_info->version);
        delete_all_swstub_info(mp, swstub_info);
        swstub_info = nullptr;

        notify_rw_ref_is_ready(scheduler, map_id, key);
        return nullptr;
      }

      swstub_info->created_from = created_from;
      swstub_info->ref = ref;
      swstub_info->is_blocked = false;
      swstub_info->version = version;

      // wake up other micro-threads
      notify_rw_ref_is_ready(scheduler, map_id, key);
    } else {
      wait_rw_ref_is_ready(scheduler, map_id, key);
      swstub_info = nullptr;
    }
  }

  assert(swstub_info->ref);
  swstub_info->local_rw_cnt++;

  return swstub_info->ref;
}

int SwStubManager::release(int map_id, const Key *key, int version) {
  SwStubInfo *swstub_info = get_swstub_info(map_id, key);
  if (!swstub_info || swstub_info->version != version) {
    SWDeadObjInfo *deadobj_info = get_deadobj_info(map_id, key, version);
    if (!deadobj_info) {
      DEBUG_ERR("Try to release reference of non-existing objects");
      return -1;
    }
    deadobj_info->local_rw_cnt--;
    if (deadobj_info->local_rw_cnt == 0) {
      erase_deadobj_info(map_id, key, version);
      swobj_manager->local_cleanup_meta(map_id, key, version,
                                        deadobj_info->created_from);
    }
    return 0;
  }

  swstub_info->local_rw_cnt--;

  if (swstub_info->local_rw_cnt == 0 && swstub_info->reserve_rw_expired) {
    swobj_manager->local_notify_expire_rwref(
        map_id, key, version, swstub_info->ref->_obj_size,
        swstub_info->ref->_obj, swstub_info->created_from);
    delete_rwref(map_id, key, swstub_info);
  }

  return 0;
}

void SwStubManager::delete_object(int map_id, const Key *key, int version) {
  SwStubInfo *swstub_info = get_swstub_info(map_id, key);
  if (!swstub_info) {
    errno = -EINVAL;
    DEBUG_ERR("Cannot release object which is not acquired " << *key);
    return;
  }
  assert(swstub_info->version == version);

  // delete and expire rw ref same time
  swstub_info->local_rw_cnt--;

  bool cleanup = false;
  if (swstub_info->local_rw_cnt == 0)
    cleanup = true;

  swobj_manager->local_delete_object(map_id, key, version, cleanup,
                                     swstub_info->created_from);

  delete_rwref(map_id, key, swstub_info);
}

SwStubBase *SwStubManager::lookup_cache(int map_id, const Key *key) {
  SwStubROInfo *swstub_info = get_swstub_ro_info(map_id, key);
  if (!swstub_info)
    swstub_info = create_swstub_ro_info(map_id, key);

  while (!swstub_info->ref) {
    if (!swstub_info->is_blocked) {
      swstub_info->is_blocked = true;

      int version;
      SwStubBase *ref = create_roref(map_id, key, version);
      // FIX ME: it is possible to create to request cache, but not actually
      // exisit
      if (!ref) {
        // XXX do not notify swstub_ro is not available to other coroutines
        erase_swstub_ro_info(map_id, key, -1);
        delete_all_swstub_ro_info(swstub_info);
        return nullptr;
      }

      swstub_info->ref = ref;
      swstub_info->is_blocked = false;
      swstub_info->version = version;

      notify_ro_ref_is_ready(scheduler, map_id, key);
    } else {
      // while loop, since,
      // object may deleted between notification and actual scheduling
      wait_ro_ref_is_ready(scheduler, map_id, key);
    }

    assert(swstub_info->ref);
    swstub_info->local_ro_cnt++;

    return swstub_info->ref;
  }

  swstub_info->local_ro_cnt++;

  return (SwStubBase *)swstub_info->ref;
}

int SwStubManager::release_cache(int map_id, const Key *key, int version) {
  SwStubROInfo *swstub_info = get_swstub_ro_info(map_id, key, version);
  if (!swstub_info) {
    DEBUG_ERR("No read only reference for sw object info");
    assert(0);
  }

  swstub_info->local_ro_cnt--;

  // XXX delaying delete ro reference
  if (swstub_info->local_ro_cnt == 0) {
    delete_roref(map_id, key, swstub_info);
  }

  return 0;
}

int SwStubManager::request_expire_local_rwref(int map_id, const Key *key,
                                              int version) {
  SwStubInfo *swstub_info = get_swstub_info(map_id, key);
  if (!swstub_info) {
    DEBUG_ERR("No SwStubInfo");
    return -1;
  }

  if (swstub_info->version != version) {
    DEBUG_ERR("ref version " << swstub_info->version << " requested version "
                             << version);
    // expire request before actually create and using it
    swstub_info->reserve_rw_expired = true;
    return 0;
  }
  // assert(swstub_info->version == version);

  if (swstub_info->local_rw_cnt > 0) {
    // not ready to expire lease
    swstub_info->reserve_rw_expired = true;

  } else if (swstub_info->local_rw_cnt == 0) {
    // ready to expire lease

    swobj_manager->local_notify_expire_rwref(
        map_id, key, version, swstub_info->ref->_obj_size,
        swstub_info->ref->_obj, swstub_info->created_from);

    delete_rwref(map_id, key, swstub_info);
  } else {
    // should not reach here
    assert(0);
  }

  return 0;
}

void SwStubManager::request_rpc(int map_id, const Key *key, int version,
                                uint32_t flag, uint32_t method_id, void *args,
                                uint32_t args_size, void *ret,
                                uint32_t ret_size) {
  execute_rpc(map_id, key, version, flag, method_id, args, args_size, ret,
              ret_size);

  // FIXME: Remote rpc request

  return;
}

void SwStubManager::execute_rpc(int map_id, const Key *key, int version,
                                uint32_t flag, uint32_t method_id, void *args,
                                uint32_t args_size, void *ret,
                                uint32_t ret_size) {
  SwStubInfo *swstub_info = get_swstub_info(map_id, key);

  if (!swstub_info || swstub_info->version != version) {
    SWDeadObjInfo *deadobj_info = get_deadobj_info(map_id, key, version);
    if (!deadobj_info) {
      DEBUG_ERR("request rpc for dead objects");
      exit(EXIT_FAILURE);
    }

    deadobj_info->ref->exec(method_id, args, args_size, &ret, &ret_size);
  }

  swstub_info->ref->exec(method_id, args, args_size, &ret, &ret_size);
  return;
}
