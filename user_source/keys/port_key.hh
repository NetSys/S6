#ifndef _DISTREF_PORTKEY_HH_
#define _DISTREF_PORTKEY_HH_

#include <rte_common.h>
#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>

#include "key_base.hh"

/* User-level implementation examples of Key Interface */
class PortKey : public SticKey {
 private:
  uint16_t port;
  _KEY_LOC_ANNOT loc_annot;

  std::ostream& _print(std::ostream& out) const { return out << port; };

  bool _lessthan(const Key& other) const {
    const PortKey* pother = KEY_CAST(PortKey, other);
    if (!pother) {
      DEBUG_ERR("Dynamic cast fail from Key to PortKey");
      return false;
    }
    return (port < pother->port);
  };

  bool _equalto(const Key& other) const {
    const PortKey* pother = KEY_CAST(PortKey, other);
    if (!pother) {
      DEBUG_ERR("Dynamic cast fail from Key to PortKey");
      return false;
    }
    return (port == pother->port);
  };

 public:
  static KeyTypeID key_tid;

  PortKey(uint16_t p) : SticKey(), port(p), loc_annot(_LCAN_NONE){};

  PortKey(uint16_t p, _KEY_LOC_ANNOT loc)
      : SticKey(), port(p), loc_annot(loc){};

  PortKey* clone() const { return new PortKey(*this); }

  PortKey* clone(size_t size, void* buf) const {
    if (size < sizeof(PortKey))
      return nullptr;

    memcpy(buf, this, sizeof(PortKey));
    return (PortKey*)buf;
  }

  Archive* serialize() const {
    Archive* ar = (Archive*)malloc(sizeof(Archive) + sizeof(PortKey));
    ar->size = sizeof(PortKey);
    ar->class_type = _S6_KEY;
    ar->class_id = PortKey::key_tid;
    new (ar->data) PortKey(*this);
    return ar;
  }

  uint32_t get_key_size() const { return sizeof(PortKey); };

  uint8_t* get_bytes() const { return (uint8_t*)this; }

  // XXX Need better hash function
  std::size_t _hash() const { return port; }

  uint16_t get_port() const { return port; }

  uint32_t get_locality_hash() const {
    if (loc_annot == _LCAN_NONE) {
      DEBUG_ERR("No location annotation\n");
      return -1;
    }

    assert(policy);
    if (loc_annot == _LCAN_SRC)
      return policy->get_locality_hash(0, 0, port, 0);
    else
      return policy->get_locality_hash(0, 0, 0, port);
  }

  static Key* unserialize(Archive* ar) {
    assert(ar->class_id == PortKey::key_tid);
    return new PortKey(*(PortKey*)(void*)ar->data);
  }

  static PortKey* create_key_sp(struct rte_mbuf* mbuf) {
    struct ipv4_hdr* iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr*,
                                                   sizeof(struct ether_hdr));
    struct tcp_hdr* tcph =
        (struct tcp_hdr*)((u_char*)iph +
                          ((iph->version_ihl & IPV4_HDR_IHL_MASK) << 2));

    return new PortKey(ntohs(tcph->src_port), _LCAN_SRC);
  }

  static PortKey* create_key_dp(struct rte_mbuf* mbuf) {
    struct ipv4_hdr* iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr*,
                                                   sizeof(struct ether_hdr));
    struct tcp_hdr* tcph =
        (struct tcp_hdr*)((u_char*)iph +
                          ((iph->version_ihl & IPV4_HDR_IHL_MASK) << 2));

    return new PortKey(ntohs(tcph->dst_port), _LCAN_DST);
  }
};

#endif
