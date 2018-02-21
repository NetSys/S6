#ifndef _JUSTINE_OB_HH_
#define _JUSTINE_OB_HH_

#include "object_base.hh"
#include <iostream>

class JustineObj : public SWObject {
 private:
  int value = 0;

 public:
  void set_value(int value) { this->value = value; }
  int get_value() const { return value; }

  friend std::ostream& operator<<(std::ostream& out, const JustineObj& jt) {
    return out << jt.value;
  }
};

#endif
