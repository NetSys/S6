#ifndef _DISTREF_SWSTUB_MANAGER_HH_
#define _DISTREF_SWSTUB_MANAGER_HH_

#include <cstdint>
#include <queue>
#include <unordered_map>

#include "key.hh"
#include "mem_pool.hh"
#include "swobj_manager.hh"
#include "type.hh"

class SwStubBase;

struct SwStubInfo;     // RW Reference for single-writable objects
struct SwStubROInfo;   // RO Reference for single-writable objects
struct SWDeadObjInfo;  // Deleted object, but with RO refs

class SWObjectManager;
class DroutineScheduler;

struct RefState;

typedef std::unordered_map<const Key *, SwStubInfo *, _dr_key_hash,
                           _dr_key_equal_to>
    SwStubMap;

typedef std::unordered_map<const Key *, SwStubROInfo *, _dr_key_hash,
                           _dr_key_equal_to>
    SwStubROMap;
typedef std::unordered_map<int, SWDeadObjInfo *> SWDeadObjMap;
typedef std::unordered_map<const Key *, SWDeadObjMap, _dr_key_hash,
                           _dr_key_equal_to>
    SWDeadObjTable;

class SwStubManager {
  int ADT_cnt = _MAX_DMAPS;
  SwStubMap swstub_rw_map_arr[_MAX_DMAPS];
  SwStubROMap swstub_ro_map_arr[_MAX_DMAPS];
  SWDeadObjTable deadobj_table_arr[_MAX_DMAPS];

  DroutineScheduler *scheduler;
  SWObjectManager *swobj_manager = nullptr;
  MemPool *mp = nullptr;

  /* handle 'swstub info' for rw */
  SwStubInfo *get_swstub_info(int map_id, const Key *key);
  SwStubInfo *create_swstub_info(int map_id, const Key *key);
  // erase swstub_info from swstub_map, not delete itself
  void erase_swstub_info(int map_id, const Key *key, int version);

  /* handle 'swstub info' for ro */
  SwStubROInfo *get_swstub_ro_info(int map_id, const Key *key);
  SwStubROInfo *get_swstub_ro_info(int map_id, const Key *key, int version);
  SwStubROInfo *create_swstub_ro_info(int map_id, const Key *key);
  // erase from swstub_ro_map, not delete itself
  void erase_swstub_ro_info(int map_id, const Key *key, int version);

  /* handle 'deadobj info' */
  SWDeadObjInfo *get_deadobj_info(int map_id, const Key *key, int version);
  void insert_deadobj_info(int map_id, const Key *key,
                           SWDeadObjInfo *deadobj_info);
  // erase swstub_info from swstub_map, not delete itself
  void erase_deadobj_info(int map_id, const Key *key, int version);

  /* dealing 'swstub' */
  SwStubBase *create_rwref(int map_id, const Key *key, int &version,
                           RefState &state, WorkerID &created_from);
  SwStubBase *lookup_rwref(int map_id, const Key *key, int &version,
                           WorkerID &created_from);
  int delete_rwref(int map_id, const Key *key, SwStubInfo *swstub_info);
  SwStubBase *create_roref(int map_id, const Key *key, int &version);
  int delete_roref(int map_id, const Key *key, SwStubROInfo *swstub_info);

  void execute_rpc(int map_id, const Key *key, int version, uint32_t flag,
                   uint32_t method_id, void *args, uint32_t args_size,
                   void *ret, uint32_t ret_size);

 public:
  SwStubManager(DroutineScheduler *sch, MemPool *mp) : scheduler(sch), mp(mp){};
  ~SwStubManager(){};

  void set_swobj_manager(SWObjectManager *swobj_manager) {
    this->swobj_manager = swobj_manager;
  }

  void teardown(bool force);

  // swstub status checking
  bool is_obj_alive(int map_id, const Key *key, int version);
  bool is_ro_accessible(int map_id, const Key *key, int version);

  void *malloc(size_t size) { return mp->malloc(size); }
  void free(void *m) { return mp->free(m); }

  // Called locally
  SwStubBase *create(int map_id, const Key *key,
                     RefState &state);             // can be blocked
  SwStubBase *get(int map_id, const Key *key);     // can be blocked
  SwStubBase *lookup(int map_id, const Key *key);  // can be blocked
  int release(int map_id, const Key *key, int version);
  void delete_object(int map_id, const Key *key, int version);

  SwStubBase *lookup_cache(int map_id, const Key *key);  // can be blocked
  int release_cache(int map_id, const Key *key, int version);

  // Called locally or remotely
  int request_expire_local_rwref(int map_id, const Key *key, int version);

  void request_rpc(int map_id, const Key *key, int version, uint32_t flag,
                   uint32_t method_id, void *args, uint32_t args_size,
                   void *ret, uint32_t ret_size);
};

#endif /* _DISTREF_SWHOOK_MANAGER_HH_ */
