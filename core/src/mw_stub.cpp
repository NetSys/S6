#include "mw_stub.hh"
#include "mwstub_manager.hh"

void MwStubBase::rpc(int map_id, const Key *key,
                     __attribute__((unused)) int version, uint32_t flag,
                     uint32_t method_id, void *args, size_t args_size,
                     void *ret, size_t ret_size) const {
  mwstub_manager->request_rpc(map_id, key, flag, method_id, args, args_size,
                              ret, ret_size);
}
