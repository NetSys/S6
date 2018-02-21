#ifndef _DISTREF_CONTROL_BUS_HH_
#define _DISTREF_CONTROL_BUS_HH_

#include "type.hh"
#include "worker_address.hh"

class ControlBus;

struct MessageBuffer {
  std::size_t body_offset;
  std::size_t body_size;
  uint8_t buf[0];

  const void *get_message_body() const { return buf + body_offset; };
  const void *get_message_header() const { return buf; };
};

struct CbusStats {
  uint64_t send_bytes = 0;
  uint64_t recv_bytes = 0;
};

// socket-like class
class Connector {
 private:
  ControlBus *creator;
  WorkerAddress my_address;

  Connector(ControlBus *creator, const WorkerAddress &addr)
      : creator(creator), my_address(addr) {}

 public:
  virtual ~Connector();

  virtual MessageBuffer *receive() final;
  // NOTE: send() will take the ownership of the packet
  virtual bool send(MessageBuffer *message, const WorkerAddress &to) final;
  virtual struct CbusStats get_stats() final;

  friend class ControlBus;
};

class ControlBus {
 protected:
  struct CbusStats stats;

  ControlBus(){};
  virtual ~ControlBus(){};

  // util function
  virtual Connector *allocate_connector(const WorkerAddress &addr) final;

 public:
  virtual Connector *register_address(const WorkerAddress &addr) = 0;
  virtual MessageBuffer *allocate_message(
      const std::size_t messageSize) const = 0;
  virtual MessageBuffer *init_message(uint8_t *buf,
                                      std::size_t buf_size) const = 0;
  struct CbusStats get_stats() {
    return stats;
  }

 private:
  virtual bool send(MessageBuffer *message, const WorkerAddress &from,
                    const WorkerAddress &to) = 0;
  virtual MessageBuffer *receive(const WorkerAddress &me) = 0;

  virtual void unregister_address(const WorkerAddress &worker) = 0;

  friend Connector::~Connector();
  friend bool Connector::send(MessageBuffer *message, const WorkerAddress &to);
  friend MessageBuffer *Connector::receive();
};

#endif /* _DISTREF_CONTROL_BUS_HH_ */
