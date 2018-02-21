#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <queue>
#include <sys/socket.h>
#include <sys/un.h>

#include <rte_cycles.h>

#include "controlbus.hh"
#include "d_routine.hh"
#include "key_space.hh"
#include "mw_skeleton.hh"
#include "rapidjson/document.h"
#include "reference_interceptor.hh"
#include "swobj_manager.hh"
#include "time.hh"
#include "worker.hh"
#include "worker_address.hh"

#define UNIX_PATH_MAX 108
#define BUFFSIZE 1024

using namespace rapidjson;

static LBPolicy *get_policy(rapidjson::Document &d) {
  // XXX Need to add src/dst ip/port semantic
  int offset = d["rules"]["offset"].GetInt();
  int size = d["rules"]["size"].GetInt();

  uint8_t sip_mlen = 0;
  uint8_t dip_mlen = 0;
  uint8_t sp_mlen = 0;
  uint8_t dp_mlen = 0;

  if (offset == 26) {  // sip starts
    if (size > 4) {
      sip_mlen = 4;
      dip_mlen = size - 4;
    } else
      sip_mlen = size;
  } else if (offset == 30) {  // dip starts
    dip_mlen = size;
  } else
    assert(0);

  DEBUG_DEV(
      "rule for load balancing masking for src/dst ip and "
      "src/dst port "
      << static_cast<int>(sip_mlen) << ":" << static_cast<int>(dip_mlen) << ":"
      << static_cast<int>(sp_mlen) << ":" << static_cast<int>(dp_mlen));

  LBPolicy *p = new LBPolicy();
  p->set_rule(LBRule(sip_mlen, dip_mlen, sp_mlen, dp_mlen));
  return p;
}

static ActiveWorkers *get_active_workers(rapidjson::Document &d) {
  // Create config
  ActiveWorkers *active_workers = new ActiveWorkers();
  active_workers->pworker_cnt = d["workers"]["pworker_count"].GetInt();
  active_workers->bgworker_cnt = d["workers"]["bgworker_count"].GetInt();
  DEBUG_DEV("number of pworkers " << active_workers->pworker_cnt);
  DEBUG_DEV("number of bgworkers " << active_workers->bgworker_cnt);

  const Value &winfos = d["workers"]["worker_infos"];
  for (SizeType i = 0; i < winfos.Size(); i++) {
    WorkerID wid = winfos[i]["node_id"].GetInt();
    active_workers->state_addrs[wid] = *WorkerAddress::CreateWorkerAddress(
        winfos[i]["state_port"].GetString());
    DEBUG_DEV("worker_info of " << winfos[i]["node_id"].GetInt() << " "
                                << winfos[i]["state_port"].GetString());
  }

  return active_workers;
}

static int update_keyspace(rapidjson::Document &d, KeySpace *key_space,
                           int node_cnt) {
  if (!d.HasMember("keyspace"))
    return -1;

  int version = key_space->update_next_version();
  if (version < 0)
    return -1;

  key_space->set_node_cnt(version, node_cnt);

  const Value &default_rule = d["keyspace"]["default_rule"];
  if (!default_rule.IsNull()) {
    LocalityType loc_type =
        static_cast<LocalityType>(default_rule["loc_type"].GetInt());
    int param = default_rule["param"].GetInt();
    key_space->set_rule(version, loc_type, param);
  }

  const Value &rules = d["keyspace"]["map_rules"];
  for (SizeType i = 0; i < rules.Size(); i++) {
    int map_id = rules[i]["map_id"].GetInt();
    LocalityType loc_type =
        static_cast<LocalityType>(rules[i]["loc_type"].GetInt());
    int param = rules[i]["param"].GetInt();
    key_space->set_rule(version, map_id, loc_type, param);
  }

  DEBUG_DEV("Update KeySpace: " << version);

  return 0;
}

/* public functions */
Worker::Worker(WorkerConfig *wconf, bool is_dpdk, ControlBus *cbus) {
  if (cbus == nullptr) {
    this->state_sock = nullptr;
  } else {
    this->state_sock = cbus->register_address(*wconf->state_addr);
    if (this->state_sock)
      DEBUG_WRK("Listen on " << wconf->state_addr);
    else
      DEBUG_ERR("Fail to listen on " << wconf->state_addr);
  }

  this->cbus = cbus;
  this->wconf = wconf;
  this->ref_interceptor = ReferenceInterceptor::GetReferenceInterceptor();
  this->scheduler = new DroutineScheduler(wconf->id, is_dpdk, wconf->log_fld);

  this->key_space = new KeySpace();

  if (wconf->max_swobj_size <= 0)
    this->mp_sw = new MemPool();
  else
    this->mp_sw = new MemPool(wconf->max_swobj_size, wconf->max_expected_flows);

  if (wconf->max_mwobj_size <= 0)
    this->mp_mw = new MemPool();
  else
    this->mp_mw =
        new MemPool(wconf->max_mwobj_size, wconf->max_expected_shared_objs);

  this->swobj_manager = new SWObjectManager(wconf->node_id, this, scheduler,
                                            cbus, key_space, this->mp_sw);
  this->swstub_manager = new SwStubManager(scheduler, this->mp_sw);

  this->mwstub_manager = new MwStubManager(wconf->node_id, this, scheduler,
                                           cbus, key_space, this->mp_mw);

  this->swstub_manager->set_swobj_manager(swobj_manager);
  this->swobj_manager->set_swstub_manager(swstub_manager);

  this->ref_interceptor->set_managers(swstub_manager, mwstub_manager);

  this->status = {false, false};
  this->bgf = {0, 0};
}

Worker::~Worker() {
  delete state_sock;  // XXX control bus interface is not goood
  delete mp_sw;
  delete mp_mw;

  delete swobj_manager;
  delete swstub_manager;
  delete mwstub_manager;
  delete scheduler;
}

WorkerID Worker::get_id() const {
  return wconf->id;
}

WorkerAddress Worker::get_address() const {
  return *wconf->state_addr;
}

void Worker::reserve_quit() {
  status.reserve_quit = true;
  status.remote_serving = false;
  DEBUG_WRK("Worker " << wconf->id << " is set to stop. ");
}

void Worker::set_remote_serving() {
  status.remote_serving = true;
}

void Worker::set_active_workers(ActiveWorkers *ac) {
  this->active_workers = ac;
}

void Worker::set_keyspace(KeySpace *key_space) {
  if (this->key_space)
    delete this->key_space;

  this->key_space = key_space;
  swobj_manager->set_keyspace(key_space);
  mwstub_manager->set_keyspace(key_space);
}

void Worker::set_application(Application *app, WorkerType w_type) {
  this->app = app;
  if (w_type == PACKET_WORKER)
    scheduler->set_packet_routine(app->get_packet_func(), NUM_CO_ROUTINES);

  // FIXME: multiple get background function
  scheduler->set_background_routine(app->get_background_func(0));
  return;
}

int Worker::set_routine_function(packet_func packet_processing) {
  scheduler->set_packet_routine(packet_processing, NUM_CO_ROUTINES);
  return 0;
}

int Worker::set_single_function(background_func single_function) {
  scheduler->set_background_routine(single_function);
  return 0;
}

// XXX fid is ignored
void Worker::run_single_function(int fid) {
  scheduler->run_background_routine();
  status.run_scheduler = true;
}

void Worker::run_single_function_after_us(int fid, useconds_t us) {
  bgf.time = get_cur_rdtsc(true) + get_tsc_freq() * (us / 1.0E+6);
  bgf.id = fid;
}

void Worker::set_num_packets_to_proc(int cnt) {
  scheduler->set_max_new_packets(cnt);
};

void Worker::send_message(WorkerID to, MessageBuffer *mb) {
  WorkerAddress waddr = active_workers->state_addrs[to];
  if (waddr == 0) {
    DEBUG_ERR("Address is unknown " << to);
    exit(EXIT_FAILURE);
  }

#ifdef D_DEV
  Message *m = (Message *)mb->get_message_body();
  DEBUG_DEV("[send_message] from " << m->from_id << " to " << m->to_id << ": "
                                   << m->mtype);
#endif

  static const uint64_t hz = get_tsc_freq();
  static uint64_t max_diff_tsc = 0;

  uint64_t cur_tsc = get_cur_rdtsc(true);

  bool success = state_sock->send(mb, waddr);
  while (!success) {
    // XXX worst_case: stack can grow infinitely
    process_state_plane(1);
    success = state_sock->send(mb, waddr);
  }

  uint64_t last_tsc = get_cur_rdtsc(true);

  if (max_diff_tsc < last_tsc - cur_tsc) {
    max_diff_tsc = last_tsc - cur_tsc;
    double t = max_diff_tsc / (double)hz * 1000;  // ms

    DEBUG_DEV("[send_message] new max time on " << t << " ms");
  }
}

void Worker::teardown(bool force) {
  swstub_manager->teardown(force);
  swobj_manager->teardown(force);
  mwstub_manager->teardown(force);
}

void Worker::transfer_objects(int map_id, WorkerID to, uint32_t pair_count,
                              MWSkeletonPair *pairs) {
  MessageBuffer *mb;

  if (pair_count == 0) {
    // Send nothing to transfer
    int msg_size = sizeof(Message) + sizeof(SkeletonStream);

    mb = cbus->allocate_message(msg_size);
    Message *m = (Message *)mb->get_message_body();
    m->mtype = MSG_MW_SKELETON_STREAM;
    m->from_id = wconf->node_id;
    m->to_id = to;

    SkeletonStream *ss = (SkeletonStream *)(void *)m->buf;
    ss->map_id = map_id;
    ss->obj_count = pair_count;
    ss->key_size = 0;
    ss->buf_size = 0;

  } else {
    DEBUG_ERR("transfer " << map_id << " from " << wconf->node_id << " to "
                          << to << " total items " << pair_count);

    // Create a message
    int key_size = pairs[0].key->get_key_size();
    int tot_obj_size = 0;
    for (int i = 0; i < (int)pair_count; i++) {
      tot_obj_size += pairs[i].obj_size;
    }

    int msg_size = sizeof(Message) + sizeof(SkeletonStream) +
                   key_size * pair_count + sizeof(uint32_t) * pair_count +
                   tot_obj_size;

    mb = cbus->allocate_message(msg_size);
    Message *m = (Message *)mb->get_message_body();
    m->mtype = MSG_MW_SKELETON_STREAM;
    m->from_id = wconf->node_id;
    m->to_id = to;

    SkeletonStream *ss = (SkeletonStream *)(void *)m->buf;
    ss->map_id = map_id;
    ss->obj_count = pair_count;
    ss->key_size = key_size;
    ss->buf_size = msg_size - (sizeof(Message) + sizeof(SkeletonStream));

    int offset = 0;
    for (int i = 0; i < (int)pair_count; i++) {
      memcpy(ss->buf + offset, pairs[i].key->get_bytes(), key_size);
      offset += key_size;
      memcpy(ss->buf + offset, &pairs[i].obj_size, sizeof(pairs[i].obj_size));
      offset += sizeof(pairs[i].obj_size);
      memcpy(ss->buf + offset, pairs[i].obj_ref->get_bytes(),
             pairs[i].obj_size);
      offset += pairs[i].obj_size;

      delete pairs[i].key;
      delete pairs[i].obj_ref;
    }

    delete pairs;
  }

  send_message(to, mb);

  return;
}

void Worker::send_msg_to_controller(const char *msg_type) {
  int nbytes;
  char buffer[BUFFSIZE];

  // send migration done message to controller
  nbytes =
      snprintf(buffer + 4, BUFFSIZE - 4,
               "{\"msg_type\":\"%s\", \"worker_id\": %d}", msg_type, wconf->id);

  char len_str[5];
  snprintf(len_str, 5, "%04u", nbytes + 5);
  memcpy(buffer, len_str, 4);
  int ret = write(mng_sock, buffer, nbytes + 5);
  if (ret != nbytes + 5) {
    DEBUG_ERR("Fail to sent " << nbytes + 5 << " actual sent " << ret);
    return;
  }
}

void Worker::notify_ready() {
  send_msg_to_controller("ready");
}

void Worker::notify_run() {
  send_msg_to_controller("run");
}

void Worker::notify_prepared_scaling() {
  send_msg_to_controller("prepared_scaling");
  DEBUG_WRK("Worker " << wconf->id << " prepared scaling");
}

void Worker::notify_started_scaling() {
  send_msg_to_controller("started_scaling");
  DEBUG_WRK("Worker " << wconf->id << " started scaling");
}

void Worker::notify_prepared_normal() {
  send_msg_to_controller("prepared_normal");
  DEBUG_WRK("Worker " << wconf->id << " prepared normal");
}

void Worker::notify_being_normal() {
  send_msg_to_controller("being_normal");
  DEBUG_WRK("Worker " << wconf->id << " being normal");
}

void Worker::notify_completed_scaling() {
  working_state = WORKER_ST_WAIT_SCALING_TEARDOWN;

  send_msg_to_controller("completed_scaling");
  DEBUG_WRK("Worker " << wconf->id << " completed scaling");
}

void Worker::notify_teared_down() {
  DEBUG_WRK("Worker " << wconf->id << " is ready to tear down");
  send_msg_to_controller("teared_down");
}

bool Worker::check_state_channel_connectivity() {
  for (int i = 0; i < active_workers->pworker_cnt; i++) {
    WorkerID to = i;  // FIXME
    if (wconf->pong_received_from[to] == false) {
      return false;
    }
  }

  for (int i = 0; i < active_workers->bgworker_cnt; i++) {
    WorkerID to = i + MAX_PWORKER_CNT;  // FIXME
    if (wconf->pong_received_from[to] == false) {
      return false;
    }
  }

  return true;
}

int Worker::connect_controller() {
  int nbytes;
  char buffer[BUFFSIZE];
  rapidjson::Document d;

  // create socket and connect
  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = wconf->mng_addr->get_port(),
      .sin_addr = {.s_addr = wconf->mng_addr->get_ip_addr()},
  };

  mng_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  assert(mng_sock >= 0);

  void *addr_p = &addr;
  int ret =
      connect(mng_sock, reinterpret_cast<sockaddr *>(addr_p), sizeof(addr));
  if (ret != 0) {
    perror("connect");
    DEBUG_ERR("connect() failed to " << wconf->mng_addr->str());
    return -1;
  }

  // say hello to controller
  nbytes = snprintf(buffer + 4, BUFFSIZE - 4,
                    "{\"msg_type\":\"hello\", \"worker_id\": %d}", wconf->id);
  char len_str[5];
  snprintf(len_str, 5, "%04u", nbytes + 5);
  memcpy(buffer, len_str, 4);
  ret = send(mng_sock, buffer, nbytes + 5, 0);
  if (ret != nbytes + 5) {
    DEBUG_ERR("Fail to sent " << nbytes + 5 << " actual sent " << ret);
    return -1;
  }

  // get policy and config from controller
  nbytes = recv(mng_sock, buffer, sizeof(buffer), 0);
  if (nbytes != atoi(buffer)) {
    DEBUG_ERR("received " << nbytes << " msg_size is " << atoi(buffer) << " ["
                          << buffer + 4 << "] ");
    return -1;
  }

  // Get rule and worker infos
  d.ParseInsitu<0>(buffer + 4);
  if (!d.HasMember("msg_type")) {
    DEBUG_ERR("[mng_channel] no 'msg_type' is specified. " << buffer + 4);
    return -1;
  }

  const char *msg_type = d["msg_type"].GetString();
  if (strncmp(msg_type, "init_rule", strlen(msg_type)) != 0) {
    DEBUG_ERR("[mng_channel] message_type  is not 'init_rule'. " << buffer + 4);
    return -1;
  }

  LBPolicy *p = get_policy(d);
  if (!p)
    return -1;

  SticKey::set_LBPolicy(p);

  active_workers = get_active_workers(d);
  if (!active_workers) {
    DEBUG_ERR("No configuration.");
    return -1;
  }

  ret = update_keyspace(d, key_space, active_workers->pworker_cnt);
  if (ret < 0) {
    DEBUG_ERR("No keyspace to update.");
    return -1;
  }

  ret = key_space->activate_next_version();
  if (ret < 0) {
    DEBUG_ERR("Fail to activate key_space");
    return -1;
  }

  DEBUG_WRK("Worker " << wconf->id << " initializes rules.");

  return 0;
}

int Worker::wait_to_be_all_ready() {
  int nbytes;
  char buffer[BUFFSIZE];
  rapidjson::Document d;

  nbytes = recv(mng_sock, buffer, sizeof(buffer), 0);
  if (nbytes <= 0)
    return -1;

  if (nbytes != atoi(buffer)) {
    DEBUG_ERR("fail to receive msg bytes in buffer: " << nbytes << " received "
                                                      << atoi(buffer) << " [ "
                                                      << buffer << " ] ");
    return -1;
  }

  d.ParseInsitu<0>(buffer + 4);
  if (!d.HasMember("msg_type"))
    return -1;

  const char *msg_type = d["msg_type"].GetString();
  if (strncmp(msg_type, "all_ready", strlen(msg_type)) != 0) {
    DEBUG_ERR("[mng_channel] message_type  is not 'all_ready'. " << buffer + 4);
    return -1;
  }

  DEBUG_WRK("Workers all ready");

  return 0;
}

void Worker::wait_to_finish() {
  int nbytes = 0;
  char buffer[BUFFSIZE];
  rapidjson::Document d;

  while (nbytes <= 0) {
    // it is set to non-blocking
    nbytes = recv(mng_sock, buffer, sizeof(buffer), MSG_NOSIGNAL | MSG_WAITALL);
    if (nbytes <= 0) {
      if (errno != EINTR && errno != EAGAIN) {
        DEBUG_ERR("Mng connection failed");
        return;
      }
    } else {
      if (nbytes != atoi(buffer)) {
        DEBUG_ERR("fail to receive msg bytes in buffer: "
                  << nbytes << " received " << atoi(buffer) << " [ " << buffer
                  << " ] ");
        return;
      }

      d.ParseInsitu<0>(buffer + 4);
      int fid = d["bg_fid"].GetInt();
      if (fid >= 0) {
        DEBUG_WRK("Worker start background function " << fid);
        run_single_function(fid);
        scheduler->call_scheduler();
      }
    }
  }
  close(mng_sock);
}

void Worker::process_command_from_controller() {
  int nbytes;
  char buffer[BUFFSIZE];
  rapidjson::Document d;

  nbytes = recv(mng_sock, buffer, sizeof(buffer), MSG_NOSIGNAL | MSG_DONTWAIT);
  if (nbytes <= 0) {
    if (errno != EINTR && errno != EAGAIN) {
      DEBUG_ERR("Mng connection failed\n");
      exit(EXIT_FAILURE);
    }
    return;
  }

  if (nbytes != atoi(buffer)) {
    DEBUG_ERR("fail to receive msg bytes in buffer: " << nbytes << " received "
                                                      << atoi(buffer) << " [ "
                                                      << buffer << " ] ");
    return;
  }

  d.ParseInsitu<0>(buffer + 4);

  if (d.HasMember("msg_type")) {
    const char *msg_type = d["msg_type"].GetString();

    if (strncmp(msg_type, "prepare_scaling", strlen(msg_type)) == 0) {
      DEBUG_WRK("Worker " << wconf->id << " prepare scaling");

      // update new worker information
      // FIXME: mark active/inactive worker

      ActiveWorkers *new_active_workers = get_active_workers(d);
      if (new_active_workers->pworker_cnt > active_workers->pworker_cnt) {
        active_workers = new_active_workers;
      } else
        active_workers->pworker_cnt = new_active_workers->pworker_cnt;

      bool wait_to_connect = false;
      for (int i = 0; i < active_workers->pworker_cnt; i++) {
        WorkerID to = i;
        if (to == (int)wconf->node_id) {
          wconf->pong_received_from[to] = true;
          continue;
        }

        if (wconf->pong_received_from[to] == false) {
          wait_to_connect = true;
          MessageBuffer *m = create_ping(cbus, wconf->node_id, to,
                                         wconf->state_addr->get_ip_addr(),
                                         ntohs(wconf->state_addr->get_port()));
          send_message(to, m);
        }
      }

      for (int i = 0; i < active_workers->bgworker_cnt; i++) {
        WorkerID to = i + MAX_PWORKER_CNT;  // FIXME
        if (to == (int)wconf->node_id) {
          wconf->pong_received_from[to] = true;
          continue;
        }

        if (wconf->pong_received_from[to] == false) {
          wait_to_connect = true;
          MessageBuffer *m = create_ping(cbus, wconf->node_id, to,
                                         wconf->state_addr->get_ip_addr(),
                                         ntohs(wconf->state_addr->get_port()));
          send_message(to, m);
        }
      }

      if (!wait_to_connect) {
        notify_prepared_scaling();
      }

      working_state = WORKER_ST_PREPARE_SCALING;

      if (d.HasMember("import_flow_cnt"))
        stats.tobe_import_flow_cnt = d["import_flow_cnt"].GetInt();

      if (d.HasMember("export_flow_cnt"))
        stats.tobe_export_flow_cnt = d["export_flow_cnt"].GetInt();

      int ret = update_keyspace(d, key_space, active_workers->pworker_cnt);
      if (ret < 0) {
        DEBUG_ERR("No keyspace to update");
        return;
      }

      swobj_manager->set_dmz_to_scaling_on();
      mwstub_manager->set_dmz_to_scaling_on();

      scheduler->print_micro_threads_stat();
      scheduler->reset_micro_threads_stat();

    } else if (strncmp(msg_type, "start_scaling", strlen(msg_type)) == 0) {
      if (working_state == WORKER_ST_PREPARE_SCALING)
        working_state = WORKER_ST_DOING_SCALING;

      int ret = key_space->activate_next_version();
      if (ret < 0) {
        DEBUG_ERR("Fail to activate next version");
        return;
      }

      swobj_manager->set_dmz_to_scaling_off();
      mwstub_manager->set_dmz_to_scaling_off();

      swobj_manager->set_scaling_on();
      mwstub_manager->set_scaling_on();

      DEBUG_WRK("Worker " << wconf->id << " start scaling"
                                          " activated version "
                          << key_space->get_version());

      notify_started_scaling();

    } else if (strncmp(msg_type, "force_complete_scaling", strlen(msg_type)) ==
               0) {
      DEBUG_WRK("Worker " << wconf->id << " force to complete scaling");

      force_scaling_completed = true;

    } else if (strncmp(msg_type, "prepare_quiescent", strlen(msg_type)) == 0) {
      DEBUG_WRK("Worker " << wconf->id << " prepare quiescent");

      working_state = WORKER_ST_NORMAL;
      force_scaling_completed = false;

      swobj_manager->set_scaling_off();
      mwstub_manager->set_scaling_off();

      swobj_manager->set_dmz_to_quiescent_on();
      mwstub_manager->set_dmz_to_quiescent_on();

      notify_prepared_normal();

    } else if (strncmp(msg_type, "quiescent", strlen(msg_type)) == 0) {
      DEBUG_WRK("Worker " << wconf->id << " go to quiescent");

      int version = key_space->get_prev_version();
      key_space->discard_version(version);

      swobj_manager->set_dmz_to_quiescent_off();
      mwstub_manager->set_dmz_to_quiescent_off();

      scheduler->print_micro_threads_stat();
      scheduler->reset_micro_threads_stat();

      notify_being_normal();

    } else if (strncmp(msg_type, "run_bg_function", strlen(msg_type)) == 0) {
      if (d.HasMember("bg_fid")) {
        int fid = d["bg_fid"].GetInt();
        DEBUG_WRK("Worker " << wconf->id << " run background function " << fid);

        run_single_function(fid);
      } else {
        DEBUG_ERR("[mng_channel] no 'bg_fid' is specified. " << buffer + 4);
      }
    } else if (strncmp(msg_type, "tear_down", strlen(msg_type)) == 0) {
      reserve_quit();
    } else {
      DEBUG_ERR("[mng_channel] unknown 'msg_type' " << msg_type);
    }

  } else {
    DEBUG_ERR("[mng_channel] no 'msg_type' is specified. " << buffer + 4);
  }
}

void Worker::run(bool work_with_controller) {
  int ret;

  status.run_scheduler = false;

  // set-up communication channel with controller
  if (work_with_controller) {
    ret = connect_controller();
    if (ret < 0)
      return;
  }

  if (!active_workers) {
    DEBUG_ERR("No active workers information");
    return;
  }

  status.run_scheduler = true;

  if (work_with_controller) {
    // send ping for all workers
    for (int i = 0; i < active_workers->pworker_cnt; i++) {
      WorkerID to = i;
      if (to == (int)wconf->node_id) {
        wconf->pong_received_from[to] = true;
        continue;
      }

      MessageBuffer *m = create_ping(cbus, wconf->node_id, to,
                                     wconf->state_addr->get_ip_addr(),
                                     ntohs(wconf->state_addr->get_port()));
      send_message(to, m);
    }

    for (int i = 0; i < active_workers->bgworker_cnt; i++) {
      WorkerID to = i + MAX_PWORKER_CNT;  // FIXME
      if (to == (int)wconf->node_id) {
        wconf->pong_received_from[to] = true;
        continue;
      }

      MessageBuffer *m = create_ping(cbus, wconf->node_id, to,
                                     wconf->state_addr->get_ip_addr(),
                                     ntohs(wconf->state_addr->get_port()));
      send_message(to, m);
    }

    while (true) {
      process_state_plane(active_workers->pworker_cnt);

      if (check_state_channel_connectivity())
        break;
    }

    notify_ready();

    ret = wait_to_be_all_ready();
    if (ret < 0)
      return;

    // set to nonblock
    ret = fcntl(mng_sock, F_SETFL, O_NONBLOCK);
    if (ret < 0)
      return;
  }

  // Run init function if exists

  init_func init = app->get_init_func();
  if (init != nullptr) {
    int ret = init(0);
    if (ret < 0) {
      DEBUG_ERR("fail to execute init");
      return;
    }
  }

  notify_run();

  DEBUG_WRK("Worker " << wconf->id << " starts");

  int count = 0;

  // when quit is set?
  // 1. when processor is ordered quit with SIG_TERM
  // 2. when processor is finish their job (status.run_scheduler == false) AND
  // no need to serve remote request
  //  2-1. background worker completes it's job
  //  2-2. packet worker complelets processing pre-defined number of packets
  while (!status.reserve_quit) {
    count++;

#ifdef D_TIME
    static const uint64_t hz = get_tsc_freq();
    static uint64_t last_tsc = get_cur_rdtsc();
    static CbusStats last_stats = state_sock->get_stats();
#endif

    process_state_plane(1);

    if (count % 10 == 0)
      mwstub_manager->check_to_push_aggregation();

    if (work_with_controller && count % 1000 == 0)
      process_command_from_controller();

    if (force_scaling_completed) {
      int ret_sw = swobj_manager->force_scaling(100);
      int ret_mw = mwstub_manager->force_scaling(100);

      // if (ret_sw + ret_mw == 0)
      //	force_scaling_completed = false;
    }

    if (status.run_scheduler && wconf->type == PACKET_WORKER) {
      int ret = BATCH_SIZE;
      while (ret == BATCH_SIZE)
        ret = scheduler->recv_pkts();

      status.run_scheduler = scheduler->call_scheduler();
    }

    // no more task to be scheduled and no remote_service
    if (!status.run_scheduler && bgf.time == 0 && !status.remote_serving)
      status.reserve_quit = true;

    if (bgf.time > 0 && bgf.time < get_cur_rdtsc()) {
      bgf.time = 0;
      status.run_scheduler = true;
      run_single_function(bgf.id);
    }

    if (working_state == WORKER_ST_DOING_SCALING &&
        stats.tobe_export_flow_cnt <= 0 && stats.tobe_import_flow_cnt <= 0 &&
        swobj_manager->check_scaling_done()) {
      notify_completed_scaling();
    }

#ifdef D_TIME
    uint64_t cur_tsc = get_cur_rdtsc();

    if (cur_tsc - last_tsc > hz) {
      swobj_manager->print_object_stats();

      struct CbusStats cur_stats = state_sock->get_stats();
      uint64_t send = cur_stats.send_bytes - last_stats.send_bytes;
      uint64_t recv = cur_stats.recv_bytes - last_stats.recv_bytes;

      if (send > 0 || recv > 0) {
        double t = (cur_tsc - last_tsc) / (double)hz;
        double sr = (send * 8 / 1.0E+6) / t;
        double rr = (recv * 8 / 1.0E+6) / t;
        DEBUG_TIME("[ctrl channel] 1sec rcv snd "
                   << "\t" << recv << "\t" << send);
      }

      last_tsc = cur_tsc;
      last_stats = cur_stats;
    }
#endif
  }

  DEBUG_WRK("Out of loop " << wconf->id << " stop. ");

#ifdef D_TIME
  struct CbusStats cur_stats = state_sock->get_stats();
  uint64_t send = cur_stats.send_bytes;
  uint64_t recv = cur_stats.recv_bytes;
  DEBUG_TIME("[control channel] "
             << "recv " << recv << " Bytes "
                                   "send "
             << send << " Bytes ");
#endif

  if (work_with_controller) {
    notify_teared_down();
    wait_to_finish();
  }

  teardown(true);
  scheduler->teardown();

  scheduler->print_micro_threads_stat();
  scheduler->print_stat(wconf->id);
  DEBUG_WRK("Worker " << wconf->id << " stop. ");
}

int Worker::process_state_plane(uint32_t max_msg) {
  static uint64_t hz = get_tsc_freq();
  static uint64_t max_diff_tsc = 0;

  if (!cbus)
    return 0;

  uint64_t cur_tsc = get_cur_rdtsc(true);

  MessageBuffer *m = state_sock->receive();
  uint32_t loop_cnt = 0;

  while (m) {
    process_state_packet(m);
    free(m);

    // breaks the loop after processing max_msg messages
    // if max_msg equals zero, process all available control messages
    // (or loop_cnt becomes wrap around
    if (++loop_cnt == max_msg)
      break;

    m = state_sock->receive();
  }

  uint64_t last_tsc = get_cur_rdtsc(true);

  if (max_diff_tsc < last_tsc - cur_tsc) {
    max_diff_tsc = last_tsc - cur_tsc;
    double t = max_diff_tsc / (double)hz * 1000;  // ms

    DEBUG_DEV("[process_state_plane] new max time on "
              << t << " ms"
              << " for processing " << loop_cnt << " messages");
  }

  return loop_cnt;
}

void Worker::process_state_packet(MessageBuffer *mb) {
  Message *m = (Message *)mb->get_message_body();

  switch (m->mtype) {
    case MSG_PING:
      process_ping((MsgPing *)(void *)m->buf, m->from_id);
      break;
    case MSG_PONG:
      process_pong((MsgPong *)(void *)m->buf, m->from_id);
      break;
    case MSG_MW_RPC_REQUEST:
      process_mw_rpc_request((RPCRequest *)(void *)m->buf, m->from_id);
      break;
    case MSG_MW_RPC_REQUEST_MULTI_ASYM:
      process_mw_rpc_request_multi_asym((RPCRequestMultiAsym *)(void *)m->buf,
                                        m->from_id);
      break;
    case MSG_MW_RPC_REQUEST_MULTI_SYM:
      process_mw_rpc_request_multi_sym((RPCRequestMultiSym *)(void *)m->buf,
                                       m->from_id);
      break;
    case MSG_MW_RPC_RESPONSE:
      process_mw_rpc_response((RPCResponse *)(void *)m->buf);
      break;
    case MSG_MW_AGGR_REQUEST:
      process_mw_aggr_request((MWAggrRequest *)(void *)m->buf);
      break;
    case MSG_RWLEASE_REQUEST:
      process_rwlease_request((RWLeaseRequest *)(void *)m->buf, m->from_id);
      break;
    case MSG_RWLEASE_RESPONSE:
      process_rwlease_response((RWLeaseResponse *)(void *)m->buf, m->from_id);
      break;
    case MSG_RWLEASE_EXPIRE_REQUEST:
      process_rwlease_expire_request((RWLeaseExpireRequest *)(void *)m->buf,
                                     m->from_id);
      break;
    case MSG_RWLEASE_EXPIRE_RESPONSE:
      process_rwlease_expire_response((RWLeaseExpireResponse *)(void *)m->buf);
      break;
    case MSG_KEY_REQUEST:
      process_key_request((RWKeyRequest *)(void *)m->buf, m->from_id);
      break;
    case MSG_KEY_RESPONSE:
      process_key_response((RWKeyResponse *)(void *)m->buf, m->from_id);
      break;
    case MSG_RW_DEL_REQUEST:
      process_rw_del_request((RWDeleteRequest *)(void *)m->buf, m->from_id);
      break;
    case MSG_RW_DEL_RESPONSE:
      process_rw_del_response((RWDeleteResponse *)(void *)m->buf);
      break;
    case MSG_RW_CLEANUP_META_REQUEST:
      process_rw_cleanup_meta_request((RWCleanupMetaRequest *)(void *)m->buf,
                                      m->from_id);
      break;
    case MSG_MW_SKELETON_STREAM:
      process_skeleton_stream((SkeletonStream *)(void *)m->buf, m->from_id);
      break;
    default:
      DEBUG_ERR("Not defined message type:" << m->mtype);
  }
  return;
}

void Worker::process_ping(MsgPing *ping, WorkerID from) {
  DEBUG_WRK("PING from " << from << " to " << wconf->node_id);

  if (active_workers->state_addrs[from] == 0)
    active_workers->state_addrs[from] = WorkerAddress(ping->sip, ping->sport);

  MessageBuffer *m =
      create_pong(cbus, wconf->node_id, from, wconf->state_addr->get_ip_addr(),
                  ntohs(wconf->state_addr->get_port()));
  send_message(from, m);

  return;
}

void Worker::process_pong(MsgPong *pong, WorkerID from) {
  DEBUG_WRK("PONG from " << from << " to " << wconf->node_id);

  wconf->pong_received_from[from] = true;

  if (working_state == WORKER_ST_PREPARE_SCALING &&
      check_state_channel_connectivity()) {
    notify_prepared_scaling();
  }
  return;
}

void Worker::process_mw_rpc_request(RPCRequest *r, WorkerID from) {
  const Key *key = (const Key *)(void *)r->buf;
  void *args = r->buf + r->key_size;

  void *ret = malloc(sizeof(int));
  uint32_t ret_size;

  mwstub_manager->execute_rpc(r->map_id, key, r->flag, r->method_id, args, &ret,
                              &ret_size);

  DEBUG_DEV("RPC_REQUEST from " << from << " " << r->r_idx << " method_id "
                                << r->method_id);

  if (ret_size <= 0) {
    free(ret);
    return;
  }

  MessageBuffer *m =
      create_mw_rpc_response(cbus, wconf->node_id, from, r->r_idx, r->map_id,
                             key, r->flag, r->method_id, ret, ret_size);
  send_message(from, m);

  free(ret);
}

void Worker::process_mw_rpc_request_multi_asym(RPCRequestMultiAsym *r,
                                               WorkerID from) {
  int offset = 0;
  for (int i = 0; i < r->count; i++) {
    RPCRequest *rpc_req = (RPCRequest *)(r->rpc_request + offset);
    process_mw_rpc_request(rpc_req, from);
    offset += sizeof(RPCRequest) + rpc_req->key_size + rpc_req->args_size;
  }
}

void Worker::process_mw_rpc_request_multi_sym(RPCRequestMultiSym *r,
                                              WorkerID from) {
  assert(0);
}

void Worker::process_mw_rpc_response(RPCResponse *r) {
  DEBUG_DEV("RPC_RESPONSE to " << r->r_idx << " method_id " << r->method_id);

  if (r->flag & _FLAG_STALE) {
    const Key *key = (const Key *)(void *)r->buf;
    void *args = r->buf + r->key_size;
    mwstub_manager->set_cache_return(r->map_id, key, r->method_id, r->args_size,
                                     args);
  } else {
    void *args = r->buf + r->key_size;
    mwstub_manager->set_strict_return(r->r_idx, r->args_size, args);
  }

  return;
}

void Worker::process_mw_aggr_request(MWAggrRequest *r) {
  const Key *key = (const Key *)(void *)r->buf;
  MWObject *obj = (MWObject *)(r->buf + r->key_size);

  DEBUG_DEV("MW_AGGR_REQUEST for " << *key);

  mwstub_manager->aggregate(r->map_id, key, obj);
}

void Worker::process_rwlease_request(RWLeaseRequest *r, WorkerID from) {
  const Key *key = (const Key *)(void *)r->buf;

  DEBUG_DEV("RW_REQUEST for " << *key);

  if (r->force_to_create)
    swobj_manager->remote_create_object(r->map_id, key, from);
  else
    swobj_manager->remote_lookup_object(r->map_id, key, from);

  stats.complete_export_flow_cnt++;

  if (working_state == WORKER_ST_PREPARE_SCALING ||
      working_state == WORKER_ST_DOING_SCALING) {
    if (stats.tobe_export_flow_cnt <= 0 && stats.tobe_import_flow_cnt <= 0)
      return;
    if (stats.tobe_export_flow_cnt <= stats.complete_export_flow_cnt &&
        stats.tobe_import_flow_cnt <= stats.complete_import_flow_cnt)
      notify_completed_scaling();
  }

  return;
}

void Worker::process_rwlease_response(RWLeaseResponse *r, WorkerID from) {
  const Key *key = (const Key *)(void *)r->buf;
  void *data = nullptr;
  if (r->obj_size > 0)
    data = r->buf + r->key_size;

  DEBUG_DEV("RW_RESPONSE for " << *key << " with version " << r->version
                               << " with size " << r->obj_size);

  swobj_manager->remote_set_object_ownership(r->map_id, key, r->version,
                                             r->obj_size, data, from);

  stats.complete_import_flow_cnt++;

  if (working_state == WORKER_ST_PREPARE_SCALING ||
      working_state == WORKER_ST_DOING_SCALING) {
    if (stats.tobe_export_flow_cnt <= 0 && stats.tobe_import_flow_cnt <= 0)
      return;
    if (stats.tobe_export_flow_cnt <= stats.complete_export_flow_cnt &&
        stats.tobe_import_flow_cnt <= stats.complete_import_flow_cnt)
      notify_completed_scaling();
  }
}

void Worker::process_rwlease_expire_request(RWLeaseExpireRequest *r,
                                            WorkerID from) {
  const Key *key = (const Key *)(void *)r->buf;
  DEBUG_DEV("RW_RELEASE_REQUEST " << key);

  swstub_manager->request_expire_local_rwref(r->map_id, key, r->version);
}

void Worker::process_rwlease_expire_response(RWLeaseExpireResponse *r) {
  const Key *key = (const Key *)(void *)r->buf;
  DEBUG_DEV("RW_RELEASE_RESPONSE " << key);

  assert(r->obj_size);

  void *data = nullptr;
  if (r->obj_size)
    data = r->buf + r->key_size;

  swobj_manager->remote_notify_expire_rwref(r->map_id, key, r->version,
                                            r->obj_size, data, -1);
}

void Worker::process_key_request(RWKeyRequest *r, WorkerID from) {
  DEBUG_DEV("RW_KEY_REQUEST from " << from);
  const Key *key = (const Key *)(void *)r->buf;

  swobj_manager->remote_return_key_ownership(r->map_id, key, from);
}

void Worker::process_key_response(RWKeyResponse *r, WorkerID from) {
  DEBUG_DEV("RW_KEY_RESPONSE from " << from);
  const Key *key = (const Key *)(void *)r->buf;

  void *data = nullptr;
  if (r->obj_size)
    data = r->buf + r->key_size;

  swobj_manager->remote_notify_return_key_ownership(
      r->map_id, key, r->version, r->obj_size, data, from, r->waiters);
}

void Worker::process_rw_del_request(RWDeleteRequest *r, WorkerID from) {
  DEBUG_DEV("RW_DELETE_REQUEST from " << from);
  const Key *key = (const Key *)(void *)r->buf;

  // TODO: false should be replaced
  swobj_manager->remote_delete_object(r->map_id, key, r->version, r->cleanup,
                                      from);
}

void Worker::process_rw_del_response(RWDeleteResponse *r) {
  DEBUG_DEV("RW_DELETE_RESPONSE ");
  // XXX Not using currently
}

void Worker::process_rw_cleanup_meta_request(RWCleanupMetaRequest *r,
                                             WorkerID from) {
  DEBUG_DEV("RW_CLEANUP_META_REQUEST from " << from);
  const Key *key = (const Key *)(void *)r->buf;

  // TODO: false should be replaced
  swobj_manager->remote_cleanup_meta(r->map_id, key, r->version, from);
}

void Worker::process_skeleton_stream(SkeletonStream *s, WorkerID from) {
  DEBUG_DEV("IMPORTING SKELETON_STREAM");

  DEBUG_ERR("Importing skeleton map " << s->map_id);
#if 0
	if (s->obj_count > 0) 
		mwstub_manager->import_objects(s->map_id, s->key_size, s->obj_count, 
				s->buf_size, (char *) s->buf);

	is_scaling_done[from][s->map_id] = true;

	bool all_scaling_done = true;
	for (int i = 0; i < MAX_WORKER_CNT; i++)
		for (int j = 0; j < _DADTCnt; j++) 
			if (!is_scaling_done[i][j])
					all_scaling_done = false;

	if (all_scaling_done) {
		is_scaling = false;
		
		if (work_with_controller) {
			char buffer[BUFFSIZE];
			int nbytes = snprintf(buffer + 4, BUFFSIZE - 4, "{\"scaling_done\": %d}", 
					wconf->id);
			char len_str[5];
			snprintf(len_str, 5, "%04u", nbytes);
			memcpy(buffer, len_str, 4);
			int ret = write(mng_sock, buffer, nbytes + 5);
			if (ret != nbytes + 5) {
				DEBUG_ERR("Fail to sent " << nbytes + 5 << " actual sent " << ret );
				return;
			}
		}
	}
#endif
}
