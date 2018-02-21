#!/usr/bin/env python2.7
from __future__ import print_function

import string
import sys
import time
import os

try:
    BESS_HOME = os.getenv(
        'BESS_HOME', os.path.join(os.path.expanduser('~'), 'bess'))
    sys.path.insert(1, BESS_HOME)
    from pybess.bess import *
except ImportError:
        print('Cannot import the API module (pybess)', file=sys.stderr)

MAX_VPORTS = 3


def connect_bess():
    s = BESS()
    try:
        s.connect()
    except s.APIError as e:
        raise Exception('Cannot connect to BESSD')
    return s


def get_measure_modules(bess):
    modules = []

    m_name = 'measure'
    summary = None

    try:
        summary = bess.run_module_command(
            m_name, 'get_summary', "EmptyArg", {})
    except Exception as e:
        print(e.message, file=sys.stderr)
        # module does not exists
        pass

    if summary is not None:
        modules.append(m_name)

    for i in range(MAX_VPORTS):
        m_name = 'measure_{}'.format(i)

        try:
            bess.run_module_command(m_name, 'get_summary', "EmptyArg", {})
        except Exception as e:
            print(e.message, file=sys.stderr)
            # module does not exists
            continue

        modules.append(m_name)

    return modules


def measure_performance(bess, measures, log_file):

    if len(measures) > 0:
        print('Start measuring performance: %s' % (' '.join(measures)))
    else:
        print('No measure module available')
        return

    logfd = sys.stdout

    if log_file:
        logfd = open(log_file, 'w')

    lasts = [bess.run_module_command(measure, 'get_summary', "EmptyArg", {})
             for measure in measures]

    while True:
        time.sleep(1)

        nows = [bess.run_module_command(measure, 'get_summary', "EmptyArg", {})
                for measure in measures]

        for mid in range(len(measures)):
            last = lasts[mid]
            now = nows[mid]

            diff_ts = last.timestamp - now.timestamp
            diff_pkts = (last.packets - now.packets) / diff_ts
            diff_bits = (last.bits - now.bits) / diff_ts
            diff_total_latency_ns = (
                last.total_latency_ns - now.total_latency_ns) / diff_ts

            lasts[mid] = nows[mid]

            if diff_pkts >= 1.0:
                ns_per_packet = diff_total_latency_ns / diff_pkts
            else:
                ns_per_packet = 0

            logfd.write('%s %d: %.3f Mpps, %.3f Mbps, rtt_avg: %.3f us, rtt_med: %.3f us, '
                        'rtt_99th: %.3f us, jitter_med: %.3f us, jitter_99th: %.3f us\n' %
                        (time.ctime(now.timestamp),
                         mid,
                         diff_pkts / 1e6,
                         diff_bits / 1e6,
                         ns_per_packet / 1e3,
                         last.latency_50_ns / 1e3,
                         last.latency_99_ns / 1e3,
                         last.jitter_50_ns / 1e3,
                         last.jitter_99_ns / 1e3))
            logfd.flush()


def main():
    bess = connect_bess()

    log_file = None

    if len(sys.argv) == 2:
        log_file = sys.argv[1]

    try:
        measures = get_measure_modules(bess)
        measure_performance(bess, measures, log_file)
    except KeyboardInterrupt:
        print('Stop measuring performance')

if __name__ == '__main__':
    main()
