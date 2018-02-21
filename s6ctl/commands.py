import fnmatch
import imp
import os
import re


cmdlist = []


def is_allowed_filename(basename):
    # do not allow whitespaces
    for c in basename:
        if c.isspace():
            return False
    return True


def complete_filename(partial_word, start_dir='', suffix='',
                      skip_suffix=False):
    try:
        sub_dir, partial_basename = os.path.split(partial_word)
        pattern = '%s*%s' % (partial_basename, suffix)

        target_dir = os.path.join(start_dir, os.path.expanduser(sub_dir))
        if target_dir:
            basenames = os.listdir(target_dir)
        else:
            basenames = os.listdir(os.curdir)

        candidates = []
        for basename in basenames + ['.', '..']:
            if basename.startswith('.'):
                if not partial_basename.startswith('.'):
                    continue

            if not is_allowed_filename(basename):
                continue

            if os.path.isdir(os.path.join(target_dir, basename)):
                candidates.append(basename + '/')
            else:
                if fnmatch.fnmatch(basename, pattern):
                    if suffix and not skip_suffix:
                        basename = basename[:-len(suffix)]
                    candidates.append(basename)

        ret = []
        for candidate in candidates:
            ret.append(os.path.join(sub_dir, candidate))
        return ret

    except OSError:
        # ignore failure of os.listdir()
        return []


def get_var_attrs(cli, var_token, partial_word):
    var_type = None
    var_desc = ''
    var_candidates = []

    if var_token == 'HOST':
        var_type = 'name'
        var_candidates = cli.s6ctl.get_host_names()
    elif var_token == 'NEW_CID':
        var_type = 'int'
    elif var_token == 'CID':
        var_type = 'int'
        var_candidates = [str(cid) for cid in cli.s6ctl.get_instance_ids()]
    elif var_token == 'CID...':
        var_type = 'int'
        var_desc = 'one or more of cids'
    elif var_token == 'CORE':
        var_type = 'int'
    elif var_token == 'CNAME':
        var_type = 'name'
        var_desc = 'container name'
    elif var_token == 'SCRIPT':
        var_type = 'script'
        var_desc = 'script name in "scripts/" directory'
        var_candidates = complete_filename(partial_word,
                                           '%s/scripts' % cli.this_dir,
                                           '.py')
    elif var_token == 'BESSCONFIG':
        var_type = 'script'
        var_desc = 'script name in "bess_config/" directory'
        var_candidates = complete_filename(partial_word,
                                           '%s/bess_config' % cli.this_dir,
                                           '.bess')
    elif var_token == 'APP_NAME':
        var_type = 'name'
        var_desc = 'application name'
        var_candidates = cli.s6ctl.get_available_apps()

    if var_type is None:
        return None
    else:
        return var_type, var_desc, var_candidates


# Return (head, tail)
#   head: consumed string portion
#   tail: the rest of input line
# You can assume that 'line == head + tail'
def split_var(cli, var_type, line):
    if var_type in ['name', 'script', 'int']:
        pos = line.find(' ')
        if pos == -1:
            head = line
            tail = ''
        else:
            head = line[:pos]
            tail = line[pos:]
    else:
        raise cli.InternalError('type "%s" is undefined', var_type)

    return head, tail


def bind_var(cli, var_type, line):
    head, remainder = split_var(cli, var_type, line)

    # default behavior
    val = head

    if var_type == 'name':
        if re.match(r'^[_a-zA-Z][\w]*$', val) is None:
            raise cli.BindError('"name" must be [_a-zA-Z][_a-zA-Z0-9]*')

    elif var_type == 'int':
        try:
            val = int(val)
        except Exception:
            raise cli.BindError('Expected an integer')

    return val, remainder


def cmd(syntax, desc=''):
    def cmd_decorator(func):
        cmdlist.append((syntax, desc, func))
    return cmd_decorator


@cmd('help', 'List available commands')
def help(cli):
    for syntax, desc, _ in cmdlist:
        cli.fout.write('  %-50s%s\n' % (syntax, desc))
    cli.fout.flush()


@cmd('quit', 'Quit CLI')
def quit(cli):
    cli.s6ctl.tear_down()
    raise EOFError()


@cmd('history', 'Show command history')
def history(cli):
    if cli.rl:
        len_history = cli.rl.get_current_history_length()
        begin_index = max(1, len_history - 100)     # max 100 items
        for i in range(begin_index, len_history):   # skip the last one
            cli.fout.write('%5d  %s\n' % (i, cli.rl.get_history_item(i)))
    else:
        cli.err('"readline" not available')


@cmd('show', 'List all containers')
def show(cli):
    containers = cli.s6ctl.listall()
    print('%-16s%-16s%-20s%-20s%s' % ('NAME', 'HOST', 'ID', 'EXIT STATUS',
                                      'LABELS'))
    for cname, host_name, cid, status, labels in containers:
        print('%-16s%-16s%-20s%-20s%s' %
              (cname, host_name, cid, status, labels))


@cmd('show HOST', 'List all containers on the specified host')
def show_host(cli, host_name):
    containers = cli.s6ctl.list(host_name)
    print('%-16s%-20s%-20s%s' % ('NAME', 'ID', 'EXIT STATUS', 'LABELS'))
    for cname, host_name, cid, status, labels in containers:
        print('%-16s%-20s%-20s%s' % (cname, cid, status, labels))


@cmd('log CID', 'Show container output')
def log_container(cli, cid):
    cli.s6ctl.container_logs(cid)


@cmd('run SCRIPT', 'Run script')
def run_script(cli, script):
    target_dir = '%s/scripts' % cli.this_dir
    basename = os.path.expanduser('%s.py' % script)
    path = os.path.join(target_dir, basename)
    module = imp.load_source(os.path.splitext(basename)[0], path)
    module.run(cli.s6ctl)


@cmd('set-app APP_NAME', 'Set network function app')
def set_application(cli, nf_name):
    cli.s6ctl.set_application(nf_name)


@cmd('start-host', 'Start host daemons')
def host_start(cli):
    cli.s6ctl.start_host_daemon()


@cmd('start-host BESSCONFIG', 'Start host daemons with BESSCONFIG')
def host_start_with_config(cli, bessconfig):
    target_dir = '%s/bess_config' % cli.this_dir
    basename = os.path.expanduser('%s.bess' % bessconfig)
    path = os.path.join(target_dir, basename)
    cli.s6ctl.start_host_daemon(path)


@cmd('init NEW_CID HOST [CORE]', 'Start a new container')
def init(cli, cid, host_name, core=None):
    cli.s6ctl.init(cid, host_name, core)


@cmd('start CID...', 'Run NF instances as a cluster')
def start(cli, cids):
    cli.s6ctl.start(cids)


@cmd('scale-out CID...', 'Join to the NF cluster')
def scale_out(cli, cids):
    cli.s6ctl.scale_out(cids)


@cmd('sclae-in CID...', 'Leave from the NF cluster')
def scale_in(cli, cids):
    cli.s6ctl.scale_in(cids)


@cmd('kill CID', 'Kill a running container')
def kill(cli, cid):
    cli.s6ctl.kill(cid)


@cmd('kill', 'Kill all running containers')
def kill_all(cli):
    cli.s6ctl.kill_all()


@cmd('cleanup HOST', 'Force to remove all containers')
def cleanup_host(cli, host_name):
    cli.s6ctl.cleanup_host(host_name)


@cmd('cleanup', 'Force to remove all containers')
def cleanup(cli):
    cli.s6ctl.cleanup()
