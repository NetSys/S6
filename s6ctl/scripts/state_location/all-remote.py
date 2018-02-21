import os
import pprint

S6_HOME = os.getenv('S6_HOME', os.path.join(os.path.expanduser('~'), 'S6'))
BESS_DIR_CONFIG = os.path.join(S6_HOME, 's6ctl/bess_config')


def run(s6ctl):
    print('Comparison between all local, all remote, S6')

    print('Start host daemons')
    s6ctl.start_host_daemon()

    # All-remote
    print('All remote')
    print('Keyspace configuration:')
    s6ctl.keyspace.set_rule_default(s6ctl.keyspace.LOC_STATIC, 16)
    pprint.pprint(s6ctl.keyspace.get_json())

    # XXX packet worker should be start from 0, and increase/decrease sequentially
    # maximum number of packet workers is 16 (0 - 15)
    s6ctl.init(0, 'center', core=1)
    s6ctl.init(1, 'center', core=2)

    s6ctl.init(16, 'mcgee', core=1, bg=True)

    s6ctl.start([0, 1, 16])
