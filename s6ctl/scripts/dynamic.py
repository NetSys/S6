import os
import pprint
import time

S6_HOME = os.getenv('S6_HOME', os.path.join(os.path.expanduser('~'), 'S6'))
BESS_DIR_CONFIG = os.path.join(S6_HOME, 's6ctl/bess_config')


def run(s6ctl):
    print('Starting dynamic scaling experiments')

    print('Start host daemons')
    s6ctl.start_host_daemon()

    print('Keyspace configuration:')
    s6ctl.keyspace.set_rule_default(s6ctl.keyspace.LOC_LOCAL)
    pprint.pprint(s6ctl.keyspace.get_json())

    cid = 0
    s6ctl.init(cid, 'localhost', core=cid + 1)
    s6ctl.start([0])

    # scale-out
    for cid in [1, 2]:
        time.sleep(2)
        s6ctl.init(cid, 'localhost', core=cid + 1)
        s6ctl.scale_out([cid])

    # scale-in
    for cid in [2, 1]:
        time.sleep(2)
        s6ctl.scale_in([cid])
        s6ctl.kill(cid)
