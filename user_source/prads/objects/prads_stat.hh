#ifndef _PRADS_STAT_HH_
#define _PRADS_STAT_HH_

#include <string.h>

#include "object_base.hh"

struct s6_prads_stat {
  uint32_t got_packets;   /* number of packets received by prads */
  uint32_t eth_recv;      /* number of Ethernet packets received */
  uint32_t arp_recv;      /* number of ARP packets received */
  uint32_t otherl_recv;   /* number of other Link layer packets received */
  uint32_t vlan_recv;     /* number of VLAN packets received */
  uint32_t ip4_recv;      /* number of IPv4 packets received */
  uint32_t ip6_recv;      /* number of IPv6 packets received */
  uint32_t ip4ip_recv;    /* number of IP4/6 packets in IPv4 packets */
  uint32_t ip6ip_recv;    /* number of IP4/6 packets in IPv6 packets */
  uint32_t gre_recv;      /* number of GRE packets received */
  uint32_t tcp_recv;      /* number of tcp packets received */
  uint32_t udp_recv;      /* number of udp packets received */
  uint32_t icmp_recv;     /* number of icmp packets received */
  uint32_t othert_recv;   /* number of other transport layer packets received */
  uint32_t assets;        /* total number of assets detected */
  uint32_t tcp_os_assets; /* total number of tcp os assets detected */
  uint32_t udp_os_assets; /* total number of udp os assets detected */
  uint32_t icmp_os_assets; /* total number of icmp os assets detected */
  uint32_t dhcp_os_assets; /* total number of dhcp os assets detected */
  uint32_t tcp_services;   /* total number of tcp services detected */
  uint32_t tcp_clients;    /* total number of tcp clients detected */
  uint32_t udp_services;   /* total number of udp services detected */
  uint32_t udp_clients;    /* total number of tcp clients detected */
};

class PRADSStat : public MWObject {
 private:
  struct s6_prads_stat prads_stat;

 public:
  uint32_t _get_size() { return 0; }

  char *_get_bytes() { return nullptr; }

  static PRADSStat *_get_object(uint32_t obj_size, char *obj) {
    return nullptr;
  }

  void _init() { prads_stat = {0}; }

  void _add(PRADSStat other) {
    struct s6_prads_stat s = other.get_prads_stat();
    this->prads_stat.got_packets += s.got_packets;
    this->prads_stat.eth_recv += s.eth_recv;
    this->prads_stat.arp_recv += s.arp_recv;
    this->prads_stat.otherl_recv += s.otherl_recv;
    this->prads_stat.vlan_recv += s.vlan_recv;
    this->prads_stat.ip4_recv += s.ip4_recv;
    this->prads_stat.ip6_recv += s.ip6_recv;
    this->prads_stat.ip4ip_recv += s.ip4ip_recv;
    this->prads_stat.ip6ip_recv += s.ip6ip_recv;
    this->prads_stat.gre_recv += s.gre_recv;
    this->prads_stat.tcp_recv += s.tcp_recv;
    this->prads_stat.udp_recv += s.udp_recv;
    this->prads_stat.icmp_recv += s.icmp_recv;
    this->prads_stat.othert_recv += s.othert_recv;
    this->prads_stat.assets += s.assets;
    this->prads_stat.tcp_os_assets += s.tcp_os_assets;
    this->prads_stat.udp_os_assets += s.udp_os_assets;
    this->prads_stat.icmp_os_assets += s.icmp_os_assets;
    this->prads_stat.dhcp_os_assets += s.dhcp_os_assets;
    this->prads_stat.tcp_services += s.tcp_services;
    this->prads_stat.tcp_clients += s.tcp_clients;
    this->prads_stat.udp_services += s.udp_services;
    this->prads_stat.udp_clients += s.udp_clients;
  };

  struct s6_prads_stat get_prads_stat() const _stale {
    return prads_stat;
  }

  void inc_got_packets() _behind { prads_stat.got_packets++; };

  void inc_eth_recv() _behind { prads_stat.eth_recv++; };

  void inc_arp_recv() _behind { prads_stat.arp_recv++; };

  void inc_otherl_recv() _behind { prads_stat.otherl_recv++; };

  void inc_vlan_recv() _behind { prads_stat.vlan_recv++; };

  void inc_ip4_recv() _behind { prads_stat.ip4_recv++; };

  void inc_ip6_recv() _behind { prads_stat.ip6_recv++; };

  void inc_ip4ip_recv() _behind { prads_stat.ip4ip_recv++; };

  void inc_ip6ip_recv() _behind { prads_stat.ip6ip_recv++; };

  void inc_gre_recv() _behind { prads_stat.gre_recv++; };

  void inc_tcp_recv() _behind { prads_stat.tcp_recv++; };

  void inc_udp_recv() _behind { prads_stat.udp_recv++; };

  void inc_icmp_recv() _behind { prads_stat.icmp_recv++; };

  void inc_othert_recv() _behind { prads_stat.othert_recv++; };

  void inc_assets() _behind { prads_stat.assets++; };

  void inc_tcp_os_assets() _behind { prads_stat.tcp_os_assets++; };

  void inc_udp_os_assets() _behind { prads_stat.udp_os_assets++; };

  void inc_icmp_os_assets() _behind { prads_stat.icmp_os_assets++; };

  void inc_dhcp_os_assets() _behind { prads_stat.dhcp_os_assets++; };

  void inc_tcp_services() _behind { prads_stat.tcp_services++; };

  void inc_tcp_clients() _behind { prads_stat.tcp_clients++; };

  void inc_udp_services() _behind { prads_stat.tcp_services++; };

  void inc_udp_clients() _behind { prads_stat.tcp_clients++; };
};

#endif
