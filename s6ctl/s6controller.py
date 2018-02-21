from __future__ import print_function

import os
import re
import signal
import shlex
import SocketServer
import subprocess
import sys

from collections import OrderedDict
from datetime import datetime

from commthread import *
from nfinstance import *
from s6ctl_config import *

lb_rule = {'offset': 26, 'size': 8, 'ogates': 1,
           'method': 'hashing', 'direction': 'bidirectional'}

SCALING_TIMEOUT = 5  # in seconds


class KeySpace(object):
    LOC_NONE = 0
    LOC_LOCAL = 1
    LOC_STATIC = 2
    LOC_HASHING = 3
    LOC_CHASHING = 4
    LOC_BALANCED = 5

    def __init__(self, nf_name, host_to_inquire):
        self.default_rule = {'loc_type': self.LOC_LOCAL, 'param': 0}
        self.map_rules = []
        self.map_name_to_id = {}

        dump_cmd = '%s -D' % nf_bins[nf_name]
        lines = host_to_inquire.ssh_cmd(dump_cmd)

        # format: <map ID> <map type> <size> <name>
        # 0 2 16 g_user_byte_count
        # 1 2 240 g_dst_asset_map
        # 2 2 344 g_malicious_server_map
        for line in lines.splitlines():
            map_id, _, _, map_name = line.split()
            self.map_name_to_id[map_name] = map_id

        if VERBOSE:
            print('ID and name for maps:')
            for name, id in self.map_name_to_id.items():
                print(id, name)

    def get_json(self):
        return {'default_rule': self.default_rule,
                'map_rules': self.map_rules}

    def set_rule_default(self, loctype, param=0):
        self.default_rule = {'loc_type': loctype, 'param': param}

    # You can also directly specify a map ID in integer for map_name
    def set_rule(self, map_name, loctype, param=0):
        if isinstance(map_name, int):
            map_id = map_name
        else:
            map_id = self.map_name_to_id[map_name]
        self.map_rules.append({'map_id': int(map_id),
                               'loc_type': loctype, 'param': param})


class Host(object):

    def __init__(self, name, addr, state_addr):
        self.name = name
        self.addr = addr
        self.state_addr = state_addr
        self.num_cores = int(self.ssh_cmd('nproc'))

        self.avail_core = [True] * self.num_cores
        self.avail_core[0] = False  # preserve for bess daemon

    def available_cores(self):
        return len([core for core in range(self.num_cores)
                    if self.avail_core[core] == True])

    def acquire_core(self, core=None):
        for i in range(self.num_cores):
            if (core is None or core == i) and self.avail_core[i] == True:
                self.avail_core[i] = False
                return i
        raise Exception('No available core')

    def release_core(self, core):
        if core == 0 or core >= self.num_cores:
            raise Exception('Core %d cannot be released' % core)
        if self.avail_core[core] == True:
            raise Exception('Core %d is already released' % core)

        self.avail_core[core] = True

    def ssh_cmd(self, cmd=''):
        local_cmd = ("ssh -i %s -o StrictHostKeyChecking=no "
                     "-o BatchMode=yes %s '%s'" % (ID_RSA, self.addr, cmd))
        if VERBOSE:
            print('[%s(%s)] %s' % (self.name, self.addr, cmd))
        return subprocess.check_output(shlex.split(local_cmd))

    def ssh_cmd_bg(self, cmd=''):
        local_cmd = ("ssh -f -i %s -o StrictHostKeyChecking=no "
                     "-o BatchMode=yes %s '%s'" % (ID_RSA, self.addr, cmd))
        if VERBOSE:
            print('[%s(%s)] %s' % (self.name, self.addr, cmd))
        return subprocess.call(shlex.split(local_cmd))

    def start_host_daemon(self, hostenv, bess_config):
        set_env_cmd = ''
        for name, var in hostenv.items():
            set_env_cmd += 'export %s="%s"; ' % (name, var)

        bessd_cmd = ('%s %s %s' % (set_env_cmd, daemon_init, bess_config))
        self.ssh_cmd(bessd_cmd)

        return

    def stop_host_daemon(self):
        bessd_cmd = ('%s' % daemon_stop)
        self.ssh_cmd(bessd_cmd)
        return

    def start_measure(self, log_name):
        log_path = os.path.join(LOG_PATH, log_name)
        cmd = '%s %s' % (MEASUREMENT_PATH, log_path)
        self.ssh_cmd_bg(cmd)
        return

    def stop_measure(self):
        ps_list = self.ssh_cmd('ps -ef')

        for line in ps_list.splitlines():
            if 'measure.py' in line and 'python' in line:
                tokens = line.split()
                cmd = "kill -9 %s" % tokens[1]
                self.ssh_cmd(cmd)
        return


def _config_hostenv(hostenv_file):

    hostenv = {}

    with open(hostenv_file) as fp:
        for i, line in enumerate(fp):
            line = line.strip()
            if len(line) == 0 or line[0] == '#':
                continue
            tokens = line.split()
            if len(tokens) == 0 or len(tokens) <= 1:
                raise Exception('Error in %s (line %d): '
                                'lines must be <name> <var>' %
                                (host_env_file, i + 1))

            name = tokens[0]
            var = tokens[1]
            hostenv[name] = var
    return hostenv


def _config_hosts(host_config_file):

    hosts = OrderedDict()
    with open(host_config_file) as fp:
        for i, line in enumerate(fp):
            line = line.strip()
            if len(line) == 0 or line[0] == '#':
                continue
            tokens = line.split()
            if len(tokens) == 0 or len(tokens) <= 2:
                raise Exception('Error in %s (line %d): '
                                'lines must be <alias> <host addres> <state channel address>\n'
                                'address should be <hostname | IP address>' %
                                (host_config_file, i + 1))

            name = tokens[0]
            addr = tokens[1]
            state_addr = tokens[2]
            hosts[name] = Host(name, addr, state_addr)
    return hosts


class S6Controller(object):

    def __init__(self, nf_name):
        self.nf_name = nf_name
        self.hostenv = _config_hostenv(HOSTENV_PATH)
        self.hosts = _config_hosts(HOST_PATH)
        self.nf_instances = {}
        self.keyspace = KeySpace(nf_name, self.hosts.values()[0])

        self.thread = CommThread(CTRL_HOST, CTRL_PORT, self.nf_instances)
        self.ctrl_address = self.thread.get_address()
        self.thread.start()

    def set_env(self, name, var):
        self.hostenv(name, var)

    def get_env(self, name):
        if name in self.hostenv.keys():
            return None
        return self.hostenv[name]

    def get_available_apps(self):
        return nf_bins.keys()

    def get_host_names(self):
        return self.hosts.keys()

    def get_instance_ids(self):
        return self.nf_instances.keys()

    def _get_json_instances(self, cids):
        bgcount = 0
        pcount = 0
        worker_infos = []

        for cid in cids:
            instance = self.nf_instances[cid]
            if instance.bg:
                bgcount += 1
            else:
                pcount += 1
            worker_infos.append({'worker_id': cid,
                                 'node_id': cid,
                                 'state_port': '%s:%d' % (instance.state_ip, instance.state_port)})
        json = {'bgworker_count': bgcount, 'pworker_count':
                pcount, 'worker_infos': worker_infos}
        return json

    def _get_json_lbrule(self):
        return lb_rule

    # Returns a list of tuples:
    # [(<container name>, <host_name>, <container ID>, <status>, <label dict>), ...]
    def list(self, host_name):
        cname_set = set()
        ret = []
        docker_fmt = '"{{.ID}} | {{.Names}} | {{.Status}} | {{.Labels}}"'
        docker_cmd = 'docker ps -a --format=%s' % docker_fmt

        if host_name not in self.hosts:
            print('Host %s does not exists' % host_name, file=sys.stderr)
            return ret

        host = self.hosts[host_name]

        try:
            lines = host.ssh_cmd(docker_cmd).strip().split('\n')
        except subprocess.CalledProcessError as e:
            print(e, file=sys.stderr)
            return []

        for line in lines:
            if line.strip() == '':
                continue

            tokens = [token.strip() for token in line.split('|')]
            cid = tokens[0]
            cname = tokens[1]

            status = tokens[2]
            result = re.match(r'Exited \((\d+)\)', tokens[2])
            if result:
                status = int(result.group(1))

            labels = tokens[3].split(',')
            if labels[0] == '':
                labels = {}
            else:
                labels = dict([label.split('=') for label in labels])

            # Note: a container may appear multiple times. Add only one.
            #       (e.g., from a hostname and "localhost")
            if cname not in cname_set:
                cname_set.add(cname)
                ret.append((cname, host.name, cid, status, labels))

        return ret

    def listall(self):
        ret = []

        for host_name in self.hosts.keys():
            ret += self.list(host_name)

        return ret

    def container_logs(self, cid, lines=-1):
        if cid not in self.nf_instances:
            print('No cid %d container exists' % cid, file=sys.stderr)
            return

        instance = self.nf_instances[cid]
        docker_cmd = 'docker logs -t -f --tail=%d --details=true %s >&2' % \
            (lines, instance.cname)

        try:
            instance.host.ssh_cmd(docker_cmd)
        except subprocess.CalledProcessError as e:
            print(e, file=sys.stderr)
        except KeyboardInterrupt:
            pass

    def _cleanup_host(self, host):
        cmd = 'docker rm -f $(docker ps -a -q)'
        try:
            host.ssh_cmd(cmd)
        except subprocess.CalledProcessError as e:
            pass

    def set_application(self, nf_name):
        if nf_name not in nf_bins.keys():
            raise Exception('Application %s does not exists' % nf_name)

        self.nf_name = nf_name
        self.keyspace = KeySpace(nf_name, self.hosts.values()[0])

    def start_host_daemon(self, bessdconfig=""):
        for host_name, host in self.hosts.items():
            host.start_host_daemon(self.hostenv, bessdconfig)

    def start_measure(self):
        timestr = datetime.now().strftime('%y%m%d-%H%M')
        log_name = "%s_%s.log" % (self.nf_name, timestr)
        print('Start log on %s' % log_name)
        for host_name, host in self.hosts.items():
            host.start_measure(log_name)

    def stop_measure(self):
        print('Stop log ')
        for host_name, host in self.hosts.items():
            host.stop_measure()

    def init(self, cid, host_name, core=None, bg=False):
        if host_name not in self.hosts:
            print('Host %s does not exists' % host_name, file=sys.stderr)
            return

        host = self.hosts[host_name]
        core = host.acquire_core(core)
        instance = NFInstance(
            cid, self.nf_name, host, core, self.ctrl_address, bg)

        if instance.start_container():
            self.nf_instances[cid] = instance
            print('[Instance %d] Created in %s (core=%d)' %
                  (cid, host_name, core))

    # Start NF application as a clusters of cids
    def start(self, cids):
        for cid in cids:
            self.nf_instances[cid].wait(NFInstance.ST_HELLO)
            print('[Instance %d] Say hello' % cid)

        msg = json.dumps({
            'msg_type': 'init_rule',
            'workers': self._get_json_instances(cids),
            'rules': self._get_json_lbrule(),
            'keyspace': self.keyspace.get_json()
        })
        for cid in cids:
            self.thread.send(cid, msg)
        for cid in cids:
            self.nf_instances[cid].wait(NFInstance.ST_READY)
            print('[Instance %d] Ready to run' % cid)

        msg = json.dumps({
            'msg_type': 'all_ready',
        })
        for cid in cids:
            self.thread.send(cid, msg)
        for cid in cids:
            self.nf_instances[cid].wait(NFInstance.ST_NORMAL)
            print('[Instance %d] Run' % cid)

    # Join to the NF cluster
    def scale_out(self, out_cids):

        print('Stage1: Make ready new instances')
        for cid in out_cids:
            self.nf_instances[cid].wait(NFInstance.ST_HELLO)
            print('[Instance %d] Say hello' % cid)

        msg = json.dumps({
            'msg_type': 'init_rule',
            'workers': {'bgworker_count': 0, 'pworker_count': 0, 'worker_infos': []},
            'rules': self._get_json_lbrule(),
            'keyspace': self.keyspace.get_json()
        })
        for cid in out_cids:
            self.thread.send(cid, msg)
        for cid in out_cids:
            self.nf_instances[cid].wait(NFInstance.ST_READY)
            print('[Instance %d] Ready to run' % cid)

        msg = json.dumps({
            'msg_type': 'all_ready',
        })
        for cid in out_cids:
            self.thread.send(cid, msg)
        for cid in out_cids:
            self.nf_instances[cid].wait(NFInstance.ST_NORMAL)
            print('[Instance %d] Run' % cid)

        print('Stage2: Prepare scaling out')
        all_cids = self.nf_instances.keys()
        msg = json.dumps({
            'msg_type': 'prepare_scaling',
            'workers': self._get_json_instances(all_cids),
            'keyspace': self.keyspace.get_json()
        })
        for cid in all_cids:
            self.thread.send(cid, msg)
        for cid in all_cids:
            self.nf_instances[cid].wait(NFInstance.ST_PREPARE_SCALING)
            print('[Instance %d] Ready to scale' % cid)

        print('Stage3: Start scaling out')
        msg = json.dumps({'msg_type': 'start_scaling'})
        for cid in all_cids:
            self.thread.send(cid, msg)
        for cid in all_cids:
            self.nf_instances[cid].wait(NFInstance.ST_SCALING)
            print('[Instance %d] Start scaling-out' % cid)

        # Change reload-balance

        print('Stage4: Wait scaling out done')
        time.sleep(SCALING_TIMEOUT)  # wait until scalig down

        # XXX force complete scaling after timeout
        # msg = json.dumps({'msg_type': 'force_complete_scaling'})
        # for cid in all_cids:
        #    self.thread.send(cid, msg)

        # time.sleep(2) # wait until scalig down

        for cid in all_cids:
            self.nf_instances[cid].wait(NFInstance.ST_COMPLETED_SCALING)
            print('[Instance %d] Completed scaling-out' % cid)

        print('Stage5: Go Normal mode')
        msg = json.dumps({'msg_type': 'prepare_quiescent'})
        for cid in all_cids:
            self.thread.send(cid, msg)
        for cid in all_cids:
            self.nf_instances[cid].wait(NFInstance.ST_PREPARE_NORMAL)
            print('[Instance %d] Prepare back to normal operations' % cid)

        msg = json.dumps({'msg_type': 'quiescent'})
        for cid in all_cids:
            self.thread.send(cid, msg)
        for cid in all_cids:
            self.nf_instances[cid].wait(NFInstance.ST_NORMAL)
            print('[Instance %d] Run normal operations' % cid)

    # Leave from the NF cluster
    def scale_in(self, in_cids):
        for cid in in_cids:
            if cid not in self.nf_instances:
                raise Exception('Instance %s is not exists')

        all_cids = self.nf_instances.keys()
        after_scale = list(set(all_cids) - set(in_cids))

        print('Stage1: Prepare scaling in')
        msg = json.dumps({
            'msg_type': 'prepare_scaling',
            'workers': self._get_json_instances(after_scale),
            'keyspace': self.keyspace.get_json()
        })
        for cid in all_cids:
            self.thread.send(cid, msg)
        for cid in all_cids:
            self.nf_instances[cid].wait(NFInstance.ST_PREPARE_SCALING)
            print('[Instance %d] Ready to scale' % cid)

        print('Stage2: Start scaling in')
        msg = json.dumps({'msg_type': 'start_scaling'})
        for cid in all_cids:
            self.thread.send(cid, msg)
        for cid in all_cids:
            self.nf_instances[cid].wait(NFInstance.ST_SCALING)
            print('[Instance %d] Start scaling-in' % cid)

        # Change reload-balance

        print('Stage3: Wait scaling in done')
        time.sleep(SCALING_TIMEOUT)  # wait until scalig down

        # XXX force complete scaling after timeout
        # msg = json.dumps({'msg_type': 'force_complete_scaling'})
        # for cid in all_cids:
        #    self.thread.send(cid, msg)

        # time.sleep(2) # wait until scalig down

        for cid in all_cids:
            self.nf_instances[cid].wait(NFInstance.ST_COMPLETED_SCALING)
            print('[Instance %d] Completed scaling-in' % cid)

        print('Stage4: Go Normal mode')
        msg = json.dumps({'msg_type': 'prepare_quiescent'})
        for cid in all_cids:
            self.thread.send(cid, msg)
        for cid in all_cids:
            self.nf_instances[cid].wait(NFInstance.ST_PREPARE_NORMAL)
            print('[Instance %d] Prepare back to normal operations' % cid)

        msg = json.dumps({'msg_type': 'quiescent'})
        for cid in all_cids:
            self.thread.send(cid, msg)
        for cid in all_cids:
            self.nf_instances[cid].wait(NFInstance.ST_NORMAL)
            print('[Instance %d] Run normal operations' % cid)

    def kill(self, cid):
        if cid not in self.nf_instances.keys():
            print('No cid %d container exists' % cid, file=sys.stderr)
            return

        instance = self.nf_instances[cid]
        del self.nf_instances[cid]

        if instance.kill_container():
            print('[Instance %d] Killed' % cid)

    def kill_all(self):
        for instance_cid in self.nf_instances.keys():
            instance = self.nf_instances[instance_cid]
            del self.nf_instances[instance_cid]
            instance.kill_container()

        for host_name, host in self.hosts.items():
            host.stop_host_daemon()

    def cleanup_host(self, host_name):
        if host_name not in self.hosts:
            print('Host %s does not exists' % host_name, file=sys.stderr)
            return
        host = self.hosts[host_name]

        self._cleanup_host(host)

    def cleanup(self):
        for host_name in self.hosts:
            self.cleanup_host(host_name)

    def tear_down(self):
        self.kill_all()
        self.thread.stop()
        self.thread.join()
