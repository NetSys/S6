#ifndef __SANGJIN_OB_HH_
#define __SANGJIN_OB_HH_

#include <iostream>

#include "dist.hh"
#include "object_base.hh"

template <class X>
class SwRef;
class JustineObj;

// Normal, Exclusive ( <- Floatable)
class SangjinObj : public SWObject {
 private:
  int value;

  // Those two are same intention
  // const SangjinObj * bro;
  // const SwRef<SangjinObj> bro;
  const SwRef<SangjinObj> const_bro;
  const SwRef<JustineObj> const_sis;
  SwRef<JustineObj> sis;

 public:
  void set_value(int value) { this->value = value; }
  int get_value() const { return value; }

  void set_const_bro(const SwRef<SangjinObj> const_bro) {
    this->const_bro = const_bro;
  }
  const SwRef<SangjinObj> get_const_bro() const { return const_bro; }

  void set_const_sis(const SwRef<JustineObj> const_sis) {
    this->const_sis = const_sis;
  }
  const SwRef<JustineObj> get_const_sis() const { return const_sis; }

  void set_sis(SwRef<JustineObj> sis) { this->sis = sis; }
  SwRef<JustineObj> get_sis() const { return sis; }

  friend std::ostream& operator<<(std::ostream& out, const SangjinObj& sj) {
    return out << sj.value;
  }
};

#endif
