#ifndef _FLOW_OB_HH_
#define _FLOW_OB_HH_

#include <iostream>
#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <stdint.h>

#include "object_base.hh"

class SimpleFlow : public SWObject {
 private:
  bool set = false;

  uint32_t src_ip;
  uint32_t dst_ip;
  uint16_t src_port;
  uint16_t dst_port;

  uint32_t count = 0;
  uint64_t bytes = 0;

 public:
  bool is_set() const { return set; };

  int setup(struct rte_mbuf *mbuf) {
    set = true;

    struct ipv4_hdr *iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr *,
                                                   sizeof(struct ether_hdr));
    struct tcp_hdr *tcph =
        (struct tcp_hdr *)((u_char *)iph +
                           ((iph->version_ihl & IPV4_HDR_IHL_MASK) << 2));

    src_ip = iph->src_addr;
    dst_ip = iph->dst_addr;
    src_port = tcph->src_port;
    dst_port = tcph->dst_port;

    return set;
  };

  void add_count(uint32_t count) { this->count += count; };

  void add_bytes(uint64_t bytes) { this->bytes += bytes; };

  uint32_t get_count() const { return count; };
  uint64_t get_bytes() const { return bytes; };

  friend std::ostream &operator<<(std::ostream &out, const SimpleFlow &flow) {
    return out << flow.src_ip << ":" << flow.src_port << " -> " << flow.src_port
               << ":" << flow.dst_port;
  }
};

#endif
