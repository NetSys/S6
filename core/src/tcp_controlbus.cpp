#include <iostream>

#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "log.hh"
#include "tcp_controlbus.hh"

typedef struct {
  std::size_t total_size;  // required to use stream as datagram
} VirtualHeader __attribute__((packed));

TCPControlBus::TCPControlBus() {}

TCPControlBus::~TCPControlBus() {
  addr_to_fdinfo.clear();

  for (auto it = fd_to_fdinfo.begin(); it != fd_to_fdinfo.end();) {
    delete it->second;
    it = fd_to_fdinfo.erase(it);
  }
  fd_to_fdinfo.clear();
}

// note: identical to the one in the SharedMemControlBus (except size)
MessageBuffer *TCPControlBus::allocate_message(
    const std::size_t messageSize) const {
  std::size_t packet_size = 0;

  std::size_t header_size = sizeof(VirtualHeader);
  packet_size += sizeof(MessageBuffer);
  packet_size += header_size;
  packet_size += messageSize;

  auto ret = reinterpret_cast<MessageBuffer *>(std::malloc(packet_size));
  if (ret != nullptr) {
    ret->body_offset = header_size;
    ret->body_size = messageSize;
  }
  return ret;
}

MessageBuffer *TCPControlBus::init_message(uint8_t *buf,
                                           std::size_t buf_size) const {
  MessageBuffer *mb = (MessageBuffer *)buf;
  mb->body_offset = sizeof(VirtualHeader);
  mb->body_size = buf_size - sizeof(MessageBuffer) - mb->body_offset;

  return mb;
}

Connector *TCPControlBus::register_address(const WorkerAddress &addr) {
  sockaddr_in s_addr;
  int so_reuseaddr = 1;
  linger so_linger;

  int ret;

  if (listen_fd > 0) {
    DEBUG_DEV("tcp control bus is already registered");
    return nullptr;
  }

  DEBUG_DEV("registering " + addr.str());

  // the address is already being used? (by me or other workers)
  assert(addr_to_fdinfo.find(addr) == addr_to_fdinfo.end());

  listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  assert(listen_fd >= 0);

  ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr,
                   sizeof(so_reuseaddr));
  assert(ret != -1);

  ret = setsockopt(listen_fd, SOL_SOCKET, SO_LINGER, &so_linger,
                   sizeof(so_linger));
  assert(ret != -1);

  memset(&s_addr, 0, sizeof(s_addr));
  s_addr.sin_family = AF_INET;
  s_addr.sin_addr.s_addr = addr.get_ip_addr();
  s_addr.sin_port = addr.get_port();
  void *s_addr_p = &s_addr;
  ret = bind(listen_fd, reinterpret_cast<sockaddr *>(s_addr_p), sizeof(s_addr));
  if (ret < 0)
    DEBUG_ERR("Fail to bind to " << addr << " : " << strerror(errno));

  ret = listen(listen_fd, 16);
  assert(ret != -1);

  epollfd = epoll_create(MAX_EVENTS);
  if (epollfd == -1) {
    DEBUG_ERR("Fail to create epoll");
    return nullptr;
  }
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = listen_fd;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
    DEBUG_ERR("Fail to register epoll event on listen_fd");
    return nullptr;
  }

  struct fd_info *fi = new fd_info(listen_fd, addr);
  addr_to_fdinfo[addr] = fi;
  fd_to_fdinfo[listen_fd] = fi;

  return allocate_connector(addr);
}

bool TCPControlBus::send(MessageBuffer *msg, const WorkerAddress &from,
                         const WorkerAddress &to) {
  int fd;
  int ret;

  if (!msg)
    return false;

  // sanity check for "from"
  assert(addr_to_fdinfo.find(from) != addr_to_fdinfo.end());

  assert(listen_fd > 0);

  auto iter = addr_to_fdinfo.find(to);
  struct fd_info *fi = nullptr;

  if (iter == addr_to_fdinfo.end()) {
    sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = to.get_port(),
        .sin_addr = {.s_addr = to.get_ip_addr()},
    };

    int ret;
    const int one = 1;

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(fd >= 0);

    void *addr_p = &addr;
    ret = connect(fd, reinterpret_cast<sockaddr *>(addr_p), sizeof(addr));
    if (ret == -1) {
      perror("connect");
      DEBUG_ERR("cannot connect to " + to.str());
      return false;
    }

    ret = fcntl(fd, F_SETFL, O_NONBLOCK);
    assert(ret != -1);

    /* because this is just optimization, we can ignore errors */
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(int));

    fi = new fd_info(fd, to);
    addr_to_fdinfo[to] = fi;
    fd_to_fdinfo[fd] = fi;

    sockaddr_in my_addr;
    socklen_t len = sizeof(my_addr);
    void *my_addr_p = &my_addr;
    if (getsockname(fd, reinterpret_cast<sockaddr *>(my_addr_p), &len)) {
      perror("getsockname");
      return false;
    }

    DEBUG_DEV("connected " + to.str() + " fd = " + std::to_string(fd) +
                  " using port number "
              << ntohs(my_addr.sin_port));

  } else {
    fi = iter->second;
    fd = fi->fd;
    if (fd == listen_fd) {
      DEBUG_ERR("loopback destination address " + to.str());
      print_stack();
      assert(0);
      return false;
    }
  }

  VirtualHeader *header = reinterpret_cast<VirtualHeader *>(
      const_cast<void *>(msg->get_message_header()));
  header->total_size = msg->body_offset + msg->body_size;

  std::queue<message_buffer_info> &send_queue = fi->send_queue;
  if (send_queue.size() >= 256) {
    flush_send_queue(fi);
    return false;
  }
  send_queue.push(message_buffer_info(msg, 0, header->total_size));

  flush_send_queue(fi);
  return true;
}

bool TCPControlBus::flush_send_queue(struct fd_info *fi) {
  std::queue<message_buffer_info> &send_queue = fi->send_queue;
  while (!send_queue.empty()) {
    message_buffer_info &info = send_queue.front();
    int ret = ::send(fi->fd, info.msgbuf->buf + info.offset,
                     info.size - info.offset, MSG_NOSIGNAL);
    if (ret < 0) {
      if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
        // connection closed
        DEBUG_ERR(strerror(errno) << " Close connection " << fi->addr);
        close_connection(fi->fd);
        exit(EXIT_FAILURE);
      }
      break;
    }

    info.offset += ret;
    stats.send_bytes += ret;

    if (info.offset != info.size)
      break;

    send_queue.pop();
  }

  return true;
}

bool TCPControlBus::receive_from_all_fds() {
  sockaddr_in sock_addr;
  socklen_t addrlen = sizeof(sockaddr_in);
  const int &one = 1;

  bool new_recv_data = false;

  struct epoll_event events[MAX_EVENTS];
  int n_events = epoll_wait(epollfd, events, MAX_EVENTS, 0);
  if (n_events <= 0)
    return new_recv_data;

  for (int i = 0; i < n_events; ++i) {
    if (events[i].data.fd == listen_fd) {
      void *sock_addr_p = &sock_addr;
      int conn_fd = accept(listen_fd, reinterpret_cast<sockaddr *>(sock_addr_p),
                           &addrlen);
      if (conn_fd == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          DEBUG_ERR("accept() errno = " + std::to_string(errno));
          break;
        }
      }

      int ret = fcntl(conn_fd, F_SETFL, O_NONBLOCK);
      assert(ret != -1);

      /* because this is just optimization, we can ignore errors */
      setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(int));

      struct epoll_event ev;
      ev.events = EPOLLIN;
      ev.data.fd = conn_fd;
      if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_fd, &ev) == -1) {
        DEBUG_ERR("epoll_ctl errno = " + std::to_string(errno));
        break;
      }

      WorkerAddress addr(sock_addr.sin_addr.s_addr, ntohs(sock_addr.sin_port));
      struct fd_info *fi = new fd_info(conn_fd, addr);
      addr_to_fdinfo[addr] = fi;
      fd_to_fdinfo[conn_fd] = fi;

      sockaddr_in my_addr;
      socklen_t len = sizeof(my_addr);
      void *my_addr_p = &my_addr;
      if (getsockname(listen_fd, reinterpret_cast<sockaddr *>(my_addr_p),
                      &len)) {
        perror("getsockname");
        return false;
      }
      DEBUG_DEV("accepted " + addr.str() + " fd = " + std::to_string(conn_fd) +
                    " using port number "
                << ntohs(my_addr.sin_port));

    } else if (events[i].events & EPOLLERR) {
      close_connection(events[i].data.fd);

      DEBUG_ERR("epollerr errno = " + std::to_string(errno));
      continue;

    } else if (events[i].events & EPOLLIN) {
      int fd = events[i].data.fd;

      auto iter = fd_to_fdinfo.find(fd);
      assert(iter != fd_to_fdinfo.end());

      struct binary_buff *b = &iter->second->recv_buff;

      ssize_t received = recv(fd, b->buf + b->offset + b->size,
                              MAX_BUFF_SIZE - b->offset - b->size,
                              MSG_NOSIGNAL | MSG_DONTWAIT);
      if (received <= 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
          continue;

        break;
      }

      new_recv_data = true;
      b->size += received;
      stats.recv_bytes += received;

      // mark fd with new read data
      g_recv_fd[rfd_count++] = fd;
    } else {
      assert(0);
    }
  }

  return new_recv_data;
}

MessageBuffer *TCPControlBus::receive(const WorkerAddress &me) {
  static int rfd_remain = 0;

  if (rfd_remain <= 0) {
    // no more bytes to process, then accepts, and receives new bytes
    rfd_idx = 0;
    rfd_count = 0;
    rfd_remain = 0;

    bool to_read = receive_from_all_fds();
    if (!to_read)
      return nullptr;
    rfd_remain = rfd_count;
    // DEBUG_ERR("receive new bytes from " << rfd_count << " fds." );
  }

  for (int i = rfd_idx; i < rfd_count; i = (i + 1) % rfd_count) {
    if (g_recv_fd[i] > 0) {
      int cur_fd = g_recv_fd[i];
      struct binary_buff *recv_buff = &fd_to_fdinfo[cur_fd]->recv_buff;

      int size = *(int *)(recv_buff->buf + recv_buff->offset);
      // DEBUG_ERR("index " << i << " has " << recv_buff->size
      //		<< " bytes and will read " << size << " bytes" );
      if (recv_buff->size >= size) {
        MessageBuffer *msg = allocate_message(size - sizeof(VirtualHeader));
        memcpy(msg->buf, recv_buff->buf + recv_buff->offset, size);
        recv_buff->offset += size;
        recv_buff->size -= size;

        if (recv_buff->size == 0) {
          recv_buff->offset = 0;
          g_recv_fd[i] = 0;
          rfd_remain--;
        }

        rfd_idx = (i + 1) % rfd_count;
        return msg;
      } else {
        // DEBUG_ERR("msg size is " << size << " but in buffer, offset is "
        //		<< recv_buff->offset << " to read is " << recv_buff->size
        //		<< " buff_size is " << MAX_BUFF_SIZE);
        // wait more message body and go to next fd to read
        if (size >= MAX_BUFF_SIZE - 2)
          perror("Error message is bigger than maximum buffer size");
        // else if (size >= MAX_BUFF_SIZE - 2 - recv_buff->offset) {
        // DEBUG_ERR("Move buffer offset into zero");
        memmove(recv_buff->buf, recv_buff->buf + recv_buff->offset,
                recv_buff->size);
        recv_buff->offset = 0;
        //}

        g_recv_fd[i] = 0;
        rfd_remain--;
        continue;
      }
    }  // end of if

    if (rfd_remain <= 0)
      break;
  }  // end of for

  return nullptr;
}

void TCPControlBus::close_connection(const WorkerAddress &addr) {
  DEBUG_DEV("close_connection " + addr.str());

  auto iter = addr_to_fdinfo.find(addr);
  assert(iter != addr_to_fdinfo.end());

  struct fd_info *fi = iter->second;
  close(fi->fd);

  addr_to_fdinfo.erase(iter);
  fd_to_fdinfo.erase(fi->fd);
  delete fi;
}

void TCPControlBus::close_connection(int fd) {
  auto iter = fd_to_fdinfo.find(fd);
  assert(iter != fd_to_fdinfo.end());

  close_connection(iter->second->addr);
}

void TCPControlBus::unregister_address(const WorkerAddress &addr) {
  return;
}
