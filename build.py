#!/usr/bin/env python2.7

# TODO: Per-application source code generation and compile
import sys
import os
import subprocess

S6_DIR = os.path.dirname(os.path.abspath(__file__))


def cmd(cmd):
    returncode = subprocess.call(cmd)

    if returncode:
        print >> sys.stderr, 'Error has occured running command: %s' % cmd
        sys.exit(returncode)


def print_log(msg):
    color_start = '\033[32m'
    color_end = '\033[0m'
    print '%sS6Builder> %s %s' % (color_start, msg, color_end)


def clean_deps():
    print_log('Cleaning deps libraries')

    os.chdir(S6_DIR + '/deps')

    cmd(['make', 'clean', '-f', 'Makefile.dpdk'])
    cmd(['make', 'clean', '-f', 'Makefile.boost'])
    cmd(['make', 'clean', '-f', 'Makefile.rapidjson'])


def clean_source():
    print_log('Cleaning auto-generated source code')

    os.chdir(S6_DIR)

    cmd(['./script/s6_codegen', 'clean_all'])


def clean_core():
    print_log('Cleaning core library')

    os.chdir(S6_DIR + '/core')

    cmd(['make', 'clean'])


def clean_apps():
    print_log('Cleaning applications')

    os.chdir(S6_DIR + '/mk')

    cmd(['make', 'clean', '-f', 'apps.mk'])
    cmd(['make', 'clean', '-f', 'evals.mk'])
    cmd(['make', 'clean', '-f', 'prads.mk'])


def clean_s6():
    clean_apps()
    clean_core()
    clean_source()


def build_deps():
    print_log('Building deps libraries')

    os.chdir(S6_DIR + '/deps')

    cmd(['make', '-f', 'Makefile.dpdk'])
    cmd(['make', '-f', 'Makefile.boost'])
    cmd(['make', '-f', 'Makefile.rapidjson'])


def generate_source():
    print_log('Generating source code')

    os.chdir(S6_DIR)

    cmd(['./script/s6_codegen', 'generate_all'])


def build_core():
    print_log('Building core library')

    os.chdir(S6_DIR + '/core')

    cmd('make')


def build_apps():
    print_log('Building applications')

    os.chdir(S6_DIR + '/mk')

    cmd(['make', '-f', 'apps.mk'])
    cmd(['make', '-f', 'evals.mk'])
    cmd(['make', '-f', 'prads.mk'])


def build_s6():
    generate_source()
    build_core()
    build_apps()


def print_usage():
    print >> sys.stderr, \
        'Usage: ' \
            'default behavior "build s6"\n' \
            '  help\n' \
            '  build [s6|deps|source|core|apps] (default: s6)\n' \
            '  clean [s6|deps|source|core|apps] (default: apps)'


def main():
    argv_len = len(sys.argv)
    if argv_len == 1:
        build_s6()
    elif argv_len >= 2:
        if sys.argv[1] == 's6':
            build_s6()
        elif sys.argv[1] == 'build':
            if argv_len >= 3:
                if sys.argv[2] == 's6':
                    build_s6()
                elif sys.argv[2] == 'deps':
                    build_deps()
                elif sys.argv[2] == 'source':
                    generate_source()
                elif sys.argv[2] == 'core':
                    build_core()
                elif sys.argv[2] == 'apps':
                    build_apps()
                else:
                    print >> sys.stderr, \
                        'Unkonwn command "%s".' % sys.argv[2]
            else:
                build_s6()
        elif sys.argv[1] == 'clean':
            if argv_len >= 3:
                if sys.argv[2] == 's6':
                    clean_s6()
                elif sys.argv[2] == 'deps':
                    clean_deps()
                elif sys.argv[2] == 'source':
                    clean_source()
                elif sys.argv[2] == 'core':
                    clean_core()
                elif sys.argv[2] == 'apps':
                    clean_apps()
                else:
                    print >> sys.stderr, \
                        'Unkonwn command "%s".' % sys.argv[2]
            else:
                clean_apps()
        elif sys.argv[1] == 'help':
            print_usage()
        else:
            print >> sys.stderr, \
                'Unkonwn command "%s".' % sys.argv[1]
    else:
        print >> sys.stderr, \
            'Unkonwn command "%s".' % sys.argv[1]


if __name__ == '__main__':
    main()
