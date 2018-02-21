#include <sstream>

#include <arpa/inet.h>

#include "worker_address.hh"

WorkerAddress* WorkerAddress::CreateWorkerAddress(std::string val) {
  std::size_t p_split = val.find(":");
  if (p_split == std::string::npos)
    return nullptr;

  std::string ip_str = val.substr(0, p_split);
  std::string port_str = val.substr(p_split + 1, val.length());

  uint32_t ip = inet_addr(ip_str.c_str());
  if (ip == INADDR_NONE)
    return nullptr;

  uint16_t port;
  try {
    port = std::stoi(port_str, nullptr);
  } catch (const std::invalid_argument& ia) {
    return nullptr;
  }

  return new WorkerAddress(ip, port);
}

WorkerAddress::WorkerAddress(uint32_t ip, uint16_t port) {
  this->addr = std::make_pair(ip, htons(port));
}

WorkerAddress::WorkerAddress(addr_t addr) {
  this->addr = addr;
}

WorkerAddress::WorkerAddress(const std::string& ip, uint16_t port) {
  this->addr = std::make_pair(inet_addr(ip.c_str()), htons(port));
}

WorkerAddress::WorkerAddress(const WorkerAddress& a) {
  this->addr = a.addr;
}

WorkerAddress::~WorkerAddress() {}

bool WorkerAddress::operator==(const WorkerAddress& other) const {
  return (this->addr == other.addr);  // XXX change if needed.
}

// Pretty sure this is not C++ style...
std::string WorkerAddress::str() const {
  std::ostringstream out;

  out << *this;

  return out.str();
}

std::ostream& operator<<(std::ostream& out, const WorkerAddress& a) {
  in_addr addr = {.s_addr = a.get_ip_addr()};

  out << inet_ntoa(addr) << ":" << ntohs(a.get_port());
  return out;
}

WorkerAddress* parse_worker_address(std::string val) {
  std::size_t p_split = val.find(":");
  if (p_split == std::string::npos)
    return nullptr;

  std::string ip_str = val.substr(0, p_split);
  std::string port_str = val.substr(p_split + 1, val.length());

  if (ip_str.compare("localhost") == 0)
    ip_str = "127.0.0.1";

  uint32_t ip = inet_addr(ip_str.c_str());
  if (ip == INADDR_NONE)
    return nullptr;

  uint16_t port;
  try {
    port = std::stoi(port_str, nullptr);
  } catch (const std::invalid_argument& ia) {
    return nullptr;
  }

  return new WorkerAddress(ip, port);
}
