#ifndef _IDS_CONFIG_HH_
#define _IDS_CONFIG_HH_

#include "object_base.hh"

class IDSConfig : public SWObject {
 private:
  long pass_rate = 0;

 public:
  bool is_pass(struct rte_mbuf *mbuf) const _stale { return true; }

  void update() { pass_rate += 0.1; }
};

#endif
