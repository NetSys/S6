#ifndef _DISTREF_REFERENCE_INTERCEPTOR_HH_
#define _DISTREF_REFERENCE_INTERCEPTOR_HH_

#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <queue>
#include <thread>

#include "log.hh"

#include "d_object.hh"
#include "d_reference.hh"
#include "d_routine.hh"
#include "message.hh"
#include "mwstub_manager.hh"
#include "swstub_manager.hh"
#include "type.hh"
#include "worker_address.hh"

/*
 * ReferenceInterceptor: hooking application references
 * to connect to 1) real objects, 2) caches of objects, or
 * 3) refs of objects
 */

template <class X>
class SwStub;
template <class X>
class MwStub;
template <class X>
class SwRef;
template <class X>
class MwRef;

class ReferenceInterceptor {
 private:
  thread_local static ReferenceInterceptor *pInstance;

  /* all the below variables are thread local from pInstance */
  SwStubManager *swstub_manager;
  MwStubManager *mwstub_manager;

  ReferenceInterceptor() {}
  ReferenceInterceptor(const ReferenceInterceptor &old);
  const ReferenceInterceptor &operator=(const ReferenceInterceptor &old);
  ~ReferenceInterceptor() {}

 public:
  // singleton getter
  static ReferenceInterceptor *GetReferenceInterceptor() {
    if (pInstance == nullptr) {
      pInstance = new ReferenceInterceptor();
    }
    return pInstance;
  }

  void set_managers(SwStubManager *swstub_manager,
                    MwStubManager *mwstub_manager) {
    this->swstub_manager = swstub_manager;
    this->mwstub_manager = mwstub_manager;
  }

  template <class X>
  SwStub<X> *create_object(const SwRef<X> *ref, RefState &state) {
    const int map_id = ref->getMapId();
    const Key *key = ref->getKey();
    SwStub<X> *ret = nullptr;

    DEBUG_REF("\tregister object "
              << " [" << map_id << ":" << key << "] "
              << ((ref->isConst()) ? "READONLY" : "WRITE"));

    ret = (SwStub<X> *)swstub_manager->create(map_id, key, state);
    return ret;
  }

  template <class X>
  MwStub<X> *create_object(const MwRef<X> *ref, RefState &state) {
    const int map_id = ref->getMapId();
    const Key *key = ref->getKey();
    MwStub<X> *ret = nullptr;

    DEBUG_REF("\tregister object "
              << " [" << map_id << ":" << key << "] "
              << ((ref->isConst()) ? "READONLY" : "WRITE"));

    ret = (MwStub<X> *)mwstub_manager->create(map_id, key, state);
    return ret;
  }

  template <class X>
  SwStub<X> *get_object(const SwRef<X> *ref) {
    const int map_id = ref->getMapId();
    const Key *key = ref->getKey();
    SwStub<X> *ret = nullptr;

    DEBUG_REF("\tregister object "
              << " [" << map_id << ":" << key << "] "
              << ((ref->isConst()) ? "READONLY" : "WRITE"));

    ret = (SwStub<X> *)swstub_manager->get(map_id, key);
    return ret;
  }

  template <class X>
  MwStub<X> *get_object(const MwRef<X> *ref) {
    const int map_id = ref->getMapId();
    const Key *key = ref->getKey();
    MwStub<X> *ret = nullptr;

    DEBUG_REF("\tregister object "
              << " [" << map_id << ":" << key << "] "
              << ((ref->isConst()) ? "READONLY" : "WRITE"));

    ret = (MwStub<X> *)mwstub_manager->get(map_id, key);
    return ret;
  }

  template <class X>
  SwStub<X> *lookup_object(const SwRef<X> *ref) {
    const int map_id = ref->getMapId();
    const Key *key = ref->getKey();
    SwStub<X> *ret = nullptr;

    DEBUG_REF("\tlookup object "
              << " [" << map_id << ":" << key << "] "
              << ((ref->isConst()) ? "READONLY" : "WRITE"));

    if (ref->isConst())
      ret = (SwStub<X> *)swstub_manager->lookup_cache(map_id, key);
    else
      ret = (SwStub<X> *)swstub_manager->lookup(map_id, key);
    return ret;
  }

  template <class X>
  MwStub<X> *lookup_object(const MwRef<X> *ref) {
    const int map_id = ref->getMapId();
    const Key *key = ref->getKey();
    MwStub<X> *ret = nullptr;

    DEBUG_REF("\tlookup object "
              << " [" << map_id << ":" << key << "] "
              << ((ref->isConst()) ? "READONLY" : "WRITE"));

    ret = (MwStub<X> *)mwstub_manager->lookup(map_id, key);
    return ret;
  }

  template <class X>
  void release_ref(const SwRef<X> *ref) {
    const Key *key = ref->getKey();
    const int map_id = ref->getMapId();

    DEBUG_REF("\trelease object " << ref->get() << " [" << map_id << ":" << key
                                  << "] "
                                  << ((ref->isConst()) ? "READONLY" : "WRITE"));

    if (ref->getObjectType() == DOBJECT_MW)
      mwstub_manager->release(map_id, key);
    else {
      const int version = ref->getVersion();
      if (ref->isConst()) {
        swstub_manager->release_cache(map_id, key, version);
      } else {
        swstub_manager->release(map_id, key, version);
      }
    }

    return;
  }

  template <class X>
  void release_ref(const MwRef<X> *ref) {
    const Key *key = ref->getKey();
    const int map_id = ref->getMapId();

    DEBUG_REF("\trelease object " << ref->get() << " [" << map_id << ":" << key
                                  << "] "
                                  << ((ref->isConst()) ? "READONLY" : "WRITE"));

    if (ref->getObjectType() == DOBJECT_MW)
      mwstub_manager->release(map_id, key);
    else {
      const int version = ref->getVersion();
      if (ref->isConst()) {
        swstub_manager->release_cache(map_id, key, version);
      } else {
        swstub_manager->release(map_id, key, version);
      }
    }

    return;
  }

  template <class X>
  void delete_object(const SwRef<X> *ref) {
    if (!ref)
      return;

    if (ref->getObjectType() != DOBJECT_SW) {
      DEBUG_ERR("Non SW_Object cannot be removed\n");
    }

    const Key *key = ref->getKey();
    const int map_id = ref->getMapId();
    const int version = ref->getVersion();

    DEBUG_REF("\tdelete object " << ref->get() << " [" << map_id << ":" << key
                                 << "] "
                                 << ((ref->isConst()) ? "READONLY" : "WRITE"));

    swstub_manager->delete_object(map_id, key, version);
  }

  template <class X>
  void delete_object(const MwRef<X> *ref) {
    if (!ref)
      return;

    if (ref->getObjectType() != DOBJECT_SW) {
      DEBUG_ERR("Non SW_Object cannot be removed\n");
    }

    const Key *key = ref->getKey();
    const int map_id = ref->getMapId();
    const int version = ref->getVersion();

    DEBUG_REF("\tdelete object " << ref->get() << " [" << map_id << ":" << key
                                 << "] "
                                 << ((ref->isConst()) ? "READONLY" : "WRITE"));

    swstub_manager->delete_object(map_id, key, version);
  }

  int create_iterator(int map_id) {
    return mwstub_manager->create_local_iterator(map_id);
  }

  void release_iterator(int map_id, int itidx) {
    return mwstub_manager->release_local_iterator(map_id, itidx);
  }

  const Key *get_next_key(int map_id, int itidx) {
    return mwstub_manager->get_local_next_key(map_id, itidx);
  }
};

#endif /* _DISTREF_REFERENCE_INTERCEPTOR_HH_ */
