#ifndef _TCP_FLOW_OB_HH_
#define _TCP_FLOW_OB_HH_

#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>

#include "dist.hh"
#include "object_base.hh"

#include "flow_context.hh"
#include "stub.flow_context.hh"

class TCPFlow : public SWObject {
 private:
  bool set = false;

  uint32_t svr_ip;
  uint32_t cli_ip;
  uint16_t svr_port;
  uint16_t cli_port;

  struct timespec start_time;
  struct timespec end_time;
  uint32_t pkt_cnt;

  SwRef<FlowContext> s2c_ctx;
  SwRef<FlowContext> c2s_ctx;

  inline void _init(uint32_t svr_ip, uint32_t cli_ip, uint16_t svr_port,
                    uint16_t cli_port) {
    set = true;
    getCurTime(&start_time);

    this->svr_ip = svr_ip;
    this->cli_ip = cli_ip;
    this->svr_port = svr_port;
    this->cli_port = cli_port;

    s2c_ctx = SwRef<FlowContext>(new FlowContext());
    c2s_ctx = SwRef<FlowContext>(new FlowContext());
  }

 public:
  uint32_t get_svr_ip() const { return svr_ip; }
  uint32_t get_cli_ip() const { return cli_ip; }
  uint16_t get_svr_port() const { return svr_port; }
  uint16_t get_cli_port() const { return cli_port; }

  bool is_equal(const TCPFlow &other) {
    return (this->svr_ip == other.svr_ip) && (this->cli_ip == other.cli_ip) &&
           (this->svr_port == other.svr_port) &&
           (this->cli_port == other.cli_port);
  }

  bool is_set() { return set; }
  void init_c2s(struct rte_mbuf *mbuf) {
    struct ipv4_hdr *iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr *,
                                                   sizeof(struct ether_hdr));
    struct tcp_hdr *tcph =
        (struct tcp_hdr *)((u_char *)iph +
                           ((iph->version_ihl & IPV4_HDR_IHL_MASK) << 2));

    if (!set)
      _init(iph->dst_addr, iph->src_addr, tcph->dst_port, tcph->src_port);

    c2s_ctx->set_init_seq(tcph->sent_seq);
  }

  void init_s2c(struct rte_mbuf *mbuf) {
    struct ipv4_hdr *iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr *,
                                                   sizeof(struct ether_hdr));
    struct tcp_hdr *tcph =
        (struct tcp_hdr *)((u_char *)iph +
                           ((iph->version_ihl & IPV4_HDR_IHL_MASK) << 2));

    if (!set)
      _init(iph->src_addr, iph->dst_addr, tcph->src_port, tcph->dst_port);

    c2s_ctx->set_init_seq(tcph->sent_seq);
  }

  void close_s2c(struct rte_mbuf *mbuf) { getCurTime(&end_time); }

  void close_c2s(struct rte_mbuf *mbuf) { getCurTime(&end_time); }

  void update_context(struct rte_mbuf *mbuf) {
    if (!set)
      return;

    pkt_cnt++;

    struct ipv4_hdr *iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr *,
                                                   sizeof(struct ether_hdr));
    struct tcp_hdr *tcph =
        (struct tcp_hdr *)((u_char *)iph +
                           ((iph->version_ihl & IPV4_HDR_IHL_MASK) << 2));
    uint8_t *payload = (uint8_t *)tcph + (tcph->data_off << 2);
    uint32_t payload_len =
        ntohs(iph->total_length) - (payload - (uint8_t *)iph);

    // xxx check the meaning of "length"
    if (iph->src_addr == svr_ip)
      s2c_ctx->update_total_bytes(payload_len);
    else
      c2s_ctx->update_total_bytes(payload_len);
  }

  uint32_t get_total_bytes() const {
    if (!set)
      return 0;

    return s2c_ctx->get_total_bytes() + c2s_ctx->get_total_bytes();
  }
};

#endif
