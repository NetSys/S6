#ifndef _DISTREF_DEBUG_HH_
#define _DISTREF_DEBUG_HH_

#include <execinfo.h>
#include <iostream>
#include <sys/syscall.h>
#include <unistd.h>

#define D_STAT
//#define D_OBJ
#define D_APP
#define D_WRK
//#define D_TIME
//#define D_REF
//#define D_MTH
//#define D_DEV
//#define D_PTC
//#define D_RPC

#define PRINT_MESSAGE

#define _DEBUG(prefix, str)                                              \
  do {                                                                   \
    std::cout << "\033[1;40;32m" << prefix << ':' << syscall(SYS_gettid) \
              << ' ' << str << "\033[0m" << std::endl;                   \
  } while (0)

// Reference management log
#ifdef D_REF
#define DEBUG_REF(str) _DEBUG("[REF]", str)
#else
#define DEBUG_REF(str)
#endif

#ifdef D_OBJ
#define DEBUG_OBJ(str) _DEBUG("[OBJ]", str)
#else
#define DEBUG_OBJ(str)
#endif

// Application-level log
#ifdef D_APP
#define DEBUG_APP(str)                                                     \
  do {                                                                     \
    std::cout << "\033[1;40;33m"                                           \
              << "[APP]: " << syscall(SYS_gettid) << "[" << __FUNCTION__   \
              << ":" << __LINE__ << "] " << str << "\033[0m" << std::endl; \
  } while (0)
#else
#define DEBUG_APP(str)
#endif

// worker thread  log
#ifdef D_STAT
#define DEBUG_STAT(str) _DEBUG("[STAT]", str)
#else
#define DEBUG_STAT(str)
#endif

// worker thread  log
#ifdef D_WRK
#define DEBUG_WRK(str) _DEBUG("[WRK]", str)
#else
#define DEBUG_WRK(str)
#endif

#ifdef D_TIME
#define DEBUG_TIME(str) _DEBUG("[TIME]", str)
#else
#define DEBUG_TIME(str)
#endif

// mastser thread log
#ifdef D_MTH
#define DEBUG_MTH(str) _DEBUG("[MTH]", str)
#else
#define DEBUG_MTH(str)
#endif

// Protocol between controller and workers
#ifdef D_PTC
#define DEBUG_PTC(str) _DEBUG("[PTC]", str)
#else
#define DEBUG_PTC(str)
#endif

// RPC log
#ifdef D_RPC
#define DEBUG_RPC(str) _DEBUG("[RPC]", str)
#else
#define DEBUG_RPC(str)
#endif

// Stuff to log during development
#ifdef D_DEV
#define DEBUG_DEV(str)                                                     \
  do {                                                                     \
    std::cout << "[DEV]:" << syscall(SYS_gettid) << " [" << __FUNCTION__   \
              << ":" << __LINE__ << "] " << str << "\033[0m" << std::endl; \
  } while (0)
#else
#define DEBUG_DEV(str)
#endif

#define DEBUG_ERR(str)                                                     \
  do {                                                                     \
    std::cout << "\033[1;40;31m"                                           \
              << "[ERR]: " << syscall(SYS_gettid) << "[" << __FUNCTION__   \
              << ":" << __LINE__ << "] " << str << "\033[0m" << std::endl; \
  } while (0)

#define DEBUG_WARN(str)                                                    \
  do {                                                                     \
    std::cout << "\033[1;40;33m"                                           \
              << "[WARN]: " << syscall(SYS_gettid) << "[ " << __FUNCTION__ \
              << ":" << __LINE__ << "] " << str << "\033[0m" << std::endl; \
  } while (0)

#define DEBUG_INFO(str)                                                    \
  do {                                                                     \
    std::cout << "\033[1;40;32m"                                           \
              << "[INFO]: " << syscall(SYS_gettid) << "[ " << __FUNCTION__ \
              << ":" << __LINE__ << "] " << str << "\033[0m" << std::endl; \
  } while (0)

static void print_stack() {
  void *array[20];
  size_t size;

  // get void*'s for all entries on the stack
  size = backtrace(array, 20);

  // print out all the frames to stderr
  backtrace_symbols_fd(array, size, STDERR_FILENO);
}

#endif  // #ifndef _DISTREF_DEBUG_H
