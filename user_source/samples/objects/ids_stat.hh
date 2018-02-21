#ifndef _IDS_STAT_HH_
#define _IDS_STAT_HH_

#include <string.h>

#include "object_base.hh"

class IDSStat : public MWObject {
  uint64_t pass_cnt;
  uint64_t fail_cnt;

 public:
  // the simplest method
  void reset() _behind {
    pass_cnt = 0;
    fail_cnt = 0;
  };

  // with a parameter
  void inc_pass(int x) _behind { pass_cnt += x; };

  void inc_fail(int x) _behind { fail_cnt += x; };

  int get_pass_cnt(void) const { return pass_cnt; };

  int get_fail_cnt(void) const { return fail_cnt; };

  /* XXX
   * Need to be auto-generated */
  uint32_t _get_size() { return sizeof(uint64_t) + sizeof(uint64_t); }

  /* XXX
   * Need to be auto-generated */
  char *_get_bytes() {
    char *ret = (char *)malloc(_get_size());
    memcpy(ret, &pass_cnt, sizeof(pass_cnt));
    memcpy(ret + sizeof(pass_cnt), &fail_cnt, sizeof(fail_cnt));
    return ret;
  }

  /* XXX
   * Need to be auto-generated */
  static IDSStat *_get_object(uint32_t obj_size, char *obj) {
    IDSStat *ret = new IDSStat();

    uint32_t offset = 0;
    ret->pass_cnt = *(uint64_t *)(obj + offset);
    offset += sizeof(uint64_t);

    ret->fail_cnt = *(uint64_t *)(obj + offset);
    offset += sizeof(uint64_t);

    if (offset > obj_size) {
      delete ret;
      return nullptr;
    }

    return ret;
  }
};

#endif
