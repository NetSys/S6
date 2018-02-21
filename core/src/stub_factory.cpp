#include "stub_factory.hh"

int num_dmap = 0;
DObjType __global_dobj_type[_MAX_DMAPS] = {static_cast<DObjType>(0)};
size_t __global_dobj_size[_MAX_DMAPS] = {0};
const char *__global_dobj_name[_MAX_DMAPS];

swstub_creator __global_swstub_creator[_MAX_DMAPS] = {0};
mwstub_creator __global_mwstub_creator[_MAX_DMAPS] = {0};
mwskeleton_creator __global_mwskeleton_creator[_MAX_DMAPS] = {0};

int __register_map(DObjType objtype, size_t size, const char *name) {
  int map_id = num_dmap++;
  __global_dobj_type[map_id] = objtype;
  __global_dobj_size[map_id] = size;
  __global_dobj_name[map_id] = name;

  return map_id;
};

void __register_swstub_creator(int map_id, swstub_creator fn) {
  __global_swstub_creator[map_id] = fn;
};

void __register_mwstub_creator(int map_id, mwstub_creator fn) {
  __global_mwstub_creator[map_id] = fn;
};

void __register_mwskeleton_creator(int map_id, mwskeleton_creator fn) {
  __global_mwskeleton_creator[map_id] = fn;
};
