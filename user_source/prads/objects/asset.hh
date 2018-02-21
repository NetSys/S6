#ifndef _ASSET_OB_HH_
#define _ASSET_OB_HH_

#include <string.h>
#include <time.h>

#include "dist.hh"
#include "object_base.hh"
#include "stub.os_asset.hh"
#include "stub.serv_asset.hh"
#define MAC_ADDR_LEN 6
#define MAX_ASSET 5

#define CMIN(a, b) (((a) < (b)) ? (a) : (b))
#define CMAX(a, b) (((a) > (b)) ? (a) : (b))

template <class X>
class SwRef;
class OSAsset;
class ServiceAsset;

class Asset : public MWObject {
 private:
  struct {
    time_t first_seen;              /* Time at which asset was first seen. */
    time_t last_seen;               /* Time at which asset was last seen. */
    unsigned short i_attempts;      /* Attempts at identifying the asset. */
    int af;                         /* IP AF_INET */
    uint16_t vlan;                  /* vlan tag */
    uint32_t ip_addr;               /* IP asset address */
    uint8_t mac_addr[MAC_ADDR_LEN]; /* Asset MAC address */
    // mac_entry *macentry;        /* Asset MAC vendor name */
    SwRef<ServiceAsset> service_list; /* Linked list with services detected */
    SwRef<OSAsset> os_list;           /* Linked list with OSes detected */
  } asset;

 public:
  uint32_t _get_size() { return 0; }

  char *_get_bytes() { return nullptr; }

  static Asset *_get_object(uint32_t obj_size, char *obj) { return nullptr; }

  void _init() {
    asset.i_attempts = 0;
    asset.service_list = nullptr;
    asset.os_list = nullptr;
  }

  void _add(Asset other) {
    asset.first_seen = CMIN(asset.first_seen, other.asset.first_seen);
    asset.last_seen = CMAX(asset.last_seen, other.asset.last_seen);
    asset.i_attempts = asset.i_attempts + other.asset.i_attempts;

    // XXX merge linked list
  }

  void init_asset(int af, uint16_t vlan, time_t now, uint32_t ip) _behind {
    asset.af = af;
    asset.vlan = vlan;
    asset.i_attempts = 0;
    asset.first_seen = asset.last_seen = now;
    asset.ip_addr = ip;
  }

  void update_asset(uint16_t vlan, time_t now) _behind {
    asset.vlan = vlan;
    asset.last_seen = now;
  }

  void update_asset_os(uint8_t detection, uint16_t sigidx, uint64_t cur_time) {
    SwRef<OSAsset> *cur = &asset.os_list;
    while (*cur != nullptr) {
      if ((*cur)->get_sigidx() == sigidx) {
        (*cur)->update_asset_os(cur_time);
        break;
      }
      cur = (*cur)->get_next();
    }

    if (*cur == nullptr) {
      *cur = SwRef<OSAsset>(new OSAsset());
      (*cur)->init_asset_os(detection, sigidx, cur_time);
    }
  }

  void update_asset_service(uint16_t port, uint8_t proto, uint8_t role,
                            uint16_t sigidx, uint64_t cur_time) {
    SwRef<ServiceAsset> *cur = &asset.service_list;
    while (*cur != nullptr) {
      if (sigidx == -1) {
        if ((*cur)->get_sigidx() == sigidx && (*cur)->get_port() == port) {
          (*cur)->update_asset_service(cur_time);
          break;
        }
      } else {
        if ((*cur)->get_sigidx() == sigidx) {
          (*cur)->update_asset_service(cur_time);
          break;
        }
      }

      cur = (*cur)->get_next();
    }

    if (*cur == nullptr) {
      *cur = SwRef<ServiceAsset>(new ServiceAsset());
      (*cur)->init_asset_service(port, proto, role, sigidx, cur_time);
    }
  }

  time_t get_last_seen() const _stale { return asset.last_seen; }
};

#endif
