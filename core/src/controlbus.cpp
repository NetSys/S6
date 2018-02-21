#include "controlbus.hh"

Connector::~Connector() {
  creator->unregister_address(my_address);
}

MessageBuffer *Connector::receive() {
  return this->creator->receive(this->my_address);
}

bool Connector::send(MessageBuffer *message, const WorkerAddress &to) {
  return this->creator->send(message, this->my_address, to);
}

struct CbusStats Connector::get_stats() {
  return this->creator->get_stats();
}

Connector *ControlBus::allocate_connector(const WorkerAddress &addr) {
  return new Connector(this, addr);
}
