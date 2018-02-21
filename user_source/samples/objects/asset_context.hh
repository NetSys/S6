#ifndef _ASSET_CONTEXT_HH_
#define _ASSET_CONTEXT_HH_

#include <stdint.h>
#include <string.h>

#include "dist.hh"
#include "object_base.hh"

// S6AssetTypeFlag
#define _AT_OS_FLAG 0x01
#define _AT_SERVICE_FLAG 0x02

// S6AssetOSID
#define OS_WINDOWS 0
#define OS_LINUX 1
#define OS_MAC 2
#define OS_ETC 3
#define OS_CNT 4

const std::string S6AssetOSName[OS_CNT] = {"OS_WINDOWS", "OS_LINUX", "OS_MAC",
                                           "OS_ETC"};

// S6AssetServiceID
#define SERVICE_FTP 0
#define SERVICE_NGINX 1
#define SERVICE_APACHE 2
#define SERVICE_SMTP 3
#define SERVICE_ETC 4
#define SERVICE_CNT 5

#define IP_PRINT(ip)                                                       \
  (ip >> 24 & 0xFF) << "." << (ip >> 16 & 0xFF) << "." << (ip >> 8 & 0xFF) \
                    << "." << (ip & 0xFF)

typedef uint16_t S6AssetTypeFlag;
typedef uint32_t S6AssetOSID;
typedef uint32_t S6AssetServiceID;

const std::string S6AssetServiceName[SERVICE_CNT] = {
    "SERVICE_FTP", "SERVICE_NGINX", "SERVICE_APACHE", "SERVICE_SMTP",
    "SERVICE_ETC"};

#if 0
inline S6AssetTypeFlag& operator|=(S6AssetTypeFlag &a, S6AssetTypeFlag b) {
	a = (S6AssetTypeFlag)((uint8_t)(a) | (uint8_t)(b));
	return a;
}

inline S6AssetOSID operator% (long int a, S6AssetOSID b) { 
	return (S6AssetOSID)(a % (uint16_t)b); 
}

inline S6AssetServiceID operator% (long int a, S6AssetServiceID b) { 
	return (S6AssetServiceID)(a % (uint16_t)b); 
}
#endif

struct S6Asset {
  S6AssetTypeFlag type;
  S6AssetOSID os_id;
  S6AssetServiceID service_id;
};

struct OSEntry {
  S6AssetOSID id;
  int count;
  struct timespec last_found_time;
};

struct ServiceEntry {
  S6AssetServiceID id;
  int count;
  struct timespec last_found_time;
};

class S6AssetContext : public MWObject {
 private:
  uint32_t src_ip;
  int os_count = 0;
  int service_count = 0;

  // TODO Change to linked_list
  OSEntry os_array[OS_CNT] = {};
  ServiceEntry service_array[SERVICE_CNT] = {};

  void update_os(S6Asset *asset) {
    OSEntry *os_entry = &os_array[asset->os_id];

    if (os_entry->count <= 0) {
      os_entry->id = asset->os_id;
      os_count++;
    }

    os_entry->count++;
    getCurTime(&os_entry->last_found_time);
  };

  void update_service(S6Asset *asset) {
    ServiceEntry *service_entry = &service_array[asset->service_id];

    if (service_entry->count <= 0) {
      service_entry->id = asset->service_id;
      service_count++;
    }

    service_entry->count++;
    getCurTime(&service_entry->last_found_time);
  };

 public:
  /* XXX
   * Need to be auto-generated */
  uint32_t _get_size() { return sizeof(int) + sizeof(int) + sizeof(uint32_t); }

  /* XXX
   * Need to be auto-generated */
  char *_get_bytes() {
    char *ret = (char *)malloc(_get_size());
    int offset = 0;
    memcpy(ret + offset, &src_ip, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    memcpy(ret + offset, &os_count, sizeof(int));
    offset += sizeof(int);
    memcpy(ret + offset, &service_count, sizeof(int));
    return ret;
  }

  static S6AssetContext *_get_object(uint32_t obj_size, char *obj) {
    S6AssetContext *ret = new S6AssetContext();

    int offset = 0;
    ret->src_ip = *(uint32_t *)(obj + offset);
    offset += sizeof(uint32_t);

    ret->os_count = *(int *)(obj + offset);
    offset += sizeof(int);

    ret->service_count = *(int *)(obj + offset);
    offset += sizeof(int);

    if (offset > (int)obj_size) {
      delete ret;
      return nullptr;
    }
    return ret;
  }

  void init(uint32_t src_ip) _behind { this->src_ip = src_ip; }

  uint32_t get_ip() const { return src_ip; }

  void update(S6Asset asset) _behind {
    if (asset.type & _AT_OS_FLAG)
      update_os(&asset);

    if (asset.type & _AT_SERVICE_FLAG)
      update_service(&asset);
  };

  void print() const {
    if (os_count + service_count <= 0)
      return;

    DEBUG_APP("\t================");
    DEBUG_APP("\t ASSET LIST FOR " << IP_PRINT(src_ip));

    // TO DO: string-like time print
    if (os_count > 0) {
      DEBUG_APP("\t\tOS_NAME\tOS_ID\tDETECT_COUNT\tLAST_FOUND_TIME");
      for (int i = 0; i < OS_CNT; i++) {
        const OSEntry *os = &os_array[i];
        if (os->count > 0)
          DEBUG_APP("\t\t" << S6AssetOSName[os->id] << "\t" << os->id << "\t"
                           << os->count << "\t"
                           << TIME_PRINT(os->last_found_time));
      }
    }

    DEBUG_APP("\t\t----------------");

    if (service_count > 0) {
      DEBUG_APP("\t\tSERVICE_NAME\tDETECT_COUNT\tLAST_FOUND_TIME");
      for (int i = 0; i < SERVICE_CNT; i++) {
        const ServiceEntry *service = &service_array[i];
        if (service->count > 0)
          DEBUG_APP("\t\t" << S6AssetServiceName[service->id] << "\t"
                           << service->id << "\t" << service->count << "\t"
                           << TIME_PRINT(service->last_found_time));
      }
    }

    DEBUG_APP("\t================");
  };
};

#endif
