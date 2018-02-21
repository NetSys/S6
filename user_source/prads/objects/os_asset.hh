#ifndef _OS_ASSET_OB_HH_
#define _OS_ASSET_OB_HH_

#include <string.h>
#include <time.h>

#include "dist.hh"
#include "object_base.hh"

template <class X>
class SwRef;
class OSAsset;

struct s6_os_asset {
  uint8_t detection;
  uint16_t sigidx;
  uint8_t i_attempts;
  time_t first_seen;
  time_t last_seen;
  SwRef<OSAsset> next;
};

class OSAsset : public SWObject {
 private:
  struct s6_os_asset os_asset;

 public:
  uint32_t _get_size() { return 0; }

  char *_get_bytes() { return nullptr; }

  static OSAsset *_get_object(uint32_t obj_size, char *obj) { return nullptr; }

  uint8_t get_detection() { return os_asset.detection; }

  uint16_t get_sigidx() { return os_asset.sigidx; }

  SwRef<OSAsset> *get_next() { return &os_asset.next; }

  void set_next(SwRef<OSAsset> next) { os_asset.next = next; }

  void init_asset_os(uint8_t detection, uint16_t sigidx, uint64_t cur_time) {
    os_asset.detection = detection;
    os_asset.sigidx = sigidx;
    os_asset.i_attempts = 0;
    os_asset.first_seen = cur_time;
    os_asset.last_seen = cur_time;
  }

  void update_asset_os(uint64_t cur_time) {
    os_asset.i_attempts++;
    os_asset.last_seen = cur_time;
  }
};

#endif
