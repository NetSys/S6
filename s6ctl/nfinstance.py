from __future__ import print_function

import subprocess
import sys
import threading

from s6ctl_config import *


class NFInstance(object):
    ST_INIT = 0
    ST_HELLO = 1
    ST_READY = 2
    ST_NORMAL = 3
    ST_PREPARE_SCALING = 4
    ST_SCALING = 5
    ST_COMPLETED_SCALING = 6
    ST_PREPARE_NORMAL = 7
    ST_TEARDOWN = 8

    tostr = ['ST_INIT', 'ST_HELLO', 'ST_READY', 'ST_NORMAL',
             'ST_PREPARE_SCALING', 'ST_SCALING', 'ST_COMPLETED_SCALING',
             'ST_PREPARE_NORMAL', 'ST_TEARDOWN']

    def __init__(self, cid, nf_name, host, core, ctrl_address, bg=False):
        self.cid = cid  # FIXME cid and nid
        self.nid = cid
        self.nf_name = nf_name
        self.cname = 's6_%s_%d' % (nf_name, cid)
        self.host = host
        self.core = core
        self.state_ip = host.state_addr
        self.state_port = STATE_PORT + self.cid
        self.ctrl_address = ctrl_address
        self.bg = bg
        self.state = self.ST_INIT
        self.cv = threading.Condition(threading.Lock())

    def start_container(self):
        nf_opts = ['-d %d' % self.cid,  # worker ID
                   '-n %d' % self.nid,  # node ID
                   '-c %d' % self.core,  # core ID
                   '-m %s:%d' % (self.ctrl_address[0], self.ctrl_address[1]),
                   # XXX: IP addr of this machine?
                   '-s %s:%d' % (self.state_ip, self.state_port),
                   ]

        if self.bg:
            nf_opts.append('-b')
        else:
            port_id = 0  # we use a single port per container

            # assumption: core id and vhost_user socket id is mapped
            sockpath = '{}/vhost_user{}_{}.sock'.format(
                SOCKDIR, self.core - 1, port_id)
            vdev = '--vdev=virtio_user{},path={}'.format(port_id, sockpath)

            nf_opts.append('-i "%s"' % vdev)  # vdev option for DPDK

        nf_cmd = 'catchsegv %s %s' % (nf_bins[self.nf_name], ' '.join(nf_opts))

        docker_cmd = 'docker run -d --name {name} --net=host -e S6_HOME={s6home} ' \
            '-v {huge}:{huge} -v {sock}:{sock} -v {s6dir}:{s6dir} ' \
            '{image} {cmd}'.format(name=self.cname, s6home=S6_HOME, huge=HUGEPAGES_PATH,
                                   sock=SOCKDIR, s6dir=S6_HOME, image=IMAGE, cmd=nf_cmd)

        try:
            self.host.ssh_cmd(docker_cmd)
        except subprocess.CalledProcessError as e:
            print(e, file=sys.stderr)
            return False

        return True

    def kill_container(self):
        self.host.release_core(self.core)

        try:
            cmd = 'docker rm -f {name}'.format(name=self.cname)
            self.host.ssh_cmd(cmd)
        except subprocess.CalledProcessError as e:
            print(e, file=sys.stderr)
            return False

        return True

    # Functions for synchronize workers behaviors
    # update called by s6ctl thread
    # notify called by communication thread
    def update(self, from_st, to_st):
        self.cv.acquire()
        if from_st and not self.state == from_st:
            raise Exception('Fail to state update "%s" -> "%s". Current state is "%s"' %
                            (self.tostr[from_st], self.tostr[to_st], self.tostr[self.state]))
        if VERBOSE:
            print('[Instance %d] Update state from %s to %s' %
                  (self.cid, self.tostr[self.state], self.tostr[to_st]))
        self.state = to_st
        self.cv.notify()
        self.cv.release()

    def wait(self, st):
        self.cv.acquire()
        while not self.state == st:
            self.cv.wait()
        self.cv.release()
