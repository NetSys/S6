#ifndef _DISTREF_TIME_HH_
#define _DISTREF_TIME_HH_

#include <cstdint>
#include <ctime>

#define TIME_PRINT(time) time.tv_sec << "." << time.tv_nsec

// XXX change to synchronous time between instances
int getCurTime(struct timespec *tv);

uint64_t get_tsc_freq(void);
uint64_t get_cur_rdtsc(bool _refresh = false);
struct timeval s6_gettimeofday(bool _refresh = false);

#endif
