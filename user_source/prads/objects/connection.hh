#ifndef _CONNECTION_OB_HH_
#define _CONNECTION_OB_HH_

#include <iostream>
#include <stdint.h>

#include "../src/prads.h"
#include "object_base.hh"

/* necessary to handle DistRef<Asset> correctly */
template <class X>
class MwRef;
class Asset;

class Connection : public SWObject {
 private:
  struct _cxt {
    time_t start_time;    /* connection start time */
    time_t last_pkt_time; /* last seen packet time */
    // uint64_t cxid;                /* connection id */
    uint8_t reversed;       /* 1 if the connection is reversed */
    uint32_t af;            /* IP version (4/6) AF_INET */
    uint16_t hw_proto;      /* layer2 protocol */
    uint8_t proto;          /* IP protocoll type */
    uint32_t s_ip;          /* source address */
    uint32_t d_ip;          /* destination address */
    uint16_t s_port;        /* source port */
    uint16_t d_port;        /* destination port */
    uint64_t s_total_pkts;  /* total source packets */
    uint64_t s_total_bytes; /* total source bytes */
    uint64_t d_total_pkts;  /* total destination packets */
    uint64_t d_total_bytes; /* total destination bytes */
    uint8_t s_tcpFlags;     /* tcpflags sent by source */
    // uint8_t  __pad__;             /* pads struct to alignment */
    uint8_t d_tcpFlags;  /* tcpflags sent by destination */
    uint8_t check;       /* Flags spesifying checking */
    MwRef<Asset> casset; /* pointer to src asset */
    MwRef<Asset> sasset; /* pointer to server asset */
  } cxt;

 public:
  uint32_t get_src_ip() { return cxt.s_ip; }
  uint32_t get_dst_ip() { return cxt.d_ip; }
  uint16_t get_src_port() { return cxt.s_port; }
  uint16_t get_dst_port() { return cxt.d_port; }
  uint8_t get_check() { return cxt.check; }
  void set_check(uint8_t check) { cxt.check |= check; }

  bool is_reversed() { return cxt.reversed; }

  MwRef<Asset> get_casset() { return cxt.casset; }
  MwRef<Asset> get_sasset() { return cxt.sasset; }

  void init_cxt(packetinfo *pi) {
    // cxtrackerid++;
    // cxt.cxid = cxtrackerid;

    cxt.af = pi->af;
    if (pi->tcph)
      cxt.s_tcpFlags = pi->tcph->t_flags;
    cxt.s_total_bytes = pi->packet_bytes;
    cxt.s_total_pkts = 1;
    cxt.start_time = pi->pheader->ts.tv_sec;
    cxt.last_pkt_time = pi->pheader->ts.tv_sec;

    cxt.s_ip = pi->ip4->ip_src;
    cxt.d_ip = pi->ip4->ip_dst;
    cxt.s_port = pi->s_port;
    cxt.d_port = pi->d_port;
    cxt.proto = pi->proto;
    cxt.hw_proto = ntohs(pi->eth_type);

    cxt.check = 0x00;
    cxt.reversed = 0;
  }

  void reverse_pi_cxt() {
    uint8_t tmpFlags;
    uint64_t tmp_pkts;
    uint64_t tmp_bytes;
    uint32_t tmp_ip;
    uint16_t tmp_port;

    /* First we chang the cxt */
    /* cp src to tmp */
    tmpFlags = cxt.s_tcpFlags;
    tmp_pkts = cxt.s_total_pkts;
    tmp_bytes = cxt.s_total_bytes;
    tmp_ip = cxt.s_ip;
    tmp_port = cxt.s_port;

    /* cp dst to src */
    cxt.s_tcpFlags = cxt.d_tcpFlags;
    cxt.s_total_pkts = cxt.d_total_pkts;
    cxt.s_total_bytes = cxt.d_total_bytes;
    cxt.s_ip = cxt.d_ip;
    cxt.s_port = cxt.d_port;

    /* cp tmp to dst */
    cxt.d_tcpFlags = tmpFlags;
    cxt.d_total_pkts = tmp_pkts;
    cxt.d_total_bytes = tmp_bytes;
    cxt.d_ip = tmp_ip;
    cxt.d_port = tmp_port;

    /* Not taking any chances :P */
    cxt.check = 0x00;
  }

  void cxt_update_client(packetinfo *pi) {
    cxt.last_pkt_time = pi->pheader->ts.tv_sec;

    if (pi->tcph)
      cxt.s_tcpFlags |= pi->tcph->t_flags;
    cxt.s_total_bytes += pi->packet_bytes;
    cxt.s_total_pkts += 1;

    pi->sc = SC_CLIENT;
    if (!cxt.casset)
      cxt.casset = pi->asset;  // connection client asset
  }

  void cxt_update_server(packetinfo *pi) {
    cxt.last_pkt_time = pi->pheader->ts.tv_sec;

    if (pi->tcph)
      cxt.d_tcpFlags |= pi->tcph->t_flags;
    cxt.d_total_bytes += pi->packet_bytes;
    cxt.d_total_pkts += 1;

    pi->sc = SC_SERVER;
    if (!cxt.sasset)
      cxt.sasset = pi->asset;  // server asset
  }

  friend std::ostream &operator<<(std::ostream &out, const Connection &flow) {
    return out << "Connection";
  }
};

#endif
