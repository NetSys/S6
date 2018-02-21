#ifndef _DISTREF_MW_MAP_HH_
#define _DISTREF_MW_MAP_HH_

#include <cstring>
#include <map>
#include <string>

#include "mw_iterator.hh"

#include "d_reference.hh"
#include "reference_interceptor.hh"
#include "stub_factory.hh"

#define VNAME(x) #x

class Key;

template <class X>
class MwRef;

template <class X, class Y>
class MwMap {
 private:
  thread_local static ReferenceInterceptor* HOOK;

  int map_id;

 public:
  MwMap(const char *name) {
    this->map_id = __register_map(Y::GetObjectType(), sizeof(Y), name);
    __register_mwstub_creator(map_id, MwStub<Y>::CreateMwStub);
    __register_mwskeleton_creator(map_id, Skeleton<Y>::CreateSkeleton);
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
  MwRef<Y> create(X* key, RefState& state) {
    return MwRef<Y>(map_id, key, state);
  }

  MwRef<Y> get(X* key) { return MwRef<Y>(map_id, key); }

  MwRef<Y> lookup(X* key) { return MwRef<Y>(map_id, key, false); }

  MwRef<Y> lookup_const(X* key) { return MwRef<Y>(map_id, key, true); }

  void remove(MwRef<Y>& r) { r.delete_object(); }

  MwIter<Y>* get_local_iterator() {
    // XXX get array of iterator_id for multiple machine
    int iterator_id = HOOK->create_iterator(map_id);
    if (iterator_id < 0)
      return nullptr;

    MwIter<Y>* iter = new MwIter<Y>(map_id, true, iterator_id);
    return iter;
  }

  void release_local_iterator(MwIter<Y>* iter) {
    return HOOK->release_iterator(map_id, iter->get_const_iterator());
  }
};

template <class X, class Y>
thread_local ReferenceInterceptor* MwMap<X, Y>::HOOK =
    ReferenceInterceptor::GetReferenceInterceptor();

#endif  // #ifndef _DISTREF_DIST_MAP_HH_
