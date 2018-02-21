#ifndef _DISTREF_SW_MAP_HH_
#define _DISTREF_SW_MAP_HH_

#include <cstring>
#include <map>
#include <string>

#include "d_reference.hh"
#include "reference_interceptor.hh"
#include "stub_factory.hh"

#define VNAME(x) #x

class Key;

template <class X, class Y>
class SwMap {
 private:
  thread_local static ReferenceInterceptor* HOOK;

  int map_id;

 public:
  SwMap(const char *name) {
    this->map_id = __register_map(Y::GetObjectType(), sizeof(Y), name);
    __register_swstub_creator(map_id, SwStub<Y>::CreateSwStub);
  }

  int get_map_id() { return map_id; }

  /*
  INPUT			OUTPUT
  create	ro/rw	created
  ============================
  true	rw		true/false 	=> create, get
      ro (x)
  ----------------------------
  false	rw		false		=> lookup
      ro		false
  */

  SwRef<Y> create(X* key, RefState& state) {
    return SwRef<Y>(map_id, key, state);
  }

  SwRef<Y> get(X* key) { return SwRef<Y>(map_id, key); }

  SwRef<Y> lookup(X* key) { return SwRef<Y>(map_id, key, false); }

  const SwRef<Y> lookup_const(X* key) { return SwRef<Y>(map_id, key, true); }

  void remove(SwRef<Y>& r) { r.delete_object(); }
};

template <class X, class Y>
thread_local ReferenceInterceptor* SwMap<X, Y>::HOOK =
    ReferenceInterceptor::GetReferenceInterceptor();

#endif  // #ifndef _DISTREF_DIST_MAP_HH_
