#ifndef _UDP_COUNTER_16KB_OB_HH_
#define _UDP_COUNTER_16KB_OB_HH_

#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>

#include "dist.hh"
#include "object_base.hh"

class UDPCounter16KB : public SWObject {
 private:
  uint32_t svr_ip;
  uint32_t cli_ip;
  uint16_t svr_port;
  uint16_t cli_port;

  uint32_t pkt_cnt;
  uint64_t bytes;
  uint64_t dummy;
  // 32 bytes upto here

  char array[16352];

 public:
  uint32_t get_svr_ip() const { return svr_ip; }
  uint32_t get_cli_ip() const { return cli_ip; }
  uint16_t get_svr_port() const { return svr_port; }
  uint16_t get_cli_port() const { return cli_port; }

  uint32_t get_pkt_cnt() const { return pkt_cnt; }
  uint64_t get_bytes() const { return bytes; }

  void inc_pkt_cnt() { this->pkt_cnt++; }
  void add_bytes(uint32_t bytes) { this->bytes += bytes; }
};

#endif
