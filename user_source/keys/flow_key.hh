#ifndef _DISTREF_FLOWKEY_HH_
#define _DISTREF_FLOWKEY_HH_

#include <cassert>

#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>

#include "key_base.hh"

#define IP_PRINT(ip)                                                       \
  (ip >> 24 & 0xFF) << "." << (ip >> 16 & 0xFF) << "." << (ip >> 8 & 0xFF) \
                    << "." << (ip & 0xFF)

class FlowKey : public SticKey {
 private:
  uint32_t sip = 0;
  uint32_t dip = 0;
  uint16_t sp = 0;
  uint16_t dp = 0;

  std::ostream& _print(std::ostream& out) const {
    return out << IP_PRINT(sip) << ":" << sp << "->" << IP_PRINT(dip) << ":"
               << dp;
  };

  bool _lessthan(const Key& other) const {
    const FlowKey* fother = KEY_CAST(FlowKey, other);
    if (!fother) {
      DEBUG_ERR("Dynamic cast fail from Key to FlowKey");
      return false;
    }
    return (sip != fother->sip)
               ? (sip < fother->sip)
               : ((dip != fother->dip)
                      ? (dip < fother->dip)
                      : ((dp != fother->dp) ? (dp < fother->dp)
                                            : (sp < fother->sp)));
  };

  bool _equalto(const Key& other) const {
    const FlowKey* fother = KEY_CAST(FlowKey, other);
    if (!fother) {
      DEBUG_ERR("Dynamic cast fail from Key to FlowKey");
      return false;
    }
    return ((sip == fother->sip) && (dip == fother->dip) &&
            (sp == fother->sp) && (dp == fother->dp));
  };

 public:
  static KeyTypeID key_tid;

  FlowKey(uint32_t sip, uint32_t dip, uint16_t sp, uint16_t dp)
      : SticKey(), sip(sip), dip(dip), sp(sp), dp(dp){};

  FlowKey* clone() const { return new FlowKey(*this); }

  FlowKey* clone(size_t size, void* buf) const {
    if (size < sizeof(FlowKey))
      return nullptr;

    memcpy(buf, this, sizeof(FlowKey));
    return (FlowKey*)buf;
  }

  Archive* serialize() const {
    Archive* ar = (Archive*)malloc(sizeof(Archive) + sizeof(FlowKey));
    ar->size = sizeof(FlowKey);
    ar->class_type = _S6_KEY;
    ar->class_id = FlowKey::key_tid;
    new (ar->data) FlowKey(*this);
    return ar;
  };

  uint32_t get_key_size() const { return sizeof(FlowKey); };

  // XXX Need better hash function
  std::size_t _hash() const { return sip + dip + sp + dp; }

  uint8_t* get_bytes() const { return (uint8_t*)this; }

  uint32_t get_locality_hash() const {
    assert(policy);
    return policy->get_locality_hash(sip, dip, sp, dp);
  }

  static Key* unserialize(Archive* ar) {
    assert(ar->class_id == FlowKey::key_tid);
    return new FlowKey(*(FlowKey*)(void*)ar->data);
  }

  static FlowKey* create_key(struct rte_mbuf* mbuf) {
    struct ipv4_hdr* iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr*,
                                                   sizeof(struct ether_hdr));
    struct tcp_hdr* tcph =
        (struct tcp_hdr*)((u_char*)iph +
                          ((iph->version_ihl & IPV4_HDR_IHL_MASK) << 2));

    return new FlowKey(ntohl(iph->src_addr), ntohl(iph->dst_addr),
                       ntohs(tcph->src_port), ntohs(tcph->dst_port));
  }
};

#endif
