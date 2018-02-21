#ifndef _SERVICE_ASSET_OB_HH_
#define _SERVICE_ASSET_OB_HH_

#include <string.h>
#include <time.h>

#include "dist.hh"
#include "object_base.hh"

template <class X>
class SwRef;
class ServiceAsset;

struct s6_service_asset {
  uint16_t port;
  uint8_t proto;
  uint8_t role;
  uint16_t sigidx;
  uint8_t i_attempts;
  time_t first_seen;
  time_t last_seen;
  SwRef<ServiceAsset> next;
};

class ServiceAsset : public SWObject {
 private:
  s6_service_asset service_asset;

 public:
  uint32_t _get_size() { return 0; }

  char *_get_bytes() { return nullptr; }

  static ServiceAsset *_get_object(uint32_t obj_size, char *obj) {
    return nullptr;
  }

  uint16_t get_sigidx() { return service_asset.sigidx; }

  uint16_t get_port() { return service_asset.port; }

  SwRef<ServiceAsset> *get_next() { return &service_asset.next; }

  void set_next(SwRef<ServiceAsset> next) { service_asset.next = next; }

  void init_asset_service(uint16_t port, uint8_t proto, uint8_t role,
                          uint16_t sigidx, uint64_t cur_time) {
    service_asset.port = port;
    service_asset.proto = proto;
    service_asset.role = role;
    service_asset.sigidx = sigidx;
    service_asset.i_attempts = 0;
    service_asset.first_seen = cur_time;
    service_asset.last_seen = cur_time;
  }

  void update_asset_service(uint64_t cur_time) {
    service_asset.i_attempts++;
    service_asset.last_seen = cur_time;
  }
};

#endif
