#ifndef _DISTREF_SUBNETKEY_HH_
#define _DISTREF_SUBNETKEY_HH_

#include <cassert>

#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>

#include "key_base.hh"

#define IP_PRINT(ip)                                                       \
  (ip >> 24 & 0xFF) << "." << (ip >> 16 & 0xFF) << "." << (ip >> 8 & 0xFF) \
                    << "." << (ip & 0xFF)

/* User-level implementation examples of Key Interface */
class SubnetKey : public SticKey {
 private:
  uint32_t ip;
  uint16_t subnet;

  _KEY_LOC_ANNOT loc_annot;

  std::ostream& _print(std::ostream& out) const {
    return out << IP_PRINT(ip) << "/" << subnet << " -> "
               << IP_PRINT((ip >> (32 - subnet) << (32 - subnet)));
  };

  bool _lessthan(const Key& other) const {
    const SubnetKey* other_key = KEY_CAST(SubnetKey, other);
    if (!other_key) {
      DEBUG_ERR("Dynamic cast fail from Key to SubnetKey");
      return false;
    }

    return (subnet != other_key->subnet)
               ? (subnet < other_key->subnet)
               : ((ip >> (32 - subnet)) < (other_key->ip >> (32 - subnet)));
  };

  bool _equalto(const Key& other) const {
    const SubnetKey* other_key = KEY_CAST(SubnetKey, other);
    if (!other_key) {
      DEBUG_ERR("Dynamic cast fail from Key to SubnetKey");
      return false;
    }
    return (subnet == other_key->subnet) &&
           ((ip >> (32 - subnet)) == (other_key->ip >> (32 - subnet)));
  };

 public:
  static KeyTypeID key_tid;

  SubnetKey(uint32_t ip, uint16_t subnet)
      : SticKey(), ip(ip), subnet(subnet), loc_annot(_LCAN_NONE){};

  SubnetKey(uint32_t ip, uint16_t subnet, _KEY_LOC_ANNOT loc)
      : SticKey(), ip(ip), subnet(subnet), loc_annot(loc){};

  SubnetKey* clone() const { return new SubnetKey(*this); }

  SubnetKey* clone(size_t size, void* buf) const {
    if (size < sizeof(SubnetKey))
      return nullptr;

    memcpy(buf, this, sizeof(SubnetKey));
    return (SubnetKey*)buf;
  }

  Archive* serialize() const {
    Archive* ar = (Archive*)malloc(sizeof(Archive) + sizeof(SubnetKey));
    ar->size = sizeof(SubnetKey);
    ar->class_type = _S6_KEY;
    ar->class_id = SubnetKey::key_tid;
    new (ar->data) SubnetKey(*this);
    return ar;
  }

  uint32_t get_key_size() const { return sizeof(SubnetKey); };

  uint8_t* get_bytes() const { return (uint8_t*)this; }

  // XXX Need better hash function
  std::size_t _hash() const {
    // DEBUG_ERR(IP_PRINT(ip));
    return ip >> (32 - subnet);
  }

  uint32_t get_ip() const { return ip; }

  uint32_t get_locality_hash() const {
    if (loc_annot == _LCAN_NONE) {
      DEBUG_ERR("No location annotation\n");
      return -1;
    }

    assert(policy);
    if (loc_annot == _LCAN_SRC)
      return policy->get_locality_hash(ip, 0, 0, 0);
    else
      return policy->get_locality_hash(0, ip, 0, 0);
  }

  static Key* unserialize(Archive* ar) {
    assert(ar->class_id == SubnetKey::key_tid);
    return new SubnetKey(*(SubnetKey*)(void*)ar->data);
  }

  static SubnetKey* create_key_src(struct rte_mbuf* mbuf, uint16_t subnet) {
    struct ipv4_hdr* iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr*,
                                                   sizeof(struct ether_hdr));
    return new SubnetKey(ntohl(iph->src_addr), subnet, _LCAN_SRC);
  }

  static SubnetKey* create_key_dst(struct rte_mbuf* mbuf, uint16_t subnet) {
    struct ipv4_hdr* iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr*,
                                                   sizeof(struct ether_hdr));
    return new SubnetKey(ntohl(iph->dst_addr), subnet, _LCAN_DST);
  }
};

#endif
