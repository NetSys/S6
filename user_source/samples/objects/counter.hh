#ifndef _COUNTER_HH_
#define _COUNTER_HH_

#include <string.h>

#include "object_base.hh"

class Counter : public MWObject {
 public:
  // the simplest method
  void reset() _behind { counter = 0; };

  // with a parameter
  void inc(int x) _behind { counter += x; };

  // with a return value
  int get(void) const _stale { return counter; };

  // with both a parameter and a return value
  int inc_and_get(int x) {
    counter += x;
    return counter;
  };

  /* XXX
   * Need to be auto-generated */
  uint32_t _get_size() { return sizeof(int); }

  /* XXX
   * Need to be auto-generated */
  char *_get_bytes() {
    char *ret = (char *)malloc(_get_size());
    memcpy(ret, &counter, sizeof(counter));
    return ret;
  }

  /* XXX
   * Need to be auto-generated */
  static Counter *_get_object(uint32_t obj_size, char *obj) {
    Counter *ret = new Counter();

    int offset = 0;
    ret->counter = *(int *)(obj + offset);
    offset += sizeof(int);

    if (offset > (int)obj_size) {
      delete ret;
      return nullptr;
    }

    return ret;
  }

 private:
  int counter = 0;
};

#endif
