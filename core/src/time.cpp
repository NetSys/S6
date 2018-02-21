#include "time.hh"
#include <rte_cycles.h>
#include <sys/time.h>

#define TIME_PRINT(time) time.tv_sec << "." << time.tv_nsec

// XXX change to synchronous time between instances
int getCurTime(struct timespec *tv) {
  return clock_gettime(CLOCK_REALTIME, tv);
};

/* Copied from a DPDK source code */
uint64_t get_tsc_freq_from_clock() {
#define NS_PER_SEC 1E9
  struct timespec sleeptime = {0, (long int)5E+8}; /* 1/2 second */

  struct timespec t_start, t_end;
  uint64_t tsc_hz;

  if (clock_gettime(CLOCK_MONOTONIC_RAW, &t_start) == 0) {
    uint64_t ns, end, start = rte_rdtsc();
    nanosleep(&sleeptime, NULL);
    clock_gettime(CLOCK_MONOTONIC_RAW, &t_end);
    end = rte_rdtsc();
    ns = ((t_end.tv_sec - t_start.tv_sec) * NS_PER_SEC);
    ns += (t_end.tv_nsec - t_start.tv_nsec);

    double secs = (double)ns / NS_PER_SEC;
    tsc_hz = (uint64_t)((end - start) / secs);
    return tsc_hz;
  }

  return 0;
}

uint64_t get_tsc_freq(void) {
  static uint64_t freq = get_tsc_freq_from_clock();
  return freq;
};

uint64_t get_cur_rdtsc(bool _refresh) {
  static uint64_t tsc = 0;

  if (_refresh) {
    tsc = rte_rdtsc();
    return tsc;
  }

  return tsc;
}

struct timeval s6_gettimeofday(bool _refresh) {
  static timeval now = {0};

  if (_refresh || now.tv_sec == 0)
    gettimeofday(&now, NULL);

  return now;
}
