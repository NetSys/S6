#ifndef __DISTREF_MW_SKELETON_HH_
#define __DISTREF_MW_SKELETON_HH_

#include "key.hh"
#include "mw_object.hh"
#include <sys/queue.h>

class MWSkeleton {
 private:
 public:
  uint64_t last_updated;
  TAILQ_ENTRY(MWSkeleton) aggr_elem;
  bool in_list = false;

  int _map_id;
  const Key *_key;
  MWObject *_obj = nullptr;

  MWSkeleton(){};
  virtual ~MWSkeleton(){};
  uint32_t get_size() { return _obj->_get_size(); };
  void *get_bytes() { return _obj->_get_bytes(); };

  /* Execute method(_id) on object(_id) */
  virtual void exec(uint32_t method_id, void *args, void **ret,
                    uint32_t *ret_size) = 0;

#if 0
		virtual int create_iterator(int _map_id) = 0;
		virtual void release_iterator(int _map_id, int _itidx) = 0;
		virtual const Key* get_next_key(int _map_id, int _itidx) = 0;
#endif
};

template <class X>
class Skeleton : MWSkeleton {
 public:
  static MWSkeleton *CreateSkeleton(int map_id, const Key *key, void *obj,
                                    bool init) {
    return nullptr;
  }
};

#endif
