
#ifndef _DISTREF_UDP_KEY_HH_
#define _DISTREF_UDP_KEY_HH_

#include <cassert>

#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include "key_base.hh"

#define IP_PRINT(ip)                                                       \
  (ip >> 24 & 0xFF) << "." << (ip >> 16 & 0xFF) << "." << (ip >> 8 & 0xFF) \
                    << "." << (ip & 0xFF)

class UDPKey : public SticKey {
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
    const UDPKey* fother = KEY_CAST(UDPKey, other);
    if (!fother) {
      DEBUG_ERR("Dynamic cast fail from Key to UDPKey");
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
    const UDPKey* fother = KEY_CAST(UDPKey, other);
    if (!fother) {
      DEBUG_ERR("Dynamic cast fail from Key to UDPKey");
      return false;
    }
    return ((sip == fother->sip) && (dip == fother->dip) &&
            (sp == fother->sp) && (dp == fother->dp));
  };

 public:
  static KeyTypeID key_tid;

  UDPKey(uint32_t sip, uint32_t dip, uint16_t sp, uint16_t dp)
      : SticKey(), sip(sip), dip(dip), sp(sp), dp(dp){};

  UDPKey* clone() const { return new UDPKey(*this); }

  UDPKey* clone(size_t size, void* buf) const {
    if (size < sizeof(UDPKey))
      return nullptr;

    memcpy(buf, this, sizeof(UDPKey));
    return (UDPKey*)buf;
  }

  Archive* serialize() const {
    Archive* ar = (Archive*)malloc(sizeof(Archive) + sizeof(UDPKey));
    ar->size = sizeof(UDPKey);
    ar->class_type = _S6_KEY;
    ar->class_id = UDPKey::key_tid;
    new (ar->data) UDPKey(*this);
    return ar;
  };

  uint32_t get_key_size() const { return sizeof(UDPKey); };

  // XXX Need better hash function
  std::size_t _hash() const { return sip + dip + sp + dp; }

  uint8_t* get_bytes() const { return (uint8_t*)this; }

  uint32_t get_locality_hash() const {
    assert(policy);
    return policy->get_locality_hash(sip, dip, sp, dp);
  }

  static Key* unserialize(Archive* ar) {
    assert(ar->class_id == UDPKey::key_tid);
    return new UDPKey(*(UDPKey*)(void*)ar->data);
  }

  static UDPKey* create_key(struct rte_mbuf* mbuf) {
    struct ipv4_hdr* iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr*,
                                                   sizeof(struct ether_hdr));
    struct udp_hdr* udph =
        (struct udp_hdr*)((u_char*)iph +
                          ((iph->version_ihl & IPV4_HDR_IHL_MASK) << 2));

    return new UDPKey(ntohl(iph->src_addr), ntohl(iph->dst_addr),
                      ntohs(udph->src_port), ntohs(udph->dst_port));
  }
};

#endif
