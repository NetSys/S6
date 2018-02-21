#ifndef _DISTREF_WORKER_ADDRESS_HH_
#define _DISTREF_WORKER_ADDRESS_HH_

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>

#include "type.hh"

// IP address and port are stored in network order
typedef std::pair<uint32_t, uint16_t> addr_t;

class WorkerAddress {
 private:
  addr_t addr;

 public:
  static WorkerAddress* CreateWorkerAddress(std::string val);
  WorkerAddress(uint32_t ip = 0, uint16_t port = 0);
  WorkerAddress(addr_t addr);
  WorkerAddress(const std::string& ip, uint16_t port);
  WorkerAddress(const WorkerAddress& a);
  ~WorkerAddress();

  bool operator==(const WorkerAddress& other) const;

  uint32_t get_ip_addr() const { return addr.first; }
  uint16_t get_port() const { return addr.second; }

  std::string str() const;
};

std::ostream& operator<<(std::ostream& out, const WorkerAddress& a);

namespace std {

template <>
struct hash<WorkerAddress> {
  std::size_t operator()(const WorkerAddress& k) const {
    return std::hash<uint32_t>()(k.get_ip_addr()) ^
           std::hash<uint16_t>()(k.get_port());
  }
};
}

WorkerAddress* parse_worker_address(std::string val);

#endif /* _DISTREF_WORKER_ADDRESS_HH_ */
