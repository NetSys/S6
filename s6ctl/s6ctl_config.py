import os
import socket

S6_HOME = os.getenv('S6_HOME', os.path.join(os.path.expanduser('~'), 'S6'))
HUGEPAGES_PATH = os.getenv('HUGEPAGES_PATH', '/dev/hugepages')
SOCKDIR = '/tmp/bessd'
IMAGE = 'ubuntu:latest'

ID_RSA = os.path.join(S6_HOME, 's6ctl/host_config/id_rsa')
HOST_PATH = os.path.join(S6_HOME, 's6ctl/host_config/hosts.txt')
HOSTENV_PATH = os.path.join(S6_HOME, 's6ctl/host_config/hostenv.txt')
MEASUREMENT_PATH = os.path.join(S6_HOME, 's6ctl/bess_config/measure.py')
LOG_PATH = os.path.join(S6_HOME, 's6ctl/log/')


def get_myip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(('8.8.8.8', 53))
    (ip, port) = s.getsockname()
    s.close()
    return ip


CTRL_HOST = get_myip()
CTRL_PORT = 9999
STATE_PORT = 1000

VERBOSE = bool(int(os.getenv('VERBOSE', '0')))

nf_bins = {
    'echo': os.path.join(S6_HOME, 'bin/apps/echo_app'),
    'sink': os.path.join(S6_HOME, 'bin/apps/sink_app'),
    'prads': os.path.join(S6_HOME, 'bin/prads'),
    'ids': os.path.join(S6_HOME, 'bin/apps/snort_app'),
    'nat': os.path.join(S6_HOME, 'bin/apps/nat_app'),
    'reasm': os.path.join(S6_HOME, 'bin/apps/reasm_app'),
}

daemon_init = os.path.join(S6_HOME, 'script/init_host_daemon.sh')
daemon_stop = os.path.join(S6_HOME, 'script/stop_host_daemon.sh')
