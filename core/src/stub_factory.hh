#ifndef _DISTREF_REFERENCE_FACTORY_HH_
#define _DISTREF_REFERENCE_FACTORY_HH_

#include <cassert>
#include <map>
#include <string>

#include "mw_skeleton.hh"
#include "mw_stub.hh"
#include "sw_stub.hh"
#include "type.hh"

typedef SwStubBase *(*swstub_creator)(SwStubManager *mng, int map_id,
                                      const Key *, int version, void *obj,
                                      bool init, bool is_const);
typedef SwStubBase *(*swstub_ro_creator)(SwStubManager *mng, int map_id,
                                         const Key *, int version);
typedef MwStubBase *(*mwstub_creator)(int map_id, const Key *,
                                      MwStubManager *mng);
typedef MWSkeleton *(*mwskeleton_creator)(int map_id, const Key *, void *obj,
                                          bool init);

extern int num_dmap;
extern DObjType __global_dobj_type[_MAX_DMAPS];
extern size_t __global_dobj_size[_MAX_DMAPS];
extern const char *__global_dobj_name[_MAX_DMAPS];
extern swstub_creator __global_swstub_creator[_MAX_DMAPS];
extern mwstub_creator __global_mwstub_creator[_MAX_DMAPS];
extern mwskeleton_creator __global_mwskeleton_creator[_MAX_DMAPS];

class StubFactory {
 private:
 public:
  static DObjType GetStubType(int map_id) {
    if (map_id >= num_dmap)
      return DOBJECT_UNKNOWN;
    return __global_dobj_type[map_id];
  }

  static SwStubBase *GetSwStubBase(int map_id, const Key *key, int version) {
    if (__global_swstub_creator[map_id] == nullptr) {
      DEBUG_ERR("No SwStubBase creator is defined for " << map_id);
      return nullptr;
    }

    return (SwStubBase *)__global_swstub_creator[map_id](
        nullptr, map_id, key, version, nullptr, false /* init */,
        false /* is_const */);
  }

  static SwStubBase *GetSwStubBase(int map_id, const Key *key, int version,
                                   void *obj, bool init) {
    if (__global_swstub_creator[map_id] == nullptr) {
      DEBUG_ERR("No SwStubBase creator is defined for " << map_id);
      return nullptr;
    }

    if (!obj) {
      DEBUG_ERR("SwStubBase should have obj");
      return nullptr;
    }

    return (SwStubBase *)__global_swstub_creator[map_id](
        nullptr, map_id, key, version, obj, init, false /* is_const */);
  }

  static SwStubBase *GetSwStubBase_RO(int map_id, const Key *key, int version,
                                      SwStubManager *mng) {
    if (__global_swstub_creator[map_id] == nullptr) {
      DEBUG_ERR("No SwStubBase creator is defined for " << map_id);
      return nullptr;
    }

    return (SwStubBase *)__global_swstub_creator[map_id](
        mng, map_id, key, version, nullptr /* obj */, false /* init */,
        true /* is_const */);
  }

  static MwStubBase *GetMwStubBase(int map_id, const Key *key,
                                   MwStubManager *mng) {
    if (__global_mwstub_creator[map_id] == nullptr) {
      DEBUG_ERR("No MwStub creator is defined for " << map_id);
      return nullptr;
    }

    return (MwStubBase *)__global_mwstub_creator[map_id](map_id, key, mng);
  }

  static MWSkeleton *GetMWSkeleton(int map_id, const Key *key, void *obj,
                                   bool init) {
    if (__global_mwstub_creator[map_id] == nullptr) {
      DEBUG_ERR("No MWSkeleton creator is defined for " << map_id);
      return nullptr;
    }

    return (MWSkeleton *)__global_mwskeleton_creator[map_id](map_id, key, obj,
                                                             init);
  }
};

int __register_map(DObjType objtype, size_t size, const char *name);
void __register_swstub_creator(int map_id, swstub_creator fn);
void __register_mwstub_creator(int map_id, mwstub_creator fn);
void __register_mwskeleton_creator(int map_id, mwskeleton_creator fn);

#endif
