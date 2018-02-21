#include "sw_stub.hh"
#include "log.hh"
#include "swstub_manager.hh"

void SwStubBase::rpc(int map_id, const Key *key, int version, uint32_t flag,
                     uint32_t method_id, void *args, uint32_t args_size,
                     void *ret, uint32_t ret_size) const {
  swstub_manager->request_rpc(map_id, key, version, flag, method_id, args,
                              args_size, ret, ret_size);
  return;
}
