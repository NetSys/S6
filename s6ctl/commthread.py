from __future__ import print_function

import errno
import json
import select
import socket
import sys
import traceback
import time
import threading

from nfinstance import *
from s6ctl_config import *

MAX_LISTENING_QSIZE = 16


class CommThread(threading.Thread):

    def __init__(self, host, port, nf_instances):
        threading.Thread.__init__(self)

        self.running = True
        self.server_address = (host, port)
        self.epoll = select.epoll()
        self.nf_instances = nf_instances

        self.lock = threading.Lock()
        self.fd_to_socket = {}
        self.fd_to_addr = {}
        self.cid_to_fd = {}
        self.received = {}
        self.tosend = {}  # self.lock required

        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setblocking(0)

        while(True):
            try:
                self.listener.bind(self.server_address)
            except:
                (host, port) = self.server_address
                self.server_address = (host, port + 1)
                continue
            break

        self.listener.listen(MAX_LISTENING_QSIZE)

        print('Controller listening on %s:%d ' %
              (self.server_address[0], self.server_address[1]))

    def get_address(self):
        return self.server_address

        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setblocking(0)

        while (True):
            try:
                self.listener.bind(self.server_address)
            except socket.error, e:
                if e[0] == errno.EADDRINUSE:
                    (host, post) = self.server_address
                    self.server_address = (host, port + 1)
                    continue
                else:
                    raise
            break

        self.listener.listen(MAX_LISTENING_QSIZE)

        print('Controller listening on %s:%d ' %
              (self.server_address[0], self.server_address[1]))

    def get_address(self):
        return self.server_address

    def send(self, cid, msg):  # called by other threads
        msg_wrap = "%04d%s\x00" % \
            (len(msg) + 5, msg)

        self.lock.acquire()
        fd = self.cid_to_fd[cid]
        self.tosend[fd] += msg_wrap
        self.lock.release()

        self.epoll.modify(fd, select.EPOLLIN | select.EPOLLOUT)

    def stop(self):
        self.running = False

    def _process_hello(self, jmsg, fd):
        wid = jmsg['worker_id']
        self.cid_to_fd[wid] = fd
        self.nf_instances[wid].update(NFInstance.ST_INIT, NFInstance.ST_HELLO)

    def _process_ready(self, jmsg):
        wid = jmsg['worker_id']
        self.nf_instances[wid].update(NFInstance.ST_HELLO,
                                      NFInstance.ST_READY)

    def _process_run(self, jmsg):
        wid = jmsg['worker_id']
        self.nf_instances[wid].update(NFInstance.ST_READY,
                                      NFInstance.ST_NORMAL)

    def _process_prepared_scaling(self, jmsg):
        wid = jmsg['worker_id']
        self.nf_instances[wid].update(NFInstance.ST_NORMAL,
                                      NFInstance.ST_PREPARE_SCALING)

    def _process_started_scaling(self, jmsg):
        wid = jmsg['worker_id']
        self.nf_instances[wid].update(NFInstance.ST_PREPARE_SCALING,
                                      NFInstance.ST_SCALING)

    def _process_completed_scaling(self, jmsg):
        wid = jmsg['worker_id']
        self.nf_instances[wid].update(NFInstance.ST_SCALING,
                                      NFInstance.ST_COMPLETED_SCALING)

    def _process_prepared_normal(self, jmsg):
        wid = jmsg['worker_id']
        self.nf_instances[wid].update(NFInstance.ST_COMPLETED_SCALING,
                                      NFInstance.ST_PREPARE_NORMAL)

    def _process_being_normal(self, jmsg):
        wid = jmsg['worker_id']
        self.nf_instances[wid].update(NFInstance.ST_PREPARE_NORMAL,
                                      NFInstance.ST_NORMAL)

    def _process_teared_down(self, jmsg):
        wid = jmsg['worker_id']
        self.nf_instances[wid].notify_teared_down(NFInstance.ST_NORMAL,
                                                  NFInstance.ST_TEARDOWN)

    def _process_message(self, jmsg, fd):

        try:
            msg_type = jmsg['msg_type']

            if msg_type == 'hello':
                self._process_hello(jmsg, fd)

            elif msg_type == 'ready':
                self._process_ready(jmsg)

            elif msg_type == 'run':
                self._process_run(jmsg)

            elif msg_type == 'prepared_scaling':
                self._process_prepared_scaling(jmsg)

            elif msg_type == 'started_scaling':
                self._process_started_scaling(jmsg)

            elif msg_type == 'completed_scaling':
                self._process_completed_scaling(jmsg)

            elif msg_type == 'prepared_normal':
                self._process_prepared_normal(jmsg)

            elif msg_type == 'being_normal':
                self._process_being_normal(jmsg)

            elif msg_type == 'teared_down':
                self._process_teared_down(jmsg)

            else:
                print('msg_type "%s" is not specified' %
                      msg_type, file=sys.stderr)

        except KeyError as e:
            print(e, file=sys.stderr)

    def _unregister(self, fd):
        self.epoll.unregister(fd)
        self.fd_to_socket[fd].close()

        if VERBOSE:
            (ip, port) = self.fd_to_addr[fd]
            print('[%s:%d(%d)] Unregister Socket' % (ip, port, fd))

        del self.fd_to_socket[fd]
        del self.fd_to_addr[fd]

    def _poll(self):

        events = self.epoll.poll(1)

        for fd, event in events:
            if fd == self.listener.fileno():
                conn, (ip, port) = self.listener.accept()
                conn.setblocking(0)

                self.epoll.register(conn.fileno(), select.EPOLLIN)
                self.fd_to_socket[conn.fileno()] = conn
                self.fd_to_addr[conn.fileno()] = (ip, port)
                self.received[conn.fileno()] = ''
                self.tosend[conn.fileno()] = ''
                if VERBOSE:
                    print('[%s:%d(%d)] Connect Socket' % (ip, port, fd))

            elif event & select.EPOLLIN:
                data = self.fd_to_socket[fd].recv(1024)
                if data:
                    self.received[fd] += data
                    msg_len = int(data[0:4], 10)

                    if VERBOSE:
                        (ip, port) = self.fd_to_addr[fd]
                        print('[%s:%d(%d)] Recv: %s' % (ip, port, fd, data))

                    if msg_len <= len(self.received[fd]):
                        jmsg = json.loads(
                            self.received[fd][4:msg_len - 1])  # Remove \00
                        self._process_message(jmsg, fd)
                        self.received[fd] = self.received[fd][msg_len:]
                else:
                    self._unregister(fd)

            elif event & select.EPOLLOUT:
                self.lock.acquire()
                data = self.tosend[fd]
                if not data:
                    (ip, port) = self.fd_to_addr[fd]
                    print('[%s:%d(%d)] No data to send' %
                          (ip, port, fd), file=sys.stderr)
                    continue

                sent = self.fd_to_socket[fd].send(data)
                self.tosend[fd] = self.tosend[fd][sent:]
                self.lock.release()

                if VERBOSE:
                    (ip, port) = self.fd_to_addr[fd]
                    print('[%s:%d(%d)] Send: %s' %
                          (ip, port, fd, data[0:sent]))

                if len(data) == sent:
                    self.epoll.modify(fd, select.EPOLLIN)

            elif event & select.EPOLLHUP:
                self._unregister(fd)

    def run(self):
        self.epoll.register(self.listener.fileno(), select.EPOLLIN)

        try:
            while self.running:
                self._poll()
        finally:
            self.epoll.unregister(self.listener.fileno())
            self.epoll.close()
            self.listener.close()
