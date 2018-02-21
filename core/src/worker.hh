#ifndef _DISTREF_WORKER_HH_
#define _DISTREF_WORKER_HH_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <queue>
#include <unordered_map>

#include "application.hh"
#include "mem_pool.hh"
#include "message.hh"
#include "worker_config.hh"

#define NUM_CO_ROUTINES 65536

class DroutineScheduler;
class Connector;
class ControlBus;
class KeySpace;

class ReferenceInterceptor;
class ReferenceManager;
class SwStubManager;
class SWObjectManager;
class MwStubManager;
class MWSkeletonPair;

enum WorkerState : uint8_t {
  WORKER_ST_NORMAL,
  WORKER_ST_PREPARE_SCALING,
  WORKER_ST_DOING_SCALING,
  WORKER_ST_WAIT_SCALING_TEARDOWN
};

/* Fake worker identifier */
class Worker {
 private:
  WorkerConfig *wconf;

  // during scaling process, maintain two set of configurations
  ActiveWorkers *active_workers;
  Application *app;

  struct {
    bool reserve_quit;
    bool run_scheduler;
    bool remote_serving;
  } status;
  struct {
    uint64_t time;
    int id;
  } bgf;

  ControlBus *cbus;
  Connector *state_sock;
  int mng_sock;

  KeySpace *key_space;
  MemPool *mp_sw;
  MemPool *mp_mw;
  DroutineScheduler *scheduler;
  ReferenceInterceptor *ref_interceptor;
  SWObjectManager *swobj_manager;
  SwStubManager *swstub_manager;
  MwStubManager *mwstub_manager;

  /* scailng-related variables */
  int working_state = WORKER_ST_NORMAL;
  struct {
    uint32_t tobe_export_flow_cnt = 0;
    uint32_t tobe_import_flow_cnt = 0;
    uint32_t complete_export_flow_cnt = 0;
    uint32_t complete_import_flow_cnt = 0;
  } stats;
  bool force_scaling_completed = false;

  Worker(const Worker &me);

  bool check_state_channel_connectivity();

  int connect_controller();
  int wait_to_be_all_ready();
  void wait_to_finish();

  void send_msg_to_controller(const char *msg_type);
  void notify_ready();
  void notify_run();
  void notify_prepared_scaling();
  void notify_started_scaling();
  void notify_prepared_normal();
  void notify_completed_scaling();
  void notify_being_normal();
  void notify_teared_down();

  void teardown(bool force);

  void process_scaling(ActiveWorkers *ac);
  void transfer_objects(int map_id, WorkerID to, uint32_t pair_count,
                        MWSkeletonPair *pairs);

  int process_state_plane(uint32_t max_msg);
  void process_state_packet(MessageBuffer *packet);
  void process_command_from_controller();

  void process_ping(MsgPing *ping, WorkerID from);
  void process_pong(MsgPong *pong, WorkerID from);
  void process_mw_rpc_request(RPCRequest *r, WorkerID from);
  void process_mw_rpc_request_multi_asym(RPCRequestMultiAsym *r, WorkerID from);
  void process_mw_rpc_request_multi_sym(RPCRequestMultiSym *r, WorkerID from);
  void process_mw_rpc_response(RPCResponse *r);
  void process_mw_aggr_request(MWAggrRequest *r);
  void process_rwlease_request(RWLeaseRequest *r, WorkerID from);
  void process_rwlease_response(RWLeaseResponse *r, WorkerID from);
  void process_rwlease_expire_request(RWLeaseExpireRequest *r, WorkerID from);
  void process_rwlease_expire_response(RWLeaseExpireResponse *r);
  void process_key_request(RWKeyRequest *r, WorkerID from);
  void process_key_response(RWKeyResponse *r, WorkerID from);
  void process_rw_del_request(RWDeleteRequest *r, WorkerID from);
  void process_rw_del_response(RWDeleteResponse *r);
  void process_rw_cleanup_meta_request(RWCleanupMetaRequest *r, WorkerID from);
  void process_skeleton_stream(SkeletonStream *s, WorkerID from);

 public:
  Worker(WorkerConfig *wconf, bool is_dpdk, ControlBus *cbus);
  ~Worker();

  bool operator==(const Worker &other) const;

  WorkerID get_id() const;
  WorkerAddress get_address() const;

  void reserve_quit();
  void set_remote_serving();
  void set_active_workers(ActiveWorkers *ac);
  void set_keyspace(KeySpace *key_space);
  void set_application(Application *app, WorkerType w_type);

  // TODO Only for tests -- Should be removed
  void set_num_packets_to_proc(int cnt);
  int set_routine_function(packet_func cb);
  int set_single_function(background_func sf);

  void run_single_function(int fid);  // XXX currently ignored fid
  void run_single_function_after_us(int fid, useconds_t us);

  void run(bool connect_controller = true);

  // access from swobj_manager, mwref_manager
  void send_message(WorkerID to, MessageBuffer *m);
};

namespace std {

template <>
struct hash<Worker> {
  std::size_t operator()(const Worker &k) const {
    return std::hash<WorkerID>()(k.get_id());
  }
};
}

#endif /* _DISTREF_WORKER_HH_ */
