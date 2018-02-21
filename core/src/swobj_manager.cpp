#include <cassert>
#include <cerrno>
#include <cstring>

#include "controlbus.hh"
#include "d_reference.hh"
#include "d_routine.hh"
#include "key_space.hh"
#include "mem_pool.hh"
#include "message.hh"
#include "swobj_manager.hh"
#include "swstub_manager.hh"
#include "worker.hh"

struct ObjectInfo {
  int version;
  bool is_activate;
  bool is_owned;
  bool is_local;
  WorkerID cur_worker;
  uint32_t obj_size;
  void *obj;

  // rw reference management
  int transfer_key_ownership_to = -1;
  int wait_key_ownership_from = -1;

  std::unordered_set<int> rw_metainfo_set;
  std::queue<int> rw_request_queue;
  std::unordered_set<int> rw_request_set;
};

struct ObjReturn {
  WorkerID created_from;
  int version;
  uint32_t size;
  void *data;
};

static void update_object_info(MemPool *mp, ObjectInfo *obj_info, int version,
                               void *obj, uint32_t obj_size) {
  // update object info from expired information
  obj_info->version = version;
  obj_info->is_owned = false;
  obj_info->cur_worker = -1;

  if (obj == nullptr) {
    if (obj_info->obj)
      mp->free(obj_info->obj);
    obj_info->obj_size = 0;
    obj_info->obj = nullptr;
  } else {
    if (obj_info->obj_size == obj_size) {
      if (obj_info->obj != obj) {
        memcpy(obj_info->obj, obj, obj_size);
      }
    } else {
      if (obj_info->obj)
        mp->free(obj_info->obj);
      obj_info->obj = mp->malloc(obj_size);
      if (!obj_info->obj) {
        DEBUG_ERR("Fail to malloc");
        assert(0);
        return;
      }
      memcpy(obj_info->obj, obj, obj_size);
      obj_info->obj_size = obj_size;
    }
  }
}

static void activate_object_info(struct ObjectInfo *info) {
  info->version++;
  info->is_activate = true;
  info->is_owned = false;
  info->cur_worker = -1;
  info->obj = nullptr;
  info->obj_size = 0;

  info->is_local = true;

  info->rw_metainfo_set.insert(info->version);

  auto it = info->rw_metainfo_set.find(info->version);
  if (it == info->rw_metainfo_set.end()) {
    DEBUG_ERR("fail to insert version " << info->version);
  }
}

static void deactivate_object_info(MemPool *mp, struct ObjectInfo *info) {
  if (info->obj && !info->is_local)
    mp->free(info->obj);

  info->is_activate = false;
  info->is_owned = false;
  info->cur_worker = -1;
  info->obj = nullptr;
  info->obj_size = 0;
}

static void delete_all_obj_info(MemPool *mp, ObjectInfo *obj_info) {
  if (obj_info->obj && !obj_info->is_local)
    mp->free(obj_info->obj);
  delete obj_info;
}

static void cleanup_object_metainfo(struct ObjectInfo *info, int version) {
  auto it = info->rw_metainfo_set.find(version);
  if (it != info->rw_metainfo_set.end())
    info->rw_metainfo_set.erase(version);
}

static void add_to_rw_waitlist(ObjectInfo *obj_info, WorkerID node_id) {
  if (obj_info->rw_request_set.find(node_id) ==
      obj_info->rw_request_set.end()) {
    DEBUG_DEV("Worker " << node_id << " is waiting ");
    obj_info->rw_request_queue.push(node_id);
    obj_info->rw_request_set.insert(node_id);
  }

  return;
}

static int next_rw_waiter(ObjectInfo *obj_info) {
  if (!obj_info->rw_request_queue.empty()) {
    WorkerID node_id = obj_info->rw_request_queue.front();
    obj_info->rw_request_queue.pop();
    auto it = obj_info->rw_request_set.find(node_id);
    assert(it != obj_info->rw_request_set.end());
    obj_info->rw_request_set.erase(node_id);
    DEBUG_DEV("Worker " << node_id << " get the rwref");
    return node_id;
  }

  return -1;
}

int SWObjectManager::force_scaling(int max_objects) {
  int count = 0;

  for (int map_id = 0; map_id < ADT_cnt; map_id++) {
    ObjInfoMap *obj_map = obj_map_arr[map_id];
    if (obj_map == nullptr)
      continue;

    ObjInfoMap *next_obj_map = tmp_obj_map_arr[map_id];
    if (next_obj_map == nullptr) {
      tmp_obj_map_arr[map_id] = new ObjInfoMap();
      next_obj_map = tmp_obj_map_arr[map_id];
    }

    for (auto it = obj_map->begin(); it != obj_map->end();) {
      WorkerID new_id = key_space->get_manager_of(map_id, it->first);

      if (new_id == node_id || new_id == -1) {
        stats.own_objects_stale--;
        stats.own_objects_new++;

        (*next_obj_map)[it->first] = it->second;
        it = obj_map->erase(it);

      } else {
        ObjectInfo *obj_info = it->second;
        const Key *key = it->first;

        uint32_t waiters = -1;
        if (!obj_info->rw_request_queue.empty()) {
          waiters = obj_info->rw_request_queue.front();

          if (obj_info->rw_request_queue.size() >= 2) {
            DEBUG_ERR("Support single object ownership waiter in queue");
            assert(0);
          }
        }

        MessageBuffer *m = create_key_ownership_response(
            cbus, node_id, new_id, key_space->get_version(), map_id, key,
            obj_info->version, obj_info->obj, obj_info->obj_size, waiters);
        worker->send_message(new_id, m);

        stats.own_objects_stale--;
        stats.own_objects--;
        stats.own_remote--;

        if (obj_info->obj != nullptr)
          stats.obj_export++;

        delete_all_obj_info(mp, it->second);
        it = obj_map->erase(it);
      }

      if (count++ == max_objects)
        return count;
    }
  }

  return count;
}

void SWObjectManager::print_object_stats() {
  static uint32_t last_obj_import = 0;
  static uint32_t last_obj_export = 0;
  if (last_obj_import != stats.obj_import ||
      last_obj_export != stats.obj_export) {
    last_obj_import = stats.obj_import;
    last_obj_export = stats.obj_export;

    DEBUG_STAT("object import export " << stats.obj_import << "\t"
                                       << stats.obj_export);
  }

#if 0
	/* own local local->local local->remote remote<-local */
	DEBUG_STAT(stats.obj_import << "\t" << stats.obj_export << "\t"
			<< stats.own_objects << "\t" << stats.local_objects << "\t"
			<< stats.own_local << "\t" << stats.own_remote << "\t"
			<< stats.borrow_local);
#endif

#if 0
	DEBUG_STAT("own_object " << stats.own_objects);

	if (scaling.on) {
		DEBUG_STAT("\tstale own_objects " << stats.own_objects_stale);
		DEBUG_STAT("\tnew own_objects " << stats.own_objects_new);
	}

	DEBUG_STAT("own_local " << stats.own_local);	
	DEBUG_STAT("own_remote " << stats.own_remote);	
	DEBUG_STAT("local_objects " << stats.local_objects);	
	DEBUG_STAT("own_local " << stats.own_local);	
	DEBUG_STAT("borrow_local " << stats.borrow_local);
#endif
}

void SWObjectManager::teardown(bool force) {
  if (!force)
    DEBUG_ERR("graceful teardown is not yet implemented");

  for (int i = 0; i < ADT_cnt; i++) {
    ObjInfoMap *obj_map = obj_map_arr[i];
    if (obj_map == nullptr)
      continue;

    for (auto it = obj_map->begin(); it != obj_map->end();) {
      delete it->first;
      delete_all_obj_info(mp, it->second);
      it = obj_map->erase(it);
    }
  }

  for (int i = 0; i < ADT_cnt; i++) {
    ObjInfoMap *obj_map = tmp_obj_map_arr[i];
    if (obj_map == nullptr)
      break;

    for (auto it = obj_map->begin(); it != obj_map->end();) {
      delete it->first;
      delete_all_obj_info(mp, it->second);
      it = obj_map->erase(it);
    }
  }
}

ObjectInfo *SWObjectManager::get_object_info(int map_id, const Key *key) {
  if (map_id < 0 || map_id >= ADT_cnt) {
    errno = -EINVAL;
    return nullptr;
  }

  ObjInfoMap *obj_map = obj_map_arr[map_id];

  if (obj_map != nullptr) {
    ObjInfoMap::iterator it = obj_map->find(key);

    if (it != obj_map->end())
      return it->second;
  }

  if (scaling.on || scaling.dmz_to_scaling_on || scaling.dmz_to_quiescent_on) {
    obj_map = tmp_obj_map_arr[map_id];

    if (obj_map != nullptr) {
      ObjInfoMap::iterator it = obj_map->find(key);

      if (it != obj_map->end())
        return it->second;
    }
  }
  return nullptr;
}

ObjectInfo *SWObjectManager::create_object_info(int map_id, const Key *key) {
  ObjInfoMap *obj_map = obj_map_arr[map_id];
  if (obj_map == nullptr) {
    obj_map_arr[map_id] = new ObjInfoMap();
    obj_map = obj_map_arr[map_id];
    obj_map->reserve(24000);
  }

  // create if not exist
  ObjectInfo *obj_info = new ObjectInfo();
  if (!obj_info) {
    errno = -ENOMEM;
    return nullptr;
  }
  obj_info->version = -1;

  stats.own_objects++;
  stats.own_objects_stale++;

  deactivate_object_info(mp, obj_info);

  (*obj_map)[key->clone()] = obj_info;

  return obj_info;
}

ObjectInfo *SWObjectManager::create_object_info_in_nextspace(int map_id,
                                                             const Key *key) {
  ObjInfoMap *obj_map = tmp_obj_map_arr[map_id];
  if (obj_map == nullptr) {
    tmp_obj_map_arr[map_id] = new ObjInfoMap();
    obj_map = tmp_obj_map_arr[map_id];
    obj_map->reserve(24000);
  }

  // create if not exist
  ObjectInfo *obj_info = new ObjectInfo();
  if (!obj_info) {
    errno = -ENOMEM;
    return nullptr;
  }
  obj_info->version = -1;

  stats.own_objects++;
  stats.own_objects_new++;

  deactivate_object_info(mp, obj_info);

  (*obj_map)[key->clone()] = obj_info;

  return obj_info;
}

void SWObjectManager::move_object_info_to_nextspace(int map_id,
                                                    const Key *key) {
  ObjInfoMap *obj_map = obj_map_arr[map_id];
  if (obj_map == nullptr)
    assert(0);

  auto it = obj_map->find(key);

  ObjInfoMap *next_obj_map = tmp_obj_map_arr[map_id];
  if (next_obj_map == nullptr) {
    tmp_obj_map_arr[map_id] = new ObjInfoMap();
    next_obj_map = tmp_obj_map_arr[map_id];
  }

  (*next_obj_map)[it->first] = it->second;

  obj_map->erase(it);

  return;
}

void SWObjectManager::erase_object_info(int map_id, const Key *key) {
  if (scaling.on) {
    ObjInfoMap *obj_map = obj_map_arr[map_id];
    if (obj_map != nullptr) {
      auto it = obj_map->find(key);

      if (it != obj_map->end()) {
        const Key *it_key = it->first;
        obj_map->erase(it);
        delete it_key;

        stats.own_objects_stale--;
        return;
      }
    }

    obj_map = tmp_obj_map_arr[map_id];
    if (obj_map != nullptr) {
      auto it = obj_map->find(key);

      if (it != obj_map->end()) {
        const Key *it_key = it->first;
        obj_map->erase(it);
        delete it_key;

        stats.own_objects_new--;
        return;
      }
    }

    print_stack();
    assert(0);
    return;

  } else {
    ObjInfoMap *obj_map = obj_map_arr[map_id];
    assert(obj_map);

    auto it = obj_map->find(key);
    assert(it != obj_map->end());

    const Key *it_key = it->first;
    obj_map->erase(it);
    delete it_key;

    stats.own_objects_stale--;
  }
}

void SWObjectManager::set_rwobj(int map_id, const Key *key, int version,
                                uint32_t obj_size, void *data,
                                WorkerID created_from) {
  auto it = object_ret_map[map_id].find(key);
  if (it == object_ret_map[map_id].end()) {
    ObjReturn *obj = new ObjReturn();
    obj->created_from = created_from;
    obj->version = version;
    obj->size = obj_size;
    if (obj_size) {
      obj->data = mp->malloc(obj_size);
      if (!obj->data) {
        DEBUG_ERR("Fail to malloc");
        assert(0);
        return;
      }
      memcpy(obj->data, data, obj_size);
    } else {
      obj->data = nullptr;
    }
    object_ret_map[map_id][key->clone()] = obj;
  } else {
    DEBUG_ERR("Duplicated rw obj update " << map_id << ":" << *key);
    assert(0);
  }

  scheduler->notify_to_wake_up(map_id, key, SW_OBJ_BLOCK);
}

void SWObjectManager::get_rwobj(int map_id, const Key *key, int &version,
                                uint32_t &obj_size, void **data,
                                WorkerID &created_from) {
  auto it = object_ret_map[map_id].find(key);
  while (it == object_ret_map[map_id].end()) {
    scheduler->yield_block(map_id, key, SW_OBJ_BLOCK);
    it = object_ret_map[map_id].find(key);
  }

  ObjReturn *obj = it->second;
  version = obj->version;
  obj_size = obj->size;
  *data = obj->data;
  created_from = obj->created_from;

  delete it->first;
  delete it->second;
  object_ret_map[map_id].erase(it);
}

void SWObjectManager::return_obj_binary_local(ObjectInfo *obj_info, int map_id,
                                              const Key *key, int &version,
                                              void **obj) {
  stats.own_local++;
  if (!obj_info->is_owned) {
    DEBUG_OBJ("return local object " << *key << " from " << node_id);

    obj_info->is_owned = true;
    obj_info->cur_worker = node_id;
    version = obj_info->version;
    *obj = obj_info->obj;

    obj_info->is_local = true;

  } else {
    // My object, but others are now using it.
    DEBUG_OBJ("Object of " << node_id << " " << *key << " used_by "
                           << obj_info->cur_worker);

    add_to_rw_waitlist(obj_info, node_id);

    local_request_expire_rwref(obj_info->cur_worker, map_id, key,
                               obj_info->version);

    uint32_t obj_size;
    WorkerID temp;
    // blocking function
    get_rwobj(map_id, key, version, obj_size, obj, temp);
    if (temp == node_id)
      obj_info->is_local = true;

    DEBUG_OBJ("return local object " << *key << " from " << node_id);
  }
}

void SWObjectManager::return_obj_binary_remote(ObjectInfo *obj_info, int map_id,
                                               const Key *key, WorkerID to) {
  if (!obj_info->is_owned) {
    DEBUG_OBJ("remote object binary reqeust " << *key << " to " << to
                                              << " from " << node_id);

    obj_info->is_owned = true;
    obj_info->cur_worker = to;

    stats.own_remote++;

    // Request swobj to remote SWObjectManager
    DEBUG_OBJ("return remote object " << *key << " to " << to);

    MessageBuffer *m = create_object_ownership_response(
        cbus, node_id, to, map_id, key, obj_info->version, obj_info->obj,
        obj_info->obj_size);
    worker->send_message(to, m);

    if (obj_info->obj != nullptr)
      stats.obj_export++;

  } else {
    if (to == obj_info->cur_worker) {
      print_stack();
      assert(0);
      return;
    }

    DEBUG_OBJ("RemoteRequest: Object of " << node_id << " " << *key
                                          << " RWed BY " << obj_info->cur_worker
                                          << " add to wait " << to);
    add_to_rw_waitlist(obj_info, to);
    local_request_expire_rwref(obj_info->cur_worker, map_id, key,
                               obj_info->version);
  }
}

void SWObjectManager::delete_object_binary(ObjectInfo *obj_info, int map_id,
                                           const Key *key, int version,
                                           bool cleanup) {
  stats.own_objects--;
  deactivate_object_info(mp, obj_info);

  if (cleanup)
    cleanup_object_metainfo(obj_info, version);

  if (scaling.on && obj_info->transfer_key_ownership_to >= 0) {
    uint32_t waiters = -1;
    if (!obj_info->rw_request_queue.empty()) {
      waiters = obj_info->rw_request_queue.front();

      if (obj_info->rw_request_queue.size() >= 2) {
        DEBUG_ERR("Support single object ownership waiter in queue");
        assert(0);
      }
    }

    MessageBuffer *m = create_key_ownership_response(
        cbus, node_id, obj_info->transfer_key_ownership_to,
        key_space->get_version(), map_id, key, -1, nullptr, 0, waiters);
    worker->send_message(obj_info->transfer_key_ownership_to, m);

    erase_object_info(map_id, key);
    delete_all_obj_info(mp, obj_info);
    return;
  }

  // No object waited..
  WorkerID to = next_rw_waiter(obj_info);
  if (to < 0) {
    if (obj_info->rw_metainfo_set.empty()) {
      erase_object_info(map_id, key);
      delete_all_obj_info(mp, obj_info);
    }
    return;
  }

  DEBUG_OBJ("delete " << *key << " and created to " << to << " node_id "
                      << node_id);

  // Let next waiter created new object!
  stats.own_objects++;
  activate_object_info(obj_info);

  if (to == node_id) {
    obj_info->is_owned = true;
    obj_info->cur_worker = node_id;

    set_rwobj(map_id, key, obj_info->version, obj_info->obj_size, obj_info->obj,
              node_id);
    return;
  }

  return_obj_binary_remote(obj_info, map_id, key, to);
  return;
}

void SWObjectManager::local_request_expire_rwref(WorkerID to, int map_id,
                                                 const Key *key, int version) {
  DEBUG_OBJ("Request expire " << *key << " from " << node_id << " to " << to);

  if (to == node_id) {
    swstub_manager->request_expire_local_rwref(map_id, key, version);
  } else {
    MessageBuffer *m = create_object_ownership_expire_request(
        cbus, node_id, to, map_id, key, version);

    worker->send_message(to, m);
  }
  return;
}

int SWObjectManager::local_create_object(int map_id, const Key *key,
                                         int &version, void **obj,
                                         WorkerID &created_from,
                                         RefState &state) {
  state.in_local = true;

  DEBUG_OBJ("local request for creating object for " << *key << " from "
                                                     << node_id);

  WorkerID to = key_space->get_manager_of(map_id, key);

  if (to == -1 || to == node_id) {
    ObjectInfo *obj_info = get_object_info(map_id, key);
    if (!obj_info) {
      if (scaling.on) {
        state.in_local = false;

        WorkerID prev = key_space->get_prev_manager_of(map_id, key);

        if (to == -1 || prev == node_id) {
          obj_info = create_object_info_in_nextspace(map_id, key);
        } else {
          // If it is exist in 'prev' then pull the object.
          DEBUG_DEV("Request SW objects ownership "
                    << *key << "  remotely to " << prev << " from " << node_id);

          obj_info = create_object_info_in_nextspace(map_id, key);
          obj_info->wait_key_ownership_from = prev;

          add_to_rw_waitlist(obj_info, node_id);

          MessageBuffer *m = create_key_ownership_request(
              cbus, node_id, prev, key_space->get_version(), map_id, key);
          worker->send_message(prev, m);
        }
      } else {
        obj_info = create_object_info(map_id, key);
      }
    }

    if (scaling.on && obj_info->wait_key_ownership_from >= 0) {
      state.in_local = false;

      uint32_t obj_size;
      get_rwobj(map_id, key, version, obj_size, obj, created_from);

      if (obj_info->wait_key_ownership_from >= 0) {
        /* return object only */
        erase_object_info(map_id, key);
        delete_all_obj_info(mp, obj_info);

      } else {
        created_from = node_id;

        obj_info->is_local = true;

        stats.own_local++;
        stats.local_objects++;
      }
      return 0;
    }

    if (!obj_info->is_activate)
      activate_object_info(obj_info);

    created_from = node_id;
    return_obj_binary_local(obj_info, map_id, key, version, obj);
    stats.local_objects++;

  } else {
    state.in_local = false;

    DEBUG_DEV("Request read/write-ability " << *key << "  remotely to " << to
                                            << " from " << node_id);
    MessageBuffer *m = create_object_ownership_request(
        cbus, node_id, to, key_space->get_version(), map_id, key, true);
    worker->send_message(to, m);

    stats.borrow_local++;
    uint32_t obj_size;
    get_rwobj(map_id, key, version, obj_size, obj, created_from);

    if (created_from == node_id) {
      ObjectInfo *obj_info = get_object_info(map_id, key);
      assert(obj_info);
      obj_info->is_local = true;
    }

    stats.local_objects++;
  }

  return 0;
}

int SWObjectManager::local_lookup_object(int map_id, const Key *key,
                                         int &version, void **obj,
                                         WorkerID &created_from) {
  WorkerID to = key_space->get_manager_of(map_id, key);
  DEBUG_OBJ("local request for looking up object for " << *key << " from "
                                                       << node_id);

  // This is my object!
  if (to == -1 || to == node_id) {
    created_from = node_id;

    ObjectInfo *obj_info = get_object_info(map_id, key);
    if (!obj_info) {
      version = -1;
      *obj = nullptr;
      return 0;
    }

    // not owned by anybody
    if (!obj_info->is_owned) {
      obj_info->is_owned = true;
      obj_info->cur_worker = node_id;
      version = obj_info->version;
      *obj = obj_info->obj;
      return 0;
    } else {
      DEBUG_ERR("Do not support remote lookup");
      assert(0);

      return 0;
    }
  }

  DEBUG_DEV("Do not support remote lookup");
  assert(0);

  // Request swobj to remote SWObjectManager
  DEBUG_DEV("Request read/write-ability " << *key << "  remotely to " << to
                                          << " from " << node_id);
  MessageBuffer *m = create_object_ownership_request(
      cbus, node_id, to, key_space->get_version(), map_id, key, false);
  worker->send_message(to, m);

  uint32_t obj_size;
  get_rwobj(map_id, key, version, obj_size, obj, created_from);

  return 0;
}

int SWObjectManager::local_delete_object(int map_id, const Key *key,
                                         int version, bool cleanup,
                                         WorkerID created_from) {
  stats.local_objects--;

  WorkerID to = key_space->get_manager_of(map_id, key);
  DEBUG_OBJ("local delete object request " << *key << " from " << node_id
                                           << " to " << to << " cleanup "
                                           << cleanup);

  ObjectInfo *obj_info = get_object_info(map_id, key);
  if (obj_info) {
    stats.own_local--;
    delete_object_binary(obj_info, map_id, key, version, cleanup);
    return 0;
  }

  WorkerID send = to;

  if (to == -1 || to == node_id) {
    if (scaling.on || scaling.dmz_to_quiescent_on) {
      WorkerID prev = key_space->get_prev_manager_of(map_id, key);
      if (prev == node_id) {
        return -EINVAL;
      }

      send = prev;
    } else {
      DEBUG_ERR("No obj_info to delete " << *key);
      return -EINVAL;
    }
  }

  if (send == -1 || send == node_id) {
    DEBUG_ERR("No obj_info to delete " << *key);
    return -EINVAL;
  }

  stats.borrow_local--;
  DEBUG_DEV("RemoteRequest: delete_object (" << map_id << "," << key << ") to "
                                             << to);

  MessageBuffer *m = create_rwobj_del_request(cbus, node_id, send, map_id, key,
                                              version, cleanup);
  worker->send_message(send, m);
  return 0;
}

int SWObjectManager::local_cleanup_meta(int map_id, const Key *key, int version,
                                        WorkerID created_from) {
  WorkerID to;
  if (scaling.on) {
    to = created_from;
  } else {
    to = key_space->get_manager_of(map_id, key);
  }

  if (to == -1 || to == node_id) {
    DEBUG_DEV("LocalRequest: cleanup_meta (" << map_id << "," << key << ")");

    ObjectInfo *obj_info = get_object_info(map_id, key);
    if (!obj_info) {
      if (scaling.on) {
        WorkerID next = key_space->get_manager_of(map_id, key);
        MessageBuffer *m = create_rwobj_cleanup_meta_request(
            cbus, node_id, next, map_id, key, version);
        worker->send_message(next, m);
      } else {
        DEBUG_ERR("No obj_info to cleanup_meta");
        return -EINVAL;
      }
    }

    cleanup_object_metainfo(obj_info, version);
    if (!obj_info->is_owned && obj_info->rw_metainfo_set.empty()) {
      erase_object_info(map_id, key);
      delete_all_obj_info(mp, obj_info);
    }
  } else {
    DEBUG_DEV("RemoteRequest: cleanup_meata (" << map_id << "," << key
                                               << ") to " << to);

    MessageBuffer *m = create_rwobj_cleanup_meta_request(cbus, node_id, to,
                                                         map_id, key, version);
    worker->send_message(to, m);
  }
  return -1;
}

int SWObjectManager::local_create_cache(int map_id, const Key *key,
                                        int &version) {
  assert(!scaling.on);

  WorkerID to = key_space->get_manager_of(map_id, key);
  if (to == -1 || to == node_id) {
    ObjectInfo *obj_info = get_object_info(map_id, key);
    if (!obj_info)
      return -1;

    version = obj_info->version;
    return 0;
  } else {
    // FIXME: handling remote roref creation
    DEBUG_ERR("Cannot get remote RO reference");
  }

  return -1;
}

void SWObjectManager::remote_create_object(int map_id, const Key *key,
                                           WorkerID from_id) {
  DEBUG_OBJ("remote request for creating object for " << *key << " from "
                                                      << from_id);

  WorkerID to = key_space->get_manager_of(map_id, key);
  if (to == -1 || to == node_id) {
    ObjectInfo *obj_info = get_object_info(map_id, key);
    if (!obj_info) {
      if (scaling.on) {
        WorkerID prev = key_space->get_prev_manager_of(map_id, key);

        if (to == -1 || prev == node_id) {
          obj_info = create_object_info_in_nextspace(map_id, key);

        } else {
          DEBUG_DEV("Request SW objects ownership "
                    << *key << "  remotely to " << to << " from " << node_id);
          obj_info = create_object_info_in_nextspace(map_id, key);
          obj_info->wait_key_ownership_from = prev;

          add_to_rw_waitlist(obj_info, from_id);

          MessageBuffer *m = create_key_ownership_request(
              cbus, node_id, prev, key_space->get_version(), map_id, key);
          worker->send_message(prev, m);

          return;
        }

      } else {
        obj_info = create_object_info(map_id, key);
      }
    }

    if (!obj_info->is_activate)
      activate_object_info(obj_info);

    return_obj_binary_remote(obj_info, map_id, key, from_id);

  } else {
    if (scaling.dmz_to_scaling_on) {
      DEBUG_WRK("DMZ to scaling work: remote_create_object");

      WorkerID next = key_space->get_next_manager_of(map_id, key);
      if (next == -1 || next == node_id) {
        if (from_id == to) {
          ObjectInfo *obj_info = get_object_info(map_id, key);
          if (!obj_info)
            obj_info = create_object_info_in_nextspace(map_id, key);

          if (!obj_info->is_activate)
            activate_object_info(obj_info);

          return_obj_binary_remote(obj_info, map_id, key, from_id);
        } else {
          // forward message
          MessageBuffer *m = create_object_ownership_request(
              cbus, from_id, to, key_space->get_version(), map_id, key, true);
          worker->send_message(to, m);
        }
      } else {
        WorkerID prev = key_space->get_prev_manager_of(map_id, key);
        WorkerID next = key_space->get_next_manager_of(map_id, key);
        DEBUG_ERR("Key owner:" << prev << "->" << next);
        assert(0);
      }

    } else if (scaling.on || scaling.dmz_to_quiescent_on) {
      // forward message
      MessageBuffer *m = create_object_ownership_request(
          cbus, from_id, to, key_space->get_version(), map_id, key, true);
      worker->send_message(to, m);
    } else {
      WorkerID prev = key_space->get_prev_manager_of(map_id, key);
      WorkerID next = key_space->get_next_manager_of(map_id, key);
      DEBUG_ERR("Key owner:" << prev << "->" << next);
      assert(0);
    }
  }
  return;
}

void SWObjectManager::remote_lookup_object(int map_id, const Key *key,
                                           WorkerID from_id) {
  /* No support for remote look up */
  assert(0);
}

void SWObjectManager::remote_return_key_ownership(int map_id, const Key *key,
                                                  WorkerID from_id) {
  if (scaling.dmz_to_scaling_on) {
    DEBUG_WRK(
        "DMZ work: request key ownership - "
        "return OBJECT ownership only");

    ObjectInfo *obj_info = get_object_info(map_id, key);
    if (!obj_info) {
      obj_info = create_object_info(map_id, key);
      activate_object_info(obj_info);
    }

    return_obj_binary_remote(obj_info, map_id, key, from_id);

    return;
  }

  if (scaling.on) {
    WorkerID prev = key_space->get_prev_manager_of(map_id, key);
    WorkerID now = key_space->get_manager_of(map_id, key);

    DEBUG_OBJ("Return key ownership for " << *key << " from " << node_id
                                          << " to " << now);

    ObjectInfo *obj_info = get_object_info(map_id, key);
    if (!obj_info) {
      MessageBuffer *m = create_key_ownership_response(
          cbus, node_id, from_id, key_space->get_version(), map_id, key, -1,
          nullptr, 0, -1);
      worker->send_message(from_id, m);

      return;
    }

    assert(obj_info->is_activate);

    if (!obj_info->is_owned) {
      MessageBuffer *m = create_key_ownership_response(
          cbus, node_id, from_id, key_space->get_version(), map_id, key, -1,
          nullptr, 0, -1);
      worker->send_message(from_id, m);

      stats.own_objects--;

      erase_object_info(map_id, key);
      delete_all_obj_info(mp, obj_info);
      return;
    } else {
      DEBUG_OBJ("RemoteRequest: Key of "
                << node_id << " " << *key << " RWed BY " << obj_info->cur_worker
                << "  need to transfer to " << from_id);
      obj_info->transfer_key_ownership_to = from_id;
      local_request_expire_rwref(obj_info->cur_worker, map_id, key,
                                 obj_info->version);
      return;
    }
  }

  if (scaling.dmz_to_quiescent_on) {
    DEBUG_WRK("DMZ to quiescent work: request key ownership");

    ObjectInfo *obj_info = get_object_info(map_id, key);
    if (!obj_info) {
      MessageBuffer *m = create_key_ownership_response(
          cbus, node_id, from_id, key_space->get_version(), map_id, key, -1,
          nullptr, 0, -1);
      worker->send_message(from_id, m);
      return;
    }

    /* not reach here */
    assert(0);
    return;
  }

  /* not reach here */
  assert(0);

  return;
}

int SWObjectManager::remote_delete_object(int map_id, const Key *key,
                                          int version, bool cleanup,
                                          WorkerID from_id) {
  DEBUG_OBJ("remote delete object request " << *key << " from " << from_id
                                            << " cleanup " << cleanup);

  ObjectInfo *obj_info = get_object_info(map_id, key);
  if (obj_info) {
    stats.own_remote--;
    delete_object_binary(obj_info, map_id, key, version, cleanup);
    return 0;
  }

  WorkerID to = -1;
  if (scaling.on || scaling.dmz_to_quiescent_on) {
    to = key_space->get_manager_of(map_id, key);
    if (to == node_id)
      to = key_space->get_prev_manager_of(map_id, key);

  } else if (scaling.dmz_to_scaling_on) {
    to = key_space->get_manager_of(map_id, key);
    if (to == node_id)
      to = key_space->get_next_manager_of(map_id, key);

  } else {
    DEBUG_ERR("No obj_info to delete " << *key);
    return -EINVAL;
  }

  if (to == -1 || to == node_id) {
    DEBUG_ERR("No obj_info to delete " << *key);
    return -EINVAL;
  }

  MessageBuffer *m = create_rwobj_del_request(cbus, node_id, to, map_id, key,
                                              version, cleanup);
  worker->send_message(to, m);

  return 0;
}

int SWObjectManager::remote_cleanup_meta(int map_id, const Key *key,
                                         int version, WorkerID from_id) {
  DEBUG_DEV("Request from remote " << from_id << " : cleanup_meta (" << map_id
                                   << "," << key << ") ");

  ObjectInfo *obj_info = get_object_info(map_id, key);
  if (!obj_info) {
    if (scaling.on) {
      WorkerID next = key_space->get_manager_of(map_id, key);
      MessageBuffer *m = create_rwobj_cleanup_meta_request(
          cbus, node_id, next, map_id, key, version);
      worker->send_message(next, m);
    } else {
      DEBUG_ERR("No obj_info to delete");
      return -EINVAL;
    }
  }

  cleanup_object_metainfo(obj_info, version);
  if (!obj_info->is_owned && obj_info->rw_metainfo_set.empty()) {
    erase_object_info(map_id, key);
    delete_all_obj_info(mp, obj_info);
  }

  return -1;
}

int SWObjectManager::local_notify_expire_rwref(int map_id, const Key *key,
                                               int version, uint32_t obj_size,
                                               void *obj,
                                               WorkerID created_from) {
  stats.local_objects--;

  WorkerID to = created_from;

  DEBUG_OBJ("Local notify expire " << *key << " from " << node_id << " to "
                                   << to);

  if (to == -1 || to == node_id) {
    stats.own_local--;
    ObjectInfo *obj_info = get_object_info(map_id, key);
    if (!obj_info) {
      DEBUG_ERR("No obj_info to delete " << *key);
      return -EINVAL;
    }

    update_object_info(mp, obj_info, version, obj, obj_size);

    if (scaling.on && obj_info->transfer_key_ownership_to >= 0) {
      uint32_t waiters = -1;
      if (!obj_info->rw_request_queue.empty()) {
        waiters = obj_info->rw_request_queue.front();

        if (obj_info->rw_request_queue.size() >= 2) {
          DEBUG_ERR("Support single object ownership waiter in queue");
          assert(0);
        }
      }

      MessageBuffer *m = create_key_ownership_response(
          cbus, node_id, obj_info->transfer_key_ownership_to,
          key_space->get_version(), map_id, key, obj_info->version,
          obj_info->obj, obj_info->obj_size, waiters);
      worker->send_message(obj_info->transfer_key_ownership_to, m);

      if (obj_info->obj != nullptr)
        stats.obj_export++;

      stats.own_objects--;

      erase_object_info(map_id, key);
      delete_all_obj_info(mp, obj_info);
      return 0;
    }

    WorkerID to = next_rw_waiter(obj_info);
    if (to < 0) {
      assert(0);
    }

    if (to == node_id) {
      obj_info->is_owned = true;
      obj_info->cur_worker = node_id;

      set_rwobj(map_id, key, obj_info->version, obj_info->obj_size,
                obj_info->obj, node_id);
    } else {
      return_obj_binary_remote(obj_info, map_id, key, to);
    }

  } else {
    stats.borrow_local--;

    MessageBuffer *m = create_object_ownership_expire_response(
        cbus, node_id, created_from, map_id, key, version, obj, obj_size);
    worker->send_message(to, m);

    if (obj != nullptr)
      stats.obj_export++;
  }
  return 0;
}

void SWObjectManager::remote_set_object_ownership(int map_id, const Key *key,
                                                  int version,
                                                  uint32_t obj_size, void *data,
                                                  WorkerID created_from) {
  if (scaling.on || scaling.dmz_to_quiescent_on) {
    ObjectInfo *obj_info = get_object_info(map_id, key);
    if (obj_info && obj_info->wait_key_ownership_from >= 0) {
      DEBUG_WRK("DMZ work: key ownership is not shipped");
      erase_object_info(map_id, key);
      delete_all_obj_info(mp, obj_info);
    }
  }

  if (data != nullptr)
    stats.obj_import++;

  set_rwobj(map_id, key, version, obj_size, data, created_from);
}

int SWObjectManager::remote_notify_expire_rwref(int map_id, const Key *key,
                                                int version, uint32_t obj_size,
                                                void *obj,
                                                WorkerID created_from) {
  ObjectInfo *obj_info = get_object_info(map_id, key);
  if (!obj_info) {
    DEBUG_ERR("No obj_info to delete " << *key);
    return -EINVAL;
  }

  if (obj != nullptr)
    stats.obj_import++;

  update_object_info(mp, obj_info, version, obj, obj_size);

  if (scaling.on && obj_info->transfer_key_ownership_to >= 0) {
    uint32_t waiters = -1;
    if (!obj_info->rw_request_queue.empty()) {
      waiters = obj_info->rw_request_queue.front();

      if (obj_info->rw_request_queue.size() >= 2) {
        DEBUG_ERR("Support single object ownership waiter in queue");
        assert(0);
      }
    }

    MessageBuffer *m = create_key_ownership_response(
        cbus, node_id, obj_info->transfer_key_ownership_to,
        key_space->get_version(), map_id, key, obj_info->version, obj_info->obj,
        obj_info->obj_size, waiters);
    worker->send_message(obj_info->transfer_key_ownership_to, m);

    stats.own_objects--;

    if (obj_info->obj != nullptr)
      stats.obj_export++;

    erase_object_info(map_id, key);
    delete_all_obj_info(mp, obj_info);
    return 0;
  }

  WorkerID next = next_rw_waiter(obj_info);
  if (next < 0) {
    assert(0);
  }

  DEBUG_OBJ("Remote notify expire " << *key << " next obj owner is " << next);

  if (next == node_id) {
    obj_info->is_owned = true;
    obj_info->cur_worker = node_id;

    set_rwobj(map_id, key, obj_info->version, obj_info->obj_size, obj_info->obj,
              node_id);
  } else {
    return_obj_binary_remote(obj_info, map_id, key, next);
  }

  return 0;
}

int SWObjectManager::remote_notify_return_key_ownership(
    int map_id, const Key *key, int version, uint32_t obj_size, void *obj,
    WorkerID from_id, int waiters) {
  bool proactive = false;

  ObjectInfo *obj_info = get_object_info(map_id, key);
  if (!obj_info) {
    /* proactively transferred keys */
    proactive = true;
    obj_info = create_object_info(map_id, key);
  }

  if (!obj_info->is_activate)
    activate_object_info(obj_info);

  if (obj != nullptr) {
    update_object_info(mp, obj_info, version, obj, obj_size);
    stats.obj_import++;
  }

  obj_info->wait_key_ownership_from = -1;

  if (waiters >= 0)
    add_to_rw_waitlist(obj_info, waiters);

  WorkerID to = next_rw_waiter(obj_info);
  if (to < 0) {
    if (!proactive) {
      DEBUG_ERR("No object waiter?? " << *key << " to " << to);
      return -EINVAL;
    }
    return 0;
  }

  if (to == node_id) {
    obj_info->is_owned = true;
    obj_info->cur_worker = node_id;

    set_rwobj(map_id, key, obj_info->version, obj_info->obj_size, obj_info->obj,
              node_id);
  } else {
    return_obj_binary_remote(obj_info, map_id, key, to);
  }

  return 0;
}
