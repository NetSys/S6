import os
import pprint
import time

S6_HOME = os.getenv('S6_HOME', os.path.join(os.path.expanduser('~'), 'S6'))
BESS_CONFIG_DIR = os.path.join(S6_HOME, 's6ctl/bess_config')

BESS_CONFIG = 'flowgen_multi_source.bess'
NF_IDS_NAME = 'ids'


def run(s6ctl):
    print('Starting static scaling experiments')

    print('Start host daemons with %s' % BESS_CONFIG)
    s6ctl.start_host_daemon(
        os.path.join(BESS_CONFIG_DIR, BESS_CONFIG))

    print('Start application: %s' % NF_IDS_NAME)
    s6ctl.set_application(NF_IDS_NAME)

    s6ctl.keyspace.set_rule_default(s6ctl.keyspace.LOC_LOCAL)
    # s6ctl.keyspace.set_rule('g_tcp_flow_map', s6ctl.keyspace.LOC_STATIC,
    #                        param=1)
    print('Keyspace configuration:')
    pprint.pprint(s6ctl.keyspace.get_json())

    # XXX packet worker should be start from 0, and increase/decrease sequentially
    # maximum number of packet workers is 16 (0 - 15)
    s6ctl.init(0, 'localhost', core=1)
    # s6ctl.init(1, 'localhost', core=2)

    # XXX bg worker id should be 16
    s6ctl.init(16, 'localhost', core=3, bg=True)

    s6ctl.start([0, 16])

    s6ctl.start_measure()

    time.sleep(10)

    s6ctl.stop_measure()

    s6ctl.kill_all()
