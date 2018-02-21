#ifndef _PORT_POOL_OB_HH_
#define _PORT_POOL_OB_HH_

#include <bitset>

#include "dist.hh"
#include "object_base.hh"

using PortBitmap = std::bitset<65536>;

class PortPool : public SWObject {
 public:
  struct Data {
    // in host order
    uint32_t public_ip;
    PortBitmap port_used;
  };

  PortPool::Data read() const { return data_; }

  void write(PortPool::Data data) { data_ = data; }

 private:
  // to make StatelessNF friendly
  PortPool::Data data_ = {
      .public_ip = 0x0a000001, .port_used = {},
  };
};

#endif  // _PORT_POOL_OB_HH_
