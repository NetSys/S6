#ifndef _DISTREF_DROUTINE_HH_
#define _DISTREF_DROUTINE_HH_

#include <atomic>
#include <boost/coroutine/all.hpp>
#include <queue>
#include <stack>
#include <stdio.h>
#include <unordered_map>

#include "log.hh"

#include "key.hh"
#include "type.hh"

#define BATCH_SIZE 32
#define MAX_PKT_BUFF_SIZE (4096)
//#define MAX_PKT_BUFF_SIZE (32*4096*16)
#define MAX_COROUTINE_CNT (MAX_PKT_BUFF_SIZE)

#ifdef BOOST_COROUTINES_SYMMETRIC_COROUTINE_H
// only Droutine.cpp needs the complete type information
typedef boost::coroutines::symmetric_coroutine<void>::call_type call_type;
typedef boost::coroutines::symmetric_coroutine<void>::yield_type yield_type;
#else
struct call_type;
struct yield_type;
#endif

enum operationID { RT_LOOP, RT_BLOCKED };

enum RTYPE { PKT_ROUTINE, BG_ROUTINE };

enum BLOCK_ID { SW_RW_BLOCK = -3, SW_RO_BLOCK = -2, SW_OBJ_BLOCK = -1 };

struct Droutine;

class DroutineScheduler {
 private:
  int pkt_coroutine_cnt = 0;
  Droutine *pkt_routine_pool;
  Droutine *bg_routine;  // XXX support single background routine

  packet_func pkt_callback;
  std::function<void(yield_type &yield)> pkt_calltype;
  background_func bg_callback;
  std::function<void(yield_type &yield)> bg_calltype;

  std::stack<Droutine *> idle_stack;  // idle_routine which does nothing
  std::queue<Droutine *> wait_queue;  // waiting to wakeup list

  std::unordered_map<VKey, std::queue<Droutine *>, _dr_vkey_hash,
                     _dr_vkey_equal_to>
      block_map_arr[_MAX_DMAPS];
  std::bitset<_MAX_DMAPS> wait_unblocked;
  std::unordered_map<int, Droutine *> block_routine_map;

  bool is_new_pkt_routine;
  bool is_new_bg_routine;
  bool is_bg_routine_running;

  Droutine *cur_routine;

  int max_pkts_proc;  // the number of packets to be processed
  struct {
    int tot_pkts_recv;     // the number of packets received
    int tot_pkts_proc;     // the number of packets processed
    int tot_pkts_return;   // the number of packets returned to bess
    int tot_pkts_free;     // the number of packets freed in instance
    int tot_pkts_discard;  // the number of packets freed during teardown
    int cur_pkts_buff;     // the number of packets in buffer
    int max_pkts_buff;     // the number of packets in buffer
  } stats;

  bool is_dpdk = false;

  std::queue<struct rte_mbuf *> pkt_queue;

  size_t thread_count[MAX_COROUTINE_CNT + 1] = {0};

  int pkt_routine_init();
  int bg_routine_init();

  void schedule(Droutine *routine, operationID opr);
  void yield_idle();

  struct rte_mbuf *get_next_packet();
  void drop_packet(struct rte_mbuf *);
  void forward_packet(struct rte_mbuf *);

 public:
  DroutineScheduler(int node_id, bool is_dpdk, char *log_dir = nullptr);
  ~DroutineScheduler();

  void teardown();
  int set_packet_routine(packet_func cb, int coroutine_cnt);
  int set_background_routine(background_func cb);
  int run_background_routine();
  int get_cur_routine_idx();

  void set_max_new_packets(int p_cnt) { this->max_pkts_proc = p_cnt; }

  int recv_pkts();
  bool call_scheduler();
  void tear_down();

  void reset_micro_threads_stat();
  void print_micro_threads_stat();
  void print_stat(int node_id);

  /* block_id
   *  0<: method id for remote function call
   * 		method id is in gen_source/ref.*.hh
   *  0: object (binary) is ready (SW object only)
   * -1: rw ref creation notification (SW ref only)
   * -2: ro ref creation notification (SW ref only)
   * */
  void yield_block(int d_idx);
  void yield_block(int map_id, const Key *key, int block_id);

  void notify_to_wake_up(int d_idx);
  void notify_to_wake_up(int map_id, const Key *key, int block_id);

  /*
   * yield, wake-up routines
   */
};

#endif /* _DISTREF_DROUTINE_HH_ */
