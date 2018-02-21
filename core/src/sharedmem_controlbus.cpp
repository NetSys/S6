#include "sharedmem_controlbus.hh"

#include <cstdlib>
#include <exception>
#include <iostream>

const std::size_t max_queue_size = 1024;

typedef struct {
  WorkerAddress src;
  WorkerAddress dst;
} SharedMemControlHeader;

MessageBuffer *SharedMemControlBus::allocate_message(
    const std::size_t messageSize) const {
  std::size_t packet_size = 0;

  std::size_t header_size = sizeof(SharedMemControlHeader);
  packet_size += sizeof(MessageBuffer);
  packet_size += header_size;
  packet_size += messageSize;

  auto ret = reinterpret_cast<MessageBuffer *>(std::malloc(packet_size));
  if (ret != nullptr) {
    ret->body_size = messageSize;
    ret->body_offset = header_size;
  }
  return ret;
}

MessageBuffer *SharedMemControlBus::init_message(uint8_t *buf,
                                                 std::size_t buf_size) const {
  MessageBuffer *mb = (MessageBuffer *)buf;
  mb->body_offset = sizeof(SharedMemControlHeader);
  mb->body_size = buf_size - sizeof(MessageBuffer) - mb->body_offset;

  return mb;
}

Connector *SharedMemControlBus::register_address(const WorkerAddress &addr) {
  std::lock_guard<std::mutex> g_lck(this->mutex);

  if (this->workers.find(addr) != this->workers.end())
    return nullptr;

  Connector *sock = this->allocate_connector(addr);
  this->workers[addr] =
      std::make_tuple(sock, new std::queue<MessageBuffer *>, new std::mutex);

  return sock;
}

bool SharedMemControlBus::send(MessageBuffer *message,
                               const WorkerAddress &from,
                               const WorkerAddress &to) {
  if (!message)
    return false;

  std::unique_lock<std::mutex> g_lck(this->mutex);
  auto iter = this->workers.find(to);
  if (iter == this->workers.end())
    return false;
  g_lck.unlock();

  SharedMemControlHeader *header =
      (SharedMemControlHeader *)message->get_message_header();
  header->src = from;
  header->dst = to;

  auto &tuple = iter->second;
  auto &lock_ptr = std::get<2>(tuple);

  std::lock_guard<std::mutex> lck(*lock_ptr);

  if (std::get<1>(tuple)->size() >= max_queue_size)
    return false;

  std::get<1>(tuple)->push(message);
  stats.send_bytes = std::get<1>(tuple)->size();

  return true;
}

MessageBuffer *SharedMemControlBus::receive(const WorkerAddress &me) {
  std::unique_lock<std::mutex> g_lck(this->mutex);
  auto iter = this->workers.find(me);
  if (iter == this->workers.end())
    return nullptr;
  g_lck.unlock();

  try {
    auto &tuple = iter->second;
    auto &lock_ptr = std::get<2>(tuple);
    auto &queue_ptr = std::get<1>(tuple);

    std::lock_guard<std::mutex> lck(*lock_ptr);

    if (queue_ptr->empty())
      return nullptr;

    MessageBuffer *ret = queue_ptr->front();
    queue_ptr->pop();
    stats.recv_bytes = std::get<1>(tuple)->size();

    return ret;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return nullptr;
  }
}

// XXX Problem:
// Workers should not be unregistered while it is send or receive messages
void SharedMemControlBus::unregister_address(const WorkerAddress &addr) {
  std::lock_guard<std::mutex> g_lck(this->mutex);

  auto iter = this->workers.find(addr);
  if (iter == this->workers.end())
    return;

  auto &tuple = iter->second;  // Connector will be freed externally
  auto &queue_ptr = std::get<1>(tuple);
  auto &lock_ptr = std::get<2>(tuple);

  delete lock_ptr;
  delete queue_ptr;

  this->workers.erase(addr);
}
