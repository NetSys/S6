#ifndef _DISTREF_MW_ITERATOR_HH_
#define _DISTREF_MW_ITERATOR_HH_

#include "d_reference.hh"
#include "reference_interceptor.hh"

template <class X>
class MwRef;

class Key;

class BaseIterator {};

template <class X>
class MwIter : public BaseIterator {
 private:
  thread_local static ReferenceInterceptor *HOOK;

  int map_id;
  bool is_const;
  int itidx;

 public:
  // Will be access directly
  const Key *key = nullptr;
  const MwRef<X> *value = nullptr;

  MwIter<X>(int map_id, bool _is_const, int itidx)
      : map_id(map_id), is_const(_is_const), itidx(itidx) {}

  int get_const_iterator() const { return itidx; }

  MwIter<X> *next() {
    key = HOOK->get_next_key(map_id, itidx);
    if (!key) {
      return nullptr;
    }

    value = new MwRef<X>(map_id, key, true /* is_const */);
    return this;
  }
};

template <class X>
thread_local ReferenceInterceptor *MwIter<X>::HOOK =
    ReferenceInterceptor::GetReferenceInterceptor();

#endif
