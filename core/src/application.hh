#ifndef _DISTREF_APPLICATION_HH_
#define _DISTREF_APPLICATION_HH_

#define MAX_BGFUNC_CNT 10

#include "type.hh"

class DroutineScheduler;

class Application {
 private:
  int bg_count = 0;
  background_func bgf_arr[MAX_BGFUNC_CNT];
  packet_func pf;
  init_func initf = nullptr;

 public:
  int set_background_func(background_func bg) {
    if (bg_count >= MAX_BGFUNC_CNT)
      return -1;

    bgf_arr[bg_count] = bg;
    bg_count++;
    return 0;
  }

  int set_packet_func(packet_func pf) {
    this->pf = pf;
    return 0;
  };

  int set_init_func(init_func initf) {
    this->initf = initf;
    return 0;
  }

  background_func get_background_func(int idx) {
    if (idx >= bg_count)
      return nullptr;

    return bgf_arr[idx];
  };

  packet_func get_packet_func() { return pf; };

  init_func get_init_func() { return initf; };
};

Application *create_application();

#endif
