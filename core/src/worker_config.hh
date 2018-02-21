#ifndef _DISTREF_WORKER_CONFIG_HH_
#define _DISTREF_WORKER_CONFIG_HH_

#include "worker_address.hh"

#define MAX_PWORKER_CNT 16
#define MAX_BGWORKER_CNT 16
#define MAX_WORKER_CNT (MAX_PWORKER_CNT + MAX_BGWORKER_CNT)

/* Configuration for initializing new workers */
struct WorkerConfig {
  WorkerID id;
  uint32_t node_id;
  WorkerType type;

  WorkerAddress *state_addr;
  WorkerAddress *mng_addr;
  int function_id;  // in case of background workers
  char *log_fld = nullptr;

  int max_swobj_size;
  int max_expected_flows;
  int max_mwobj_size;
  int max_expected_shared_objs;

  bool pong_received_from[MAX_WORKER_CNT];
};

struct ActiveWorkers {
  int pworker_cnt;
  int bgworker_cnt;
  WorkerAddress state_addrs[MAX_WORKER_CNT];
};

#endif
