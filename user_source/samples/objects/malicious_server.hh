#ifndef _MALICIOUS_SERVER_OB_H
#define _MALICIOUS_SERVER_OB_H

#include <string.h>

#include "dist.hh"
#include "object_base.hh"

#include "stub.tcp_flow.hh"
#include "tcp_flow.hh"

#define IP_PRINT(ip)                                                       \
  (ip >> 24 & 0xFF) << "." << (ip >> 16 & 0xFF) << "." << (ip >> 8 & 0xFF) \
                    << "." << (ip & 0xFF)

class MaliciousServer : public MWObject {
#define MAX_CFLOW_CNT 10
 private:
 public:
  uint32_t ip = 0;
  int cflow_cnt = 0;
  uint32_t attack_count = 0;
  const SwRef<TCPFlow> cflow_arr[MAX_CFLOW_CNT] = {};

  /* XXX
   * Need to be auto-generated */
  uint32_t _get_size() {
    uint32_t ret = sizeof(uint32_t) + sizeof(int);
    for (int i = 0; i < MAX_CFLOW_CNT; i++) {
      ret += cflow_arr[i].get_serial_size();
      ;
    }

    return ret;
  }

  /* XXX
   * Need to be auto-generated */
  char *_get_bytes() {
    char *ret = (char *)malloc(_get_size());
    int offset = 0;
    memcpy(ret + offset, &ip, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    memcpy(ret + offset, &cflow_cnt, sizeof(int));
    offset += sizeof(int);

    for (int i = 0; i < MAX_CFLOW_CNT; i++) {
      memcpy(ret + offset, &cflow_cnt, sizeof(int));
      offset += sizeof(int);

      memcpy(ret + offset, cflow_arr[i].serialize(),
             cflow_arr[i].get_serial_size());
      offset += cflow_arr[i].get_serial_size();
    }
    return ret;
  }

  /* XXX
   * Need to be auto-generated */
  static MaliciousServer *_get_object(uint32_t obj_size, char *obj) {
    MaliciousServer *ret = new MaliciousServer();

    int offset = 0;
    ret->ip = *(uint32_t *)(obj + offset);
    offset += sizeof(uint32_t);

    ret->cflow_cnt = *(int *)(obj + offset);
    offset += sizeof(int);

    for (int i = 0; i < MAX_CFLOW_CNT; i++) {
      int serial_size = *(int *)(obj + offset);
      offset += sizeof(uint32_t);

      ret->cflow_arr[i] =
          *SwRef<TCPFlow>::deserialize((struct SwStubSerial *)(obj + offset));
      offset += serial_size;
    }

    if (offset > (int)obj_size) {
      delete ret;
      return nullptr;
    }
    return ret;
  }

  void detect_malicious_pattern(int attack_pattern, uint32_t src_addr,
                                uint32_t dst_addr, uint16_t src_port,
                                uint16_t dst_port) _behind {
    attack_count++;
  }

  void _init() {
    attack_count = 0;
    cflow_cnt = 0;
  }

  void _add(MaliciousServer other) {
    attack_count += other.attack_count;
    cflow_cnt += other.cflow_cnt;
  }

  void init(uint32_t ip) _behind { this->ip = ip; }

  bool is_cflow_exist(const SwRef<TCPFlow> flow) {
    for (int i = 0; i < cflow_cnt; i++) {
      if (cflow_arr[i] == flow)
        return true;
    }
    return false;
  }

  bool add_cflow(const SwRef<TCPFlow> flow) _behind {
    if (cflow_cnt == MAX_CFLOW_CNT - 1)
      return false;

    if (is_cflow_exist(flow)) {
      return true;
    }

    cflow_arr[cflow_cnt++] = flow;
    return true;
  }

  void print_cflows() const {
    DEBUG_APP("Hosts communicates with malicious server " << IP_PRINT(ip));

    if (cflow_cnt <= 0) {
      DEBUG_APP("\tNone");
      return;
    }

    //			for (int i = 0; i < cflow_cnt; i++) {
    //				uint32_t cli_ip = cflow_arr[i]->get_cli_ip();
    //				DEBUG_APP("\t" << i << " th Host: " << IP_PRINT(cli_ip));
    //			}
  }

#undef MAX_CFLOW_CNT
};

#endif
