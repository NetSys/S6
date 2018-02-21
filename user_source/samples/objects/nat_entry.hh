#ifndef _NAT_ENTRY_OB_HH_
#define _NAT_ENTRY_OB_HH_

#include "dist.hh"
#include "object_base.hh"

class NatEntry : public SWObject {
 public:
  struct Data {
    // in host order
    uint32_t new_ip;
    uint16_t new_port;
    bool is_forward;
  };

  NatEntry::Data read() const { return data_; }

  void write(NatEntry::Data data) { data_ = data; }

 private:
  // to make StatelessNF friendly
  NatEntry::Data data_ = {};
};

#endif  // _NAT_ENTRY_OB_HH_
