#ifndef _REASM_ENTRY_OB_HH_
#define _REASM_ENTRY_OB_HH_

#include <array>

#include "dist.hh"
#include "object_base.hh"

struct Segment {
  uint32_t seq;  // seq number 0 is reserved for "empty" in the array
  uint32_t pktbuf_key;
};

class ReasmEntry : public SWObject {
 public:
  struct Data {
    uint32_t next_expected_seq;
    uint32_t next_generated_seq;  // seq numbers to be added to planned_seqs
    std::array<uint32_t, 10> planned_seqs;
    std::array<Segment, 32> buffered_segs;
  };

  ReasmEntry::Data read() const { return data_; }

  void write(ReasmEntry::Data data) { data_ = data; }

 private:
  // to make StatelessNF friendly
  ReasmEntry::Data data_ = {};
};

#endif  // _REASM_ENTRY_OB_HH_
