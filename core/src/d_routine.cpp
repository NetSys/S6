#include "d_routine.hh"

#include <bitset>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <string>
#include <time.h>

#include <rte_config.h>
#include <rte_cycles.h>

#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_udp.h>

#include "dpdk.hh"
#include "time.hh"

#define IP_PRINT(ip)                                                       \
  (ip >> 24 & 0xFF) << "." << (ip >> 16 & 0xFF) << "." << (ip >> 8 & 0xFF) \
                    << "." << (ip & 0xFF)

struct Droutine {
  int idx;  // for debugging
  RTYPE type;
  call_type *call;
  yield_type *yield;
  std::atomic<bool> loop_coroutine;
};

// XXX Move to packet generator
/* Copied from a bess native_apps/source.c */
/* Copied from a DPDK example */
static void update_ip_csum(struct ipv4_hdr *ip) {
  uint32_t ip_cksum;
  uint16_t *buf;

  buf = (uint16_t *)ip;
  ip_cksum = 0;
  ip_cksum += buf[0];
  ip_cksum += buf[1];
  ip_cksum += buf[2];
  ip_cksum += buf[3];
  ip_cksum += buf[4];
  /* buf[5]: checksum field */
  ip_cksum += buf[6];
  ip_cksum += buf[7];
  ip_cksum += buf[8];
  ip_cksum += buf[9];

  /* reduce16 */
  ip_cksum = ((ip_cksum & 0xFFFF0000) >> 16) + (ip_cksum & 0x0000FFFF);
  if (ip_cksum > 65535)
    ip_cksum -= 65535;
  ip_cksum = (~ip_cksum) & 0x0000FFFF;
  if (ip_cksum == 0)
    ip_cksum = 0xFFFF;

  ip->hdr_checksum = (uint16_t)ip_cksum;
}

#define SET_LLADDR(lladdr, a, b, c, d, e, f) \
  do {                                       \
    (lladdr).addr_bytes[0] = a;              \
    (lladdr).addr_bytes[1] = b;              \
    (lladdr).addr_bytes[2] = c;              \
    (lladdr).addr_bytes[3] = d;              \
    (lladdr).addr_bytes[4] = e;              \
    (lladdr).addr_bytes[5] = f;              \
  } while (0)

static void build_template(char *pkt_tmp, int size) {
  struct ether_hdr *eth_tmp = (struct ether_hdr *)pkt_tmp;
  struct ipv4_hdr *ip_tmp =
      (struct ipv4_hdr *)(pkt_tmp + sizeof(struct ether_hdr));
  struct udp_hdr *udp_tmp =
      (struct udp_hdr *)(pkt_tmp + sizeof(struct ether_hdr) +
                         sizeof(struct ipv4_hdr));

  SET_LLADDR(eth_tmp->d_addr, 0, 0, 0, 0, 0, 2);
  SET_LLADDR(eth_tmp->s_addr, 0, 0, 0, 0, 0, 1);
  eth_tmp->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

  ip_tmp->version_ihl = (4 << 4) | sizeof(struct ipv4_hdr) >> 2;
  ip_tmp->type_of_service = 0;
  ip_tmp->total_length = rte_cpu_to_be_16(size - sizeof(*eth_tmp));
  ip_tmp->packet_id = rte_cpu_to_be_16(0);
  ip_tmp->fragment_offset = rte_cpu_to_be_16(0);
  ip_tmp->time_to_live = 64;
  ip_tmp->next_proto_id = IPPROTO_UDP;
  ip_tmp->dst_addr = rte_cpu_to_be_32(IPv4(192, 168, 0, 2));
  ip_tmp->src_addr = rte_cpu_to_be_32(IPv4(192, 168, 0, 0));
  update_ip_csum(ip_tmp);

  udp_tmp->src_port = rte_cpu_to_be_16(1234);
  udp_tmp->dst_port = rte_cpu_to_be_16(5678);
  udp_tmp->dgram_len =
      rte_cpu_to_be_16(size - sizeof(*eth_tmp) - sizeof(*ip_tmp));
  udp_tmp->dgram_cksum = rte_cpu_to_be_16(0);
}

DroutineScheduler::DroutineScheduler(int node_id, bool is_dpdk, char *log_dir) {
  this->pkt_routine_pool = nullptr;
  this->bg_routine = nullptr;
  this->pkt_callback = nullptr;
  this->bg_callback = nullptr;
  this->is_new_pkt_routine = false;
  this->is_new_bg_routine = false;
  this->is_bg_routine_running = false;
  this->cur_routine = nullptr;

  this->max_pkts_proc = 0;  // 0 for processing packets infinitely
                            // until dies
  this->is_dpdk = is_dpdk;
  stats = {0};

  wait_unblocked.set();

  if (!is_dpdk) {
    int p_size = 64;

    char pkt_tmp[p_size];
    build_template(pkt_tmp, p_size);

    for (int i = 0; i < 4096; i++) {
      struct rte_mbuf *mbuf = (struct rte_mbuf *)calloc(1, 2048);
      assert(mbuf);

      // those packets should be only used in this process!
      // just for working rte_pktmbuf_mtod() or rte_pktmbuf_dump();
      mbuf->pkt_len = mbuf->data_len = p_size;
      mbuf->buf_len = 2048 - sizeof(struct rte_mbuf);
      mbuf->nb_segs = 1;
      mbuf->next = NULL;
      mbuf->buf_addr = mbuf + 1;
      mbuf->data_off = RTE_PKTMBUF_HEADROOM;

      memcpy((char *)mbuf->buf_addr + mbuf->data_off, pkt_tmp, p_size);

      // XXX modify packet contents if necessary
      struct ipv4_hdr *iph = rte_pktmbuf_mtod_offset(
          (struct rte_mbuf *)mbuf, struct ipv4_hdr *, sizeof(struct ether_hdr));
      iph->src_addr = rte_cpu_to_be_32(IPv4(192, 168, 0, i % 255));
      iph->dst_addr = rte_cpu_to_be_32(IPv4(10, 0, 0, i % 255));

      struct udp_hdr *udph = (struct udp_hdr *)(iph + 1);
      udph->src_port =
          rte_cpu_to_be_16(rte_be_to_cpu_16(udph->src_port) + (i % 65535));

      pkt_queue.push(mbuf);
    }
  }
};

DroutineScheduler::~DroutineScheduler() {
  /* clear coroutines, coroutine containers - stack, queue, map */
  while (!idle_stack.empty())
    idle_stack.pop();
  while (!wait_queue.empty())
    wait_queue.pop();
  for (int i = 0; i < _MAX_DMAPS; i++) {
    block_map_arr[i].clear();
  }

  if (bg_routine != nullptr) {
    delete bg_routine->call;
    delete bg_routine;
  }

  for (int i = 0; i < pkt_coroutine_cnt; i++) {
    Droutine *routine = &pkt_routine_pool[i];
    delete routine->call;
  }

  free(pkt_routine_pool);
}

int DroutineScheduler::pkt_routine_init() {
  pkt_calltype = ([&](yield_type &yield) {
    int pkt_count = 0;
    int loop_count = 0;
    int count = 0;
    while (true) {
      loop_count++;

      if (loop_count >= 4) {
        pkt_count = 0;
        loop_count = 0;
        yield_idle();
        continue;
      }

      cur_routine->yield = &yield;

      if ((max_pkts_proc > 0) && (stats.tot_pkts_proc >= max_pkts_proc)) {
        yield_idle();
        continue;
      }

      struct rte_mbuf *pkt_p = get_next_packet();
      if (!pkt_p) {
        yield_idle();
        continue;
      }

      pkt_count++;
      loop_count--;

      int forward = pkt_callback(pkt_p);
      if (forward)
        forward_packet(pkt_p);
      else
        drop_packet(pkt_p);

      if (!cur_routine->loop_coroutine || pkt_count >= 96) {
        pkt_count = 0;
        loop_count = 0;
        yield_idle();
        continue;
      }
    }  // end for while
  });

  pkt_routine_pool =
      (Droutine *)std::malloc(pkt_coroutine_cnt * sizeof(Droutine));
  if (!pkt_routine_pool) {
    errno = -ENOMEM;
    return -1;
  }

  for (int i = 0; i < pkt_coroutine_cnt; i++) {
    Droutine *routine = &pkt_routine_pool[i];
    routine->idx = pkt_coroutine_cnt - i;
    routine->type = PKT_ROUTINE;
    routine->call = new call_type(pkt_calltype);
    routine->yield = nullptr;
    routine->loop_coroutine = true;

    idle_stack.push(routine);
  }

  is_new_pkt_routine = true;

  return 0;
}

int DroutineScheduler::bg_routine_init() {
  bg_calltype = ([&](yield_type &yield) {

    cur_routine->yield = &yield;

    bg_callback();

    is_bg_routine_running = false;
  });

  bg_routine = new Droutine();
  bg_routine->idx = MAX_COROUTINE_CNT;
  bg_routine->type = BG_ROUTINE;
  bg_routine->call = new call_type(bg_calltype);
  bg_routine->yield = nullptr;
  bg_routine->loop_coroutine = false;
  return 0;
}

int DroutineScheduler::recv_pkts() {
  if (!is_dpdk)
    return 0;

  static const uint64_t hz = get_tsc_freq();

  static const uint64_t start_tsc = get_cur_rdtsc(true);
  static uint64_t last_call_tsc = start_tsc;
  static uint64_t max_diff_tsc = 0;

  s6_gettimeofday(true);

  static bool print_empty_pool = true;
  if (pkt_queue.size() > MAX_PKT_BUFF_SIZE) {
    if (print_empty_pool) {
      DEBUG_WARN("number of packets exceeds the predefined maximum "
                 << MAX_PKT_BUFF_SIZE);
      print_empty_pool = false;
    }
    return -1;
  }

  rte_mbuf *pkt_pool[BATCH_SIZE];

  int ret = dpdk_receive_pkts(pkt_pool, BATCH_SIZE);
  if (ret <= 0)
    return -1;

  for (int i = 0; i < ret; i++)
    pkt_queue.push(pkt_pool[i]);

  stats.tot_pkts_recv += ret;
  stats.cur_pkts_buff += ret;

  uint64_t recv_tsc = get_cur_rdtsc(true);
  if (max_diff_tsc < recv_tsc - last_call_tsc) {
    max_diff_tsc = recv_tsc - last_call_tsc;
    double t = max_diff_tsc / (double)hz * 1000;  // ms
    DEBUG_TIME("[dpdk_receive_pkts] new max time diff between two calls "
               << t << " ms");
  }
  last_call_tsc = recv_tsc;

  if (stats.cur_pkts_buff > stats.max_pkts_buff &&
      recv_tsc - start_tsc > (double)hz * 5) {  // ignore first 5 sec
    stats.max_pkts_buff = stats.cur_pkts_buff;
  }

  return ret;
}

struct rte_mbuf *DroutineScheduler::get_next_packet() {
  if (!is_dpdk) {
    if (pkt_queue.empty())
      return nullptr;

    rte_mbuf *pkt = pkt_queue.front();
    pkt_queue.pop();
    stats.tot_pkts_proc++;
    return pkt;
  }

  if (pkt_queue.empty()) {
    bool is_new_packet = false;
    int ret = BATCH_SIZE;
    while (ret == BATCH_SIZE) {
      int ret = recv_pkts();
      if (ret <= 0)
        break;
      is_new_packet = true;
    }

    if (!is_new_packet)
      return nullptr;
  }

  rte_mbuf *pkt = pkt_queue.front();
  pkt_queue.pop();
  stats.tot_pkts_proc++;
  return pkt;
}

void DroutineScheduler::drop_packet(struct rte_mbuf *pkt) {
  if (!is_dpdk) {
    pkt_queue.push(pkt);
    return;
  }

  rte_pktmbuf_free(pkt);
  stats.tot_pkts_free++;
  stats.cur_pkts_buff--;
}

void DroutineScheduler::forward_packet(struct rte_mbuf *pkt) {
  if (!is_dpdk) {
    pkt_queue.push(pkt);
    return;
  }

  uint16_t tx = dpdk_send_pkt(pkt);
  if (tx == 0)
    rte_pktmbuf_free(pkt);

  stats.tot_pkts_free++;
  stats.cur_pkts_buff--;
}

void DroutineScheduler::teardown() {
  while (!pkt_queue.empty()) {
    rte_mbuf *pkt = pkt_queue.front();
    pkt_queue.pop();
    rte_pktmbuf_free(pkt);

    stats.tot_pkts_discard++;

    if (!is_dpdk)
      free(pkt);
  }
}

int DroutineScheduler::set_packet_routine(packet_func cb, int coroutine_cnt) {
  pkt_callback = cb;

  if (coroutine_cnt > MAX_COROUTINE_CNT) {
    DEBUG_WARN("The number of requested coroutine "
               << coroutine_cnt << " exceeds the maximum " << MAX_COROUTINE_CNT
               << " set to maximum.");
    pkt_coroutine_cnt = MAX_COROUTINE_CNT;
  } else
    pkt_coroutine_cnt = coroutine_cnt;

  return pkt_routine_init();
};

int DroutineScheduler::set_background_routine(background_func cb) {
  bg_callback = cb;
  bg_routine_init();

  return 0;
};

int DroutineScheduler::run_background_routine() {
  if (is_bg_routine_running || bg_callback == nullptr)
    return -1;

  is_new_bg_routine = true;
  is_bg_routine_running = true;

  return 0;
};

int DroutineScheduler::get_cur_routine_idx() {
  if (cur_routine)
    return cur_routine->idx;
  else
    return -1;
}

/* scheduling routines -Dummiest scheduler ever ;( */
bool DroutineScheduler::call_scheduler() {
  static bool warn_coroutine_pool = false;

#ifdef NO_COROUTINE  // e.g, valgrind test
  if (is_new_pkt_routine) {
    struct rte_mbuf *pkt_p = get_next_packet();
    pkt_callback(pkt_p);
    drop_packet(pkt_p);

    if ((max_pkts_proc > 0) && (tot_pkts_proc >= max_pkts_proc))
      is_new_pkt_routine = false;

    return true;
  }

  if (is_new_bg_routine) {
    DEBUG_MTH("schedule bg_routine " << bg_routine->idx);
    bg_callback();
    return false;
  }
#else  // undef NO_COROUTINE

  /* executing idle routines with new packets */
  if (is_new_pkt_routine) {
    DEBUG_MTH("Schedule new packet routine");
    if (!idle_stack.empty()) {
      Droutine *idle_routine = idle_stack.top();
      idle_stack.pop();
      DEBUG_MTH("schedule idle_routine " << idle_routine->idx);

      int cnt = pkt_coroutine_cnt - idle_stack.size();
      assert(cnt >= 0 && cnt <= MAX_COROUTINE_CNT);
      thread_count[cnt]++;

      schedule(idle_routine, RT_LOOP);
    } else {
      // no alert
      warn_coroutine_pool = true;
      if (!warn_coroutine_pool)
        DEBUG_WARN("There is no idle routine!!");
    }

    if ((max_pkts_proc > 0) && (stats.tot_pkts_proc >= max_pkts_proc))
      is_new_pkt_routine = false;
  }

  // XXX Need to implement priority scheduler
  // pkt_routine >> bg_routine
  if (bg_routine && is_new_bg_routine) {
    is_new_bg_routine = false;
    DEBUG_MTH("schedule bg_routine " << bg_routine->idx);
    schedule(bg_routine, RT_BLOCKED);
  }

  /* executing wait routines */
  while (!wait_queue.empty()) {
    DEBUG_MTH("Schedule new waiting routines");
    Droutine *wait_routine = wait_queue.front();
    wait_queue.pop();
    DEBUG_MTH("schedule wait_routine " << wait_routine->idx);
    schedule(wait_routine, RT_BLOCKED);
  }

  if (!is_new_pkt_routine && wait_unblocked.all() && wait_queue.empty() &&
      block_routine_map.empty()) {
    DEBUG_MTH("Stop scheduler");
    return false;
  }

#endif  // ifdef NO_COROUTINE
  return true;
}

void DroutineScheduler::reset_micro_threads_stat() {
  thread_count[MAX_COROUTINE_CNT + 1] = {0};
}

void DroutineScheduler::print_micro_threads_stat() {
  uint64_t total = 0;
  uint64_t max = 0;

  for (int i = 0; i < MAX_COROUTINE_CNT; i++) {
    if (thread_count[i] > 0) {
      total += thread_count[i];
      max = i;
    }
  }
  DEBUG_INFO("The maximum number of multi-threads is " << max);

  uint64_t median_cnt = total / 2;
  uint64_t t_total = 0;
  for (int i = 0; i < MAX_COROUTINE_CNT; i++) {
    if (thread_count[i] == 0)
      continue;

    // DEBUG_ERR( i << " " << thread_count[i]);

    t_total += thread_count[i];
    if (median_cnt < t_total) {
      DEBUG_INFO("The median number of multi-threads is " << i);
      break;
    }
  }
}

void DroutineScheduler::print_stat(int node_id) {
  DEBUG_INFO("Packet stats ");
  DEBUG_INFO("Received: " << stats.tot_pkts_recv);
  DEBUG_INFO("Processed: " << stats.tot_pkts_proc);
  DEBUG_INFO("Returned to DPDK: " << stats.tot_pkts_return);
  DEBUG_INFO("Freed internally: " << stats.tot_pkts_free);
  DEBUG_INFO("Discarded during tear-down: " << stats.tot_pkts_discard);
  DEBUG_INFO("[w" << node_id
                  << "] Maximum buffer occupancy: " << stats.max_pkts_buff);
}

void DroutineScheduler::yield_block(int d_idx) {
  DEBUG_MTH("\tyield_block " << cur_routine->idx);

  block_routine_map[d_idx] = cur_routine;

  yield_type *yield = cur_routine->yield;
  cur_routine = nullptr;
  (*yield)();

  DEBUG_MTH("\tyield_block back " << cur_routine->idx);
};

void DroutineScheduler::yield_block(int map_id, const Key *key, int block_id) {
  DEBUG_MTH("\tyield_block " << cur_routine->idx);

  std::queue<Droutine *> &block_queue =
      block_map_arr[map_id][VKey(key, block_id)];
  wait_unblocked.reset(map_id);
  block_queue.push(cur_routine);

  yield_type *yield = cur_routine->yield;
  cur_routine = nullptr;
  (*yield)();

  DEBUG_MTH("\tyield_block back " << cur_routine->idx);
}

void DroutineScheduler::yield_idle() {
  DEBUG_MTH("\tyield_idle " << cur_routine->idx);
  idle_stack.push(cur_routine);

  yield_type *yield = cur_routine->yield;
  cur_routine = nullptr;
  (*yield)();

  DEBUG_MTH("\tyield_idle back " << cur_routine->idx);
}

void DroutineScheduler::schedule(Droutine *routine, operationID opr) {
  assert(routine);

  if (opr == RT_LOOP)
    routine->loop_coroutine = true;
  else
    routine->loop_coroutine = false;

  cur_routine = routine;
  call_type *call = cur_routine->call;
  (*call)();
}

void DroutineScheduler::notify_to_wake_up(int d_idx) {
  auto it = block_routine_map.find(d_idx);
  if (it == block_routine_map.end()) {
    DEBUG_MTH("No waiting queue in strict " << d_idx);
    return;
  }

  DEBUG_MTH("wait to be scheduled in queue " << d_idx);
  Droutine *d = it->second;
  block_routine_map.erase(it);
  wait_queue.push(d);
}

void DroutineScheduler::notify_to_wake_up(int map_id, const Key *key,
                                          int block_id) {
  // pop the coroutines from the wake_up queue
  std::queue<Droutine *> &block_queue =
      block_map_arr[map_id][VKey(key, block_id)];

  if (block_queue.empty())
    DEBUG_MTH("No waiting queue in cached ");

  while (!block_queue.empty()) {
    DEBUG_MTH("Move to wake up queue");
    Droutine *block_routine = block_queue.front();
    block_queue.pop();
    wait_queue.push(block_routine);
  }

  block_map_arr[map_id].erase(VKey(key, block_id));

  if (block_map_arr[map_id].empty())
    wait_unblocked.set(map_id);
}
