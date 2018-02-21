#ifndef _DISTREF_TCP_CONTROL_BUS_HH_
#define _DISTREF_TCP_CONTROL_BUS_HH_

#include <map>
#include <queue>
#include <sys/epoll.h>
#include <unordered_map>

#include "controlbus.hh"
#include "worker_address.hh"
#include "worker_config.hh"

/*
 * TCP is connection-oriented, which doesn't fit well the current abstraction...
 *
 * NOTE:
 * - This class is not thread safe yet.
 * - There are tons of assert()s. Should be good enough for testing though.
 * - send() may block, especially when connect() needs to be done.
 * - address <-> fd translation is not really necessary. To eliminate this,
 *		1. Connector needs to embed fd directly, and
 *		2. We should use epoll (which supports cookie), not poll.
 */

#define MAX_EVENTS 20
#define MAX_BUFF_SIZE (1024 * 1024)
#define MAX_CONN MAX_WORKER_CNT

struct binary_buff {
  u_char buf[MAX_BUFF_SIZE];
  int offset = 0;
  int size = 0;
};

struct message_buffer_info {
  MessageBuffer* msgbuf;
  int offset;
  int size;

  message_buffer_info(MessageBuffer* _msgbuf, int _offset, int _size)
      : msgbuf(_msgbuf), offset(_offset), size(_size) {}
};

struct fd_info {
  int fd = -1;
  WorkerAddress addr;
  std::queue<message_buffer_info> send_queue;
  struct binary_buff recv_buff;

  fd_info(int new_fd, WorkerAddress new_addr) : fd(new_fd), addr(new_addr) {}
};

class TCPControlBus : public ControlBus {
 private:
  std::unordered_map<WorkerAddress, struct fd_info*> addr_to_fdinfo;
  std::unordered_map<int, struct fd_info*> fd_to_fdinfo;

  int listen_fd = -1;
  int epollfd;

  int g_recv_fd[MAX_CONN];
  int rfd_count = 0;
  int rfd_idx = 0;

 public:
  TCPControlBus();
  virtual ~TCPControlBus();

 public:
  virtual Connector* register_address(const WorkerAddress& addr);
  virtual MessageBuffer* allocate_message(const std::size_t messageSize) const;
  virtual MessageBuffer* init_message(uint8_t* buf, std::size_t buf_size) const;

 private:
  virtual bool send(MessageBuffer* pkt, const WorkerAddress& from,
                    const WorkerAddress& to);
  virtual MessageBuffer* receive(const WorkerAddress& me);

  virtual void unregister_address(const WorkerAddress& worker);

  bool flush_send_queue(struct fd_info*);
  bool receive_from_all_fds();
  void close_connection(const WorkerAddress& worker);
  void close_connection(int fd);
};

#endif /* _DISTREF_TCP_CONTROL_BUS_HH_ */
