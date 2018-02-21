#ifndef _DISTREF_IPKEY_HH_
#define _DISTREF_IPKEY_HH_

#include <cassert>

#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>

#include "key_base.hh"

#define IP_PRINT(ip)                                                       \
  (ip >> 24 & 0xFF) << "." << (ip >> 16 & 0xFF) << "." << (ip >> 8 & 0xFF) \
                    << "." << (ip & 0xFF)

/* User-level implementation examples of Key Interface */
class IPKey : public SticKey {
 private:
  uint32_t ip;
  _KEY_LOC_ANNOT loc_annot;

  std::ostream &_print(std::ostream &out) const { return out << IP_PRINT(ip); };

  bool _lessthan(const Key &other) const {
    const IPKey *other_key = KEY_CAST(IPKey, other);
    if (!other_key) {
      DEBUG_ERR("Dynamic cast fail from Key to IPKey");
      return false;
    }
    return (ip < other_key->ip);
  };

  bool _equalto(const Key &other) const {
    const IPKey *other_key = KEY_CAST(IPKey, other);
    if (!other_key) {
      DEBUG_ERR("Dynamic cast fail from Key to IPKey");
      return false;
    }
    return (ip == other_key->ip);
  };

 public:
  static KeyTypeID key_tid;

  IPKey(uint32_t ip) : SticKey(), ip(ip), loc_annot(_LCAN_NONE){};

  IPKey(uint32_t ip, _KEY_LOC_ANNOT loc) : SticKey(), ip(ip), loc_annot(loc){};

  IPKey *clone() const { return new IPKey(*this); }

  IPKey *clone(size_t size, void *buf) const {
    if (size < sizeof(IPKey))
      return nullptr;
    memcpy(buf, this, sizeof(IPKey));
    return (IPKey *)buf;
  }

  Archive *serialize() const {
    Archive *ar = (Archive *)malloc(sizeof(Archive) + sizeof(IPKey));
    ar->size = sizeof(IPKey);
    ar->class_type = _S6_KEY;
    ar->class_id = IPKey::key_tid;
    new (ar->data) IPKey(*this);
    return ar;
  }

  uint32_t get_key_size() const { return sizeof(IPKey); };

  uint8_t *get_bytes() const { return (uint8_t *)this; }

  // XXX Need better hash function
  std::size_t _hash() const {
    // DEBUG_ERR(IP_PRINT(ip));
    uint32_t hash = 0;
    for (int i = 0; i < 4; ++i) {
      hash += (ip >> ((3 - i) * 8)) & 0xFF;
      hash += (hash << 10);
      hash ^= (hash << 6);
    }
    return hash;
  }

  uint32_t get_ip() const { return ip; }

  uint32_t get_locality_hash() const {
    if (loc_annot == _LCAN_NONE) {
      return _hash();
    }

    assert(policy);
    if (loc_annot == _LCAN_SRC)
      return policy->get_locality_hash(ip, 0, 0, 0);
    else
      return policy->get_locality_hash(0, ip, 0, 0);
  }

  static Key *unserialize(Archive *ar) {
    assert(ar->class_id == IPKey::key_tid);
    return new IPKey(*(IPKey *)(void *)ar->data);
  }

  static IPKey *create_key_src(struct rte_mbuf *mbuf) {
    struct ipv4_hdr *iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr *,
                                                   sizeof(struct ether_hdr));
    return new IPKey(ntohl(iph->src_addr), _LCAN_SRC);
  }

  static IPKey *create_key_dst(struct rte_mbuf *mbuf) {
    struct ipv4_hdr *iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr *,
                                                   sizeof(struct ether_hdr));
    return new IPKey(ntohl(iph->dst_addr), _LCAN_DST);
  }
};

#endif
