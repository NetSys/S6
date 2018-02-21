#ifndef _PACKET_BUF_OB_HH_
#define _PACKET_BUF_OB_HH_

#include <bitset>

#include "dist.hh"
#include "object_base.hh"

class PacketBuf : public SWObject {
 public:
  struct Data {
    uint32_t seq;
    char frame[1518];
  };

  PacketBuf::Data read() const { return data_; }

  void write(PacketBuf::Data data) { data_ = data; }

 private:
  // to make StatelessNF friendly
  PacketBuf::Data data_ = {};
};

#endif  // _PACKET_BUF_OB_HH_
