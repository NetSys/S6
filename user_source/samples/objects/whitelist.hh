#ifndef _WHITE_LIST_HH_
#define _WHITE_LIST_HH_

#include "dist.hh"
#include "object_base.hh"

class WhiteList : public MWObject {
  uint32_t src_ip = 0;
  bool is_ok = false;

 public:
  uint32_t _get_size() { return 0; }

  char *_get_bytes() { return nullptr; }

  static WhiteList *_get_object(uint32_t obj_size, char *obj) {
    return nullptr;
  }

  void update(uint32_t src_ip, bool is_ok) {
    this->src_ip = src_ip;
    this->is_ok = is_ok;
  }

  void update_behind(uint32_t src_ip, bool is_ok) _behind {
    this->src_ip = src_ip;
    this->is_ok = is_ok;
  }

  bool get_is_ok() const _stale { return is_ok; }
};

#endif
