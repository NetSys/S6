#ifndef _DISTREF_SWOBJECT_MANAGER_HH_
#define _DISTREF_SWOBJECT_MANAGER_HH_

#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "key.hh"
#include "log.hh"
#include "type.hh"

class Worker;
class KeySpace;
class DroutineScheduler;
class ControlBus;
class Connector;
class SwStubManager;
class MessageBuffer;

struct ObjectInfo;
struct ObjReturn;
struct RefState;

typedef std::unordered_map<const Key *, ObjectInfo *, _dr_key_hash,
                           _dr_key_equal_to>
    ObjInfoMap;

/*
   Map between key and istance_id in charge of the key
   The worker is charge of
  1. Who have the object now (which has RW right)
  2. Who is request the RW access next

  This mapping should be strongly consistent between object
  This mapping is expected to be stable during normal case
    - exception: scaling will change the table contents
 */

class SWObjectManager {
 private:
  int ADT_cnt = _MAX_DMAPS;
  ObjInfoMap *obj_map_arr[_MAX_DMAPS] = {0};
  ObjInfoMap *tmp_obj_map_arr[_MAX_DMAPS] = {0}; /* using during scaling */

  std::unordered_map<const Key *, ObjReturn *, _dr_key_hash, _dr_key_equal_to>
      object_ret_map[_MAX_DMAPS];

  WorkerID node_id;

  DroutineScheduler *scheduler;  // pointer to yield and idle
  SwStubManager *swstub_manager = nullptr;
  KeySpace *key_space = nullptr;
  MemPool *mp = nullptr;

  struct {
    bool dmz_to_scaling_on = false;
    bool dmz_to_quiescent_on = false;
    bool on = false;
  } scaling;

  // XXX Hope to remove
  ControlBus *cbus;
  Worker *worker = nullptr;

  struct {
    uint32_t counter;

    /* without scaling */
    uint32_t own_objects;   /* number of owned objects */
    uint32_t own_local;     /* owned locally */
    uint32_t own_remote;    /* owned remotely */
    uint32_t local_objects; /* number of objects in local */
    uint32_t borrow_local;  /* borrowed remotely */

    /* scaling */
    uint32_t own_objects_stale; /* own objects in old map */
    uint32_t own_objects_new;   /* own objects in new map */

    uint32_t obj_export;
    uint32_t obj_import;

  } stats;

  ObjectInfo *get_object_info(int map_id, const Key *key);
  ObjectInfo *create_object_info(int map_id, const Key *key);
  void erase_object_info(int map_id, const Key *key);
  void local_request_expire_rwref(WorkerID to, int map_id, const Key *key,
                                  int version);
  void obj_map_clean_up();

  /* for scaling */
  ObjectInfo *create_object_info_in_nextspace(int map_id, const Key *key);
  void move_object_info_to_nextspace(int map_id, const Key *key);

  void return_obj_binary_local(ObjectInfo *obj_info, int map_id, const Key *key,
                               int &version, void **obj);
  void return_obj_binary_remote(ObjectInfo *obj_info, int map_id,
                                const Key *key, WorkerID from_id);
  void delete_object_binary(ObjectInfo *obj_info, int map_id, const Key *key,
                            int version, bool cleanup);

  // might blocking until satisyfing wake-up condition by set_rwobj()
  void get_rwobj(int map_id, const Key *key, int &version, uint32_t &obj_size,
                 void **data, WorkerID &created_from);

  // satisfying wake-up condition locally/remotely
  // previously blocked by get_rwobj()
  void set_rwobj(int map_id, const Key *key, int version, uint32_t obj_size,
                 void *data, WorkerID created_from);

 public:
  SWObjectManager(uint32_t node_id, Worker *worker, DroutineScheduler *sch,
                  ControlBus *cbus, KeySpace *key_space, MemPool *mp) {
    this->node_id = node_id;

    this->scheduler = sch;
    this->key_space = key_space;

    this->worker = worker;
    this->cbus = cbus;
    this->mp = mp;
  }
  ~SWObjectManager() {}

  void set_keyspace(KeySpace *key_space) { this->key_space = key_space; }

  void set_swstub_manager(SwStubManager *swstub_manager) {
    this->swstub_manager = swstub_manager;
  }

  void set_dmz_to_scaling_on() { this->scaling.dmz_to_scaling_on = true; }

  void set_dmz_to_scaling_off() { this->scaling.dmz_to_scaling_on = false; }

  void set_dmz_to_quiescent_on() { this->scaling.dmz_to_quiescent_on = true; }

  void set_dmz_to_quiescent_off() { this->scaling.dmz_to_quiescent_on = false; }

  void set_scaling_on() { this->scaling.on = true; }

  void set_scaling_off() {
    this->scaling.on = false;

    for (int i = 0; i < _MAX_DMAPS; i++) {
      obj_map_arr[i] = tmp_obj_map_arr[i];
      tmp_obj_map_arr[i] = nullptr;
    }

    stats.own_objects_stale = stats.own_objects_new;
    stats.own_objects_new = 0;
  }

  bool check_scaling_done() { return (stats.own_objects_stale == 0); }

  void print_object_stats();
  int force_scaling(int max_objects);

  void teardown(bool force);

  // Called by application thread: May call yield()
  int local_create_object(int map_id, const Key *key, int &version, void **obj,
                          WorkerID &created_from, RefState &state);
  int local_lookup_object(int map_id, const Key *key, int &version, void **obj,
                          WorkerID &created_from);
  int local_delete_object(int map_id, const Key *key, int version, bool cleanup,
                          WorkerID created_from);
  int local_cleanup_meta(int map_id, const Key *key, int version,
                         WorkerID created_from);
  int local_create_cache(int map_id, const Key *key, int &version);
  int local_notify_expire_rwref(int map_id, const Key *key, int version,
                                uint32_t obj_size, void *obj,
                                WorkerID created_from);

  // Called by control network thread
  void remote_create_object(int map_id, const Key *key, WorkerID from_id);
  void remote_lookup_object(int map_id, const Key *key, WorkerID from_id);
  void remote_return_key_ownership(int map_id, const Key *key,
                                   WorkerID from_id);
  int remote_delete_object(int map_id, const Key *key, int version,
                           bool cleanup, WorkerID from_id);
  int remote_cleanup_meta(int map_id, const Key *key, int version,
                          WorkerID from_id);

  void remote_set_object_ownership(int map_id, const Key *key, int version,
                                   uint32_t obj_size, void *data,
                                   WorkerID created_from);
  int remote_notify_expire_rwref(int map_id, const Key *key, int version,
                                 uint32_t obj_size, void *obj,
                                 WorkerID created_from);
  int remote_notify_return_key_ownership(int map_id, const Key *key,
                                         int version, uint32_t obj_size,
                                         void *obj, WorkerID from_id,
                                         int waiters);
};

#endif /* _DISTREF_SWOBJECT_MANAGER_HH_ */
