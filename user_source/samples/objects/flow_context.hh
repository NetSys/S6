#ifndef _FLOW_CONTEXT_OB_H
#define _FLOW_CONTEXT_OB_H

#include "object_base.hh"

class FlowContext : public SWObject {
 private:
  uint32_t init_seq = 0;
  uint32_t total_bytes = 0;

 public:
  void set_init_seq(uint32_t init_seq) { this->init_seq = init_seq; }
  uint32_t get_init_seq() const { return init_seq; }

  void update_total_bytes(uint32_t bytes) { this->total_bytes += bytes; }
  uint32_t get_total_bytes() const { return total_bytes; }
};

#endif
