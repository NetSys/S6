#ifndef __DISTREF_SW_STUB_HH_
#define __DISTREF_SW_STUB_HH_

#include <iostream>

#include "key.hh"

class SWObject;
class Key;
class SwStubManager;

class SwStubBase {
 private:
  SwStubManager *swstub_manager;  // XXX want to remove it

 public:
  SwStubBase() { swstub_manager = nullptr; };
  SwStubBase(SwStubManager *_swstub_manager)
      : swstub_manager(_swstub_manager){};
  virtual ~SwStubBase(){};

  SWObject *_obj = nullptr;

  int _map_id;
  const Key *_key;
  bool _is_const;
  int _obj_version = -1;
  int _obj_size = -1;  // XXX Do we really need a size?

  void rpc(int map_id, const Key *key, int version, uint32_t flag,
           uint32_t method_id, void *args, uint32_t args_size, void *ret,
           uint32_t ret_size) const;

  virtual void exec(int method_id, void *args, uint32_t args_size, void **ret,
                    uint32_t *ret_size) = 0;
};
#endif
