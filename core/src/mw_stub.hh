#ifndef __DISTREF_MW_STUB_HH_
#define __DISTREF_MW_STUB_HH_

#include <cassert>

#include "log.hh"

class Key;
class MwStubManager;

class MwStubBase {
 private:
  MwStubManager *mwstub_manager;  // XXX want to remove it

 public:
  int _obj_version = -1;

  MwStubBase() { mwstub_manager = nullptr; };
  MwStubBase(MwStubManager *_mwstub_manager)
      : mwstub_manager(_mwstub_manager) {}

  virtual ~MwStubBase(){};

  void rpc(int map_id, const Key *key, int version, uint32_t flag,
           uint32_t method_id, void *args, size_t args_size, void *ret,
           size_t ret_size) const;
};

#endif
