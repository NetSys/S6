#!/usr/bin/env python2.7

import sys
import os
import os.path
import glob
import pprint
import fnmatch
import string

this_dir = os.path.dirname(os.path.realpath(__file__))

TEMPLATE_DIR = this_dir + '/templates/'

TEMPLATE_MW_STUB = TEMPLATE_DIR + 'mw_stub.hh'
TEMPLATE_MW_STUB_CLASS = TEMPLATE_DIR + 'mw_stub_class.hh'
TEMPLATE_MW_STUB_METHOD = TEMPLATE_DIR + 'mw_stub_method.hh'
TEMPLATE_MW_SKELETON = TEMPLATE_DIR + 'mw_skeleton.hh'
TEMPLATE_MW_SKELETON_CLASS = TEMPLATE_DIR + 'mw_skeleton_class.hh'
TEMPLATE_SKELETON_METHOD = TEMPLATE_DIR + 'skeleton_method.hh'
TEMPLATE_SW_STUB = TEMPLATE_DIR + 'sw_stub.hh'
TEMPLATE_SW_STUB_CLASS = TEMPLATE_DIR + 'sw_stub_class.hh'
TEMPLATE_SW_STUB_METHOD = TEMPLATE_DIR + 'sw_stub_method.hh'
TEMPLATE_SW_STUB_CONST_METHOD = TEMPLATE_DIR + 'sw_stub_const_method.hh'
TEMPLATE_ADT_INFO_SOURCE = TEMPLATE_DIR + 'adt_info.cpp'

USR_CONST_FLAG = 0x1
USR_RESTRICT_FLAG = 0x2
USR_VOLATILE_FLAG = 0x4

DIST_TYPE_MAP = {"SwMap": "ADT_MAP", "MwMap": "ADT_MAP"}

# Need to be updated with src/Dbject.hh
ATTR_TO_DEF_MAP = {"operation_stale": "_FLAG_STALE",
                   "operation_behind": "_FLAG_BEHIND"}

try:
    from clang.cindex import *
    from clang.cindex import AccessSpecifier
except ImportError:
    print >> sys.stderr, '"python-clang" package is not available.'
    sys.exit(1)

commands = ['generate', 'dump']
parse_opts = ['-x', 'c++', '-stc=c++11']
all_stubs_include = []
all_keys_include = []
all_user_classes = []
all_global_maps = {}
all_key_classes = []

output_dir = './'
apps_dirs = []
keys_dirs = []
objects_dirs = []

from pprint import pprint


def dump_recur(node, prefix, lines_dump):
    kind = node.kind.name
    name = node.spelling or node.displayname

    try:
        token_loc = node.get_tokens().next().location
    except StopIteration:
        return

    if str(token_loc.file) != dump.mainfile:
        line_pos = 0
        # skip for included header files
        return
    else:
        line_pos = token_loc.line - 1

    lines_dump.append(
        (line_pos, '%s%4d: %s %s' % (prefix, line_pos + 1, kind, name)))

    for child in node.get_children():
        dump_recur(child, prefix + '    ', lines_dump)


def dump(filename):
    lines_dump = []
    lines_src = map(lambda x: x.rstrip(), open(filename).readlines())

    index = Index.create()
    t_unit = index.parse(filename, parse_opts)

    dump.mainfile = str(t_unit.cursor.get_tokens().next().location.file)
    dump_recur(t_unit.cursor, '', lines_dump)

    max_width = max(map(lambda x: len(x[1]), lines_dump))

    cur_line = 0
    for cur_index, (line_pos, dump_line) in enumerate(lines_dump):
        while cur_line < line_pos:
            print '%-*s| %d: %s' % \
                (max_width + 1, '', cur_line + 1, lines_src[cur_line]),
            cur_line += 1

        print '%-*s|' % (max_width + 1, dump_line),

        if cur_index + 1 < len(lines_dump):
            next_line = lines_dump[cur_index + 1][0]
        else:
            next_line = 999999

        first = True
        while cur_line < len(lines_src):
            if cur_line >= next_line and cur_line > line_pos:
                break

            if first:
                first = False
            else:
                print '\n%-*s|' % (max_width + 1, ''),
            print '%4d: %s' % (cur_line + 1, lines_src[cur_line]),

            cur_line += 1
        print


class CxxClass(object):

    def __init__(self, name, base):
        self.name = name
        self.base = base
        self.methods = []

    def __str__(self):
        buf = ['Class %s (%s)' % (self.name, self.base)]
        for method in self.methods:
            ret = str(method).split('\n')
            for line in ret:
                buf.append('  ' + line)
        return '\n'.join(buf)

    def add_method(self, method):
        self.methods.append(method)


class CxxMethod(object):

    def __init__(self, name, is_const, return_type, return_size):
        self.name = name
        self.return_type = return_type
        self.return_size = return_size
        self.arguments = []
        self.attribute = set()
        if is_const:
            self.const = 'const'
        else:
            self.const = ''

    def __str__(self):
        buf2 = ""
        if len(self.attribute) != 0:
            buf2 = ": attribute " + str(self.attribute)
        buf = ['Method %s (%s: %d bytes)%s' %
               (self.name, self.return_type, self.return_size, buf2)]
        for argument in self.arguments:
            ret = str(argument).split('\n')
            for line in ret:
                buf.append('  ' + line)
        return '\n'.join(buf)

    def add_argument(self, argument):
        self.arguments.append(argument)


class CxxArgument(object):

    def __init__(self, name, arg_type, size, code, s6_ref_type):
        self.name = name
        self.arg_type = arg_type
        self.size = size
        self.code = code
        self.s6_ref_type = s6_ref_type

    def __str__(self):
        return 'Argument %s (%s: %s bytes) - %s' % \
            (self.name, self.arg_type, self.size, self.code)


def get_method(cls, node, is_allow_pointer):
    name = node.spelling

    if (not is_allow_pointer) and (node.result_type.kind == TypeKind.POINTER):
        print >> sys.stderr, 'Error: %s::%s() has a pointer return ' \
            'type: %s' % (cls.name, name, return_type)
        sys.exit(1)

    return_type = node.result_type.spelling
    return_size = node.result_type.get_size()
    if return_size < 0:
        return_size = 0

    is_const = False
    usr_str = node.get_usr()
    if usr_str.rfind('#') + 2 == len(usr_str):
        if string.atoi(usr_str[-1]) & USR_CONST_FLAG:
            is_const = True

    method = CxxMethod(name, is_const, return_type, return_size)
    for x in node.get_children():
        if x.kind.is_attribute():
            if x.spelling in ATTR_TO_DEF_MAP:
                method.attribute.add(x.spelling)

    # print dir(node)
    for arg in node.get_arguments():
        # XXX: What about normal references?
        # XXX: What about dist_ref?

        s6_ref_type = None

        if (not is_allow_pointer) and (arg.type.kind == TypeKind.POINTER):
            print >> sys.stderr, 'Error: %s::%s() has a pointer argument: ' \
                '%s' % (cls.name, name, arg.spelling)
            sys.exit(1)

        arg_name = arg.spelling
        arg_type = arg.type.spelling
        assert(arg.type.get_size() >= 0)   # XXX: is 0-byte argument possible?
        arg_size = str(arg.type.get_size())

        if (string.find(arg_type, 'MRef') != -1 or
                string.find(arg_type, 'SRef') != -1):
            x = string.index(arg_type, '<')
            y = string.index(arg_type, '>')
            if (x + 1 >= y):
                print >> sys.stderr, 'Error: %s is not a valid class type' % \
                    arg_type
                sys.exit(1)
            s6_ref_type = arg.type.spelling[x + 1:y]
            arg_size = '%s.get_serial_size()' % arg_name

        src_file = arg.extent.start.file.name
        offset_start = arg.extent.start.offset
        offset_end = arg.extent.end.offset

        with open(src_file) as f:
            f.seek(offset_start)
            arg_code = f.read(offset_end - offset_start)

        method.add_argument(CxxArgument(arg_name, arg_type, arg_size, arg_code,
                                        s6_ref_type))

    return method

# for debugging


def obj_dump(obj):
    print >> sys.stderr, '-------------------'
    print >> sys.stderr, repr(obj)
    for x in dir(obj):
        if x.startswith('_'):
            continue

        try:
            val = getattr(obj, x)
            if callable(val):
                print >> sys.stderr, '  %s()' % x
            else:
                print >> sys.stderr, '  %s: (%s) %s' % (x, type(val), str(val))
        except:
            pass
    print >> sys.stderr


def get_dobj_base(node, filename):
    for child in node.get_children():
        if child.kind.name == 'CXX_BASE_SPECIFIER':
            for grandchild in child.get_children():
                if grandchild.kind.name != 'TYPE_REF':
                    continue
                if grandchild.type.spelling == 'DObject':
                    print >> sys.stderr, \
                        'Error: %s directly inherits DObject!' % \
                            node.spelling
                    sys.exit(1)
                if grandchild.type.spelling in ['SWObject', 'MWObject']:
                    return grandchild.type.spelling
                if grandchild.type.spelling in ['SticKey', 'Key']:
                    all_key_classes.append((node.spelling,
                                            grandchild.type.spelling, filename))
                    all_keys_include.append(filename[filename.rfind('/') +
                                                     1:])
                    return None

    return None


def find_class(classes, node, filename):
    if node.kind.name != 'CLASS_DECL':
        for child in node.get_children():
            find_class(classes, child, filename)
        return

    if str(node.get_tokens().next().location.file) != find_classes.mainfile:
        return

    generate.base = get_dobj_base(node, filename)
    if generate.base is None:
        print >> sys.stderr, 'Ignoring non-DObject class %s' % node.spelling
        return

    cls = CxxClass(node.spelling, generate.base)

    for child in node.get_children():
        child_type = child.kind.name

        if child_type != 'CXX_METHOD':
            continue

        # ignore method with underbar --> they are not accessible through
        # skeleton
        if child.spelling[0] == '_':
            continue

        if child.is_static_method():
            print >> sys.stderr, 'Ignoring static method %s' % \
                child.spelling
            continue

        if child.access_specifier != AccessSpecifier.PUBLIC:
            print >> sys.stderr, 'Ignoring non-public method %s' % \
                child.spelling
            continue

        if generate.base == 'SWObject':
            cls.add_method(get_method(cls, child, True))
        elif generate.base == 'MWObject':
            cls.add_method(get_method(cls, child, False))

    classes.append(cls)


def find_classes(filename):
    index = Index.create()
    t_unit = index.parse(filename, parse_opts)

    find_classes.mainfile = str(
        t_unit.cursor.get_tokens().next().location.file)

    classes = []
    find_class(classes, t_unit.cursor, filename)
    return classes


def replace(filename, macros, indent=''):
    ret = []

    for line in open(filename):
        for old, new in macros.iteritems():
            old = '%%%s%%' % old
            new = str(new)

            if line.strip() == old:
                if new.strip():
                    ret.append(line.replace(old, new))
                break

            line = line.replace(old, new)
        else:
            ret.append(line)

    return ''.join(ret).rstrip().replace('\n', '\n' + indent)


def generate_stub_macro(method_id, method):
    if method.return_size:

        if method.return_type[0:5] == 'const':
            return_decl = '%s _ret;' % method.return_type[6:]
        else:
            return_decl = '%s _ret;' % method.return_type

        return_stmt = 'return _ret;'
        rpc_stmt = 'rpc(_map_id, _key, _obj_version, _flag, %s, &_args, ' \
            'sizeof(_args), &_ret, sizeof(_ret));' % (method_id)
    else:
        return_decl = ''
        return_stmt = ''
        rpc_stmt = 'rpc(_map_id, _key, _obj_version, _flag, %s, &_args, ' \
            'sizeof(_args), nullptr, 0);' % (method_id)

    set_arguments = []
    arguments_size = '0'

    if len(method.arguments) > 0:
        set_arguments.append('char *_ptr = reinterpret_cast<char*>'
                             '(&_args);')

        for i, arg in enumerate(method.arguments):
            if arg.s6_ref_type is None:
                set_arguments.append('*(reinterpret_cast<%s *>(_ptr))'
                                     ' = %s;' % (arg.arg_type, arg.name))
                if i + 1 < len(method.arguments):
                    set_arguments.append('_ptr += sizeof(%s);' % arg.name)
            else:
                set_arguments.append('memcpy(_ptr, %s.serialize(), '
                                     '%s.get_serial_size());' % (arg.name, arg.name))
                if i + 1 < len(method.arguments):
                    set_arguments.append('_ptr += %s.get_serial_size();'
                                         % arg.name)

        arguments_size = ' + '.join(map(lambda x: x.size, method.arguments))

    method_flags = "0"
    for attr in method.attribute:
        method_flags += " | " + ATTR_TO_DEF_MAP[attr]

    macros = {
        'METHODNAME': method.name,
            'METHOD_CONST': method.const,
            'RETURN_TYPE': method.return_type,
            'ARGUMENTS': ', '.join(map(lambda x: x.code,
                                       method.arguments)),
            'ARGUMENTS_PASSING': ', '.join(map(lambda x: x.name,
                                               method.arguments)),
            'METHOD_ID': method_id,
            'ARGSIZE': arguments_size,
            'SET_ARGUMENTS': '\n  '.join(set_arguments),
            'RPC_STMT': rpc_stmt,
            'RETURN': return_stmt,
            'RETURN_DECL': return_decl,
            'METHOD_FLAGS': method_flags,
    }

    return macros

# MW_STUB


def generate_mwstub_methods(methods):
    ret = []
    for method_id, method in enumerate(methods):
        macros = generate_stub_macro(method_id, method)

        ret.append(replace(TEMPLATE_MW_STUB_METHOD, macros, '  '))

    return '\n\n  '.join(ret)


def generate_mwstub_classes(classes):
    ret = []
    for cls in classes:
        macros = {
            'CLASSNAME': cls.name,
                'METHODS': generate_mwstub_methods(cls.methods),
        }
        ret.append(replace(TEMPLATE_MW_STUB_CLASS, macros))

    return '\n\n'.join(ret)


def generate_mwstub(basename, classes):
    filename = 'stub.' + basename
    all_stubs_include.append(filename)
    write_to = open(os.path.join(output_dir, filename), 'w+')

    macros = {
        'MWSTUB_FILENAME_CAP': ('MWSTUB_%s' % basename.split(".")[0]).upper(),
            'FILENAME': basename,
            'SKELETON_FILENAME': 'skeleton.%s' % basename,
            'CLASSES': generate_mwstub_classes(classes)
    }

    write_to.write(replace(TEMPLATE_MW_STUB, macros))


def generate_skeleton_methods(methods, const_only):
    ret = []
    for method_id, method in enumerate(methods):
        if const_only and method.const != 'const':
            continue

        arguments = ', '.join(map(lambda x: x.name, method.arguments))
        if method.return_size:
            return_stmt = '*(static_cast<%s *>(*_ret)) = ' % method.return_type
            return_stmt += '_static_obj->%s(%s);' % (method.name, arguments)
            return_size_stmt = '*_ret_size = sizeof(%s);' % method.return_type
        else:
            return_stmt = '_static_obj->%s(%s);\n    ' % (
                method.name, arguments)
            return_stmt += '*_ret = nullptr;'
            return_size_stmt = '*_ret_size = 0;'

        set_arguments = []

        if len(method.arguments) > 0:
            for i, arg in enumerate(method.arguments):
                if arg.s6_ref_type == 'SRef':
                    set_arguments.append('%s = *SRef<%s>::'
                                         'deserialize((struct SwRefSerial *)_ptr);' %
                                         (arg.code, arg.s6_ref_type))
                    if i + 1 < len(method.arguments):
                        set_arguments.append('_ptr += %s.get_serial_size();'
                                             % arg.name)
                elif arg.s6_ref_type == 'MRef':
                    set_arguments.append('%s = *MRef<%s>::'
                                         'deserialize((struct MwRefSerial *)_ptr);' %
                                         (arg.code, arg.s6_ref_type))
                    if i + 1 < len(method.arguments):
                        set_arguments.append('_ptr += %s.get_serial_size();'
                                             % arg.name)
                else:
                    # use "auto" instead...?
                    set_arguments.append('%s = *(reinterpret_cast<%s *>'
                                         '(_ptr));' % (arg.code, arg.arg_type))
                    if i + 1 < len(method.arguments):
                        set_arguments.append('_ptr += sizeof(%s);' % arg.name)

        macros = {
            'METHODNAME': method.name,
                'METHOD_ID': method_id,
                'RETURN_TYPE': method.return_type,
                'ARGUMENTS': arguments,
                'ARGSIZE': ' + '.join(map(lambda x: x.size, method.arguments)),
                'SET_ARGUMENTS': '\n    '.join(set_arguments),
                'SET_RETURN_SIZE': return_size_stmt,
                'RETURN': return_stmt,
        }

        ret.append(replace(TEMPLATE_SKELETON_METHOD, macros, '    '))
    return '\n\n    '.join(ret)


def generate_mw_skeleton_classes(classes):
    ret = []
    for cls in classes:
        macros = {
            'CLASSNAME': cls.name,
                'METHODS': generate_skeleton_methods(cls.methods, False),
        }
        ret.append(replace(TEMPLATE_MW_SKELETON_CLASS, macros))

    return '\n\n'.join(ret)


def generate_mw_skeleton(basename, classes):
    filename = 'skeleton.' + basename
    all_stubs_include.append(filename)
    write_to = open(os.path.join(output_dir, filename), 'w+')

    macros = {
        'MW_SKELETON_FILENAME_CAP': ('MW_SKELETON_%s' %
                                     basename.split(".")[0]).upper(),
            'FILENAME': basename,
            'CLASSES': generate_mw_skeleton_classes(classes)
    }

    write_to.write(replace(TEMPLATE_MW_SKELETON, macros))

# SW_STUB


def generate_swstub_methods(methods):
    ret = []
    for method_id, method in enumerate(methods):
        macros = generate_stub_macro(method_id, method)

        if (method.const == 'const'):
            ret.append(replace(TEMPLATE_SW_STUB_CONST_METHOD, macros, '  '))
        else:
            ret.append(replace(TEMPLATE_SW_STUB_METHOD, macros, '  '))

    return '\n\n  '.join(ret)


def generate_swstub_classes(classes):
    ret = []
    for cls in classes:
        macros = {
            'CLASSNAME': cls.name,
                'METHODS': generate_swstub_methods(cls.methods),
                'EXEC_METHODS': generate_skeleton_methods(cls.methods, True),
        }
        ret.append(replace(TEMPLATE_SW_STUB_CLASS, macros))

    return '\n\n'.join(ret)


def generate_swstub(basename, classes):
    filename = 'stub.' + basename
    all_stubs_include.append(filename)
    write_to = open(os.path.join(output_dir, filename), 'w+')

    macros = {
        'SWSTUB_FILENAME_CAP': ('SWSTUB_%s' %
                                basename.split(".")[0]).upper(),
            'FILENAME': basename,
            'CLASSES': generate_swstub_classes(classes)
    }

    write_to.write(replace(TEMPLATE_SW_STUB, macros))


def generate(filename):
    classes = find_classes(filename)

    for cls in classes:
        print >> sys.stderr, cls
        all_user_classes.append((cls.name, filename, generate.base))

    print >> sys.stderr, '-' * 79

    basename = os.path.basename(filename)

    if generate.base == 'SWObject':
        generate_swstub(basename, classes)
    elif generate.base == 'MWObject':
        generate_mwstub(basename, classes)
        generate_mw_skeleton(basename, classes)


def exit_on_cmd_error():
    print >> sys.stderr, 'Usage: python %s %s [-O output_dir] ' \
        '[-I include_path] <file|directory>' % \
            (sys.argv[0], '|'.join(commands))
    sys.exit(2)


def extract_var_decls(cursor, cont):
    if cursor.kind.name == "VAR_DECL":
        cont.append(cursor)
    elif cursor.kind.name == "NAMESPACE" or \
            cursor.kind.name == "TRANSLATION_UNIT":
        for child in cursor.get_children():
            extract_var_decls(child, cont)


def search_globals(filename):
    index = Index.create()
    t_unit = index.parse(filename, parse_opts)
    cursor = t_unit.cursor
    decl_list = []
    extract_var_decls(cursor, decl_list)
    for child in decl_list:
        tokens = child.get_tokens()
        token_list = []
        for grand in tokens:
            token_list.append(grand.spelling)
        if len(token_list) == 9 and DIST_TYPE_MAP.has_key(token_list[1]) \
                and token_list[2] == "<" and token_list[6] == ">":
            data_type = token_list[1]
            key_type = token_list[3]
            class_type = token_list[5]
            var_name = child.spelling

            entry = (class_type, key_type, data_type)
            if all_global_maps.has_key(var_name):
                # print >> sys.stderr, "[WARNING] Duplicated definitions for ' \
                #        '%s<%s, %s> %s" % \
                #        (data_type, key_type, class_type, var_name)
                if entry != all_global_maps[var_name]:
                    print >> sys.stderr, "[ERROR] Type mismatch: %s and %s" % \
                        (str(entry), str(all_global_maps[var_name]))
                    sys.exit(1)
                    continue
            else:
                all_global_maps[var_name] = entry
        # else:
        #    for item in token_list:
        #        for key in DIST_TYPE_MAP.keys():
        #            if key in item:
        #                print >> sys.stderr, "[WARNING] %s seems to be ' \
        #        'processed as global dist_*, but not processed" % \
        #        str(child.location)


def analyze_user_code(command):
    # Working for Key directory
    print keys_dirs
    for keys_dir in keys_dirs:
        if not os.path.exists(keys_dir) or not os.path.isdir(keys_dir):
            print >> sys.stderr, 'Invalid directory "%s"' % keys_dir

        filenames = glob.glob(os.path.join(keys_dir, '**.hh'))
        if len(filenames) == 0:
            print >> sys.stderr, 'No C++ header file (*.hh) found in "%s"' % \
                filenames
            exit_on_cmd_error()

        for filename in filenames:
            print >> sys.stderr, '>>> Processing %s' % filename
            globals()[command](filename)

    # Working for Object directory
    for objects_path in objects_dirs:
        if not os.path.exists(objects_path):
            print >> sys.stderr, 'Invalid directory "%s"' % objects_path
            exit_on_cmd_error()

        if os.path.isdir(objects_path):
            # search subdirectories recursively
            filenames = glob.glob(os.path.join(objects_path, '**.hh'))
            if len(filenames) == 0:
                print >> sys.stderr, 'No C++ header file (*.hh) found in "%s"' % \
                    objects_path

            for filename in filenames:
                # skip generated classes (stubs and skeletons)
                basename = os.path.basename(filename)
                if fnmatch.fnmatch(basename, 'stub.*'):
                    continue
                if fnmatch.fnmatch(basename, 'skeleton.*'):
                    continue

                print >> sys.stderr, '>>> Processing %s' % filename
                globals()[command](filename)
        else:
            filename = objects_path
            globals()[command](filename)

    # Working for Application directory
    for apps_dir in apps_dirs:
        if not os.path.exists(apps_dir) or not os.path.isdir(apps_dir):
            print >> sys.stderr, 'Invalid directory "%s"' % apps_dir
            exit_on_cmd_error()

        # search subdirectories recursively
        filenames = glob.glob(os.path.join(apps_dir, '**.cpp'))
        if len(filenames) == 0:
            print >> sys.stderr, 'No C++ source file (*.cpp) ' \
                'found in "%s"' % apps_dir
            exit_on_cmd_error()

        for filename in filenames:
            print >> sys.stderr, '>>> Searching global objects in %s' % \
                filename
            search_globals(filename)


def _find_from_all_user_classes(data_type):
    for (class_name, include_file, base_class) in all_user_classes:
        if class_name == data_type:
            return base_class
    return None


def validate_global_maps():
    for (var_name, (class_type, key_type, data_type)) in \
            all_global_maps.items():
        base_class = _find_from_all_user_classes(class_type)
        if base_class == None:
            print >> sys.stderr, 'base class for "%s" is None' % class_type
            return False
        else:
            if not ((base_class == 'MWObject' and data_type == 'MwMap') or
                    (base_class == 'SWObject' and data_type == 'SwMap')):
                print >> sys.stderr, '%s is not consistent: ' \
                    'MapType - %s ObjType - %s' % \
                    (var_name, data_type, base_class)
                return False
    return True


def generate_code():
    if not validate_global_maps():
        sys.exit(1)

    # Create object-related lists
    key_registration_list = ""
    map_instantiation_list = ""

    class_index_dict = {}
    counter = 0
    for (class_name, include_file, base_class) in all_user_classes:
        class_index_dict[class_name] = counter
        counter = counter + 1

    counter = 0
    for (var_name, (class_type, key_type, data_type)) in \
            all_global_maps.items():
        map_instantiation_list += "\n" + '%s<%s, %s> %s("%s");' % \
            (data_type, key_type, class_type, var_name, var_name)

        # user_index = class_index_dict[class_type]
        #(class_name, include_file, base_class) = all_user_classes[user_index]

        counter = counter + 1

    for (class_name, parent_class_name, filename) in all_key_classes:
        key_registration_list += "\n" + "REGISTER_KEY(%s, %s::unserialize);" % \
            (class_name, class_name)

    # Define macros to fill in templates
    macros = {
        'KEY_REGISTRATION': key_registration_list,
        'MAP_INSTANTIATIONS': map_instantiation_list,
    }

    # Generate source code from templates
    with open(os.path.join(output_dir, 'all_stub_headers.hh'), 'w+') as out:
        out.write('#ifndef _S6STUB_ALL_STUBS_HH_\n')
        out.write('#define _S6STUB_ALL_STUBS_HH_\n')
        for i in all_stubs_include:
            out.write('#include \"' + i + '\"\n')
        out.write('#endif\n')
    with open(os.path.join(output_dir, 'all_key_headers.hh'), 'w+') as out:
        out.write('#ifndef _S6STUB_ALL_KEYS_HH_\n')
        out.write('#define _S6STUB_ALL_KEYS_HH_\n')
        for i in all_keys_include:
            out.write('#include \"' + i + '\"\n')
        out.write('#endif\n')
    with open(os.path.join(output_dir, 'adt_info.cpp'), 'w+') as out:
        out.write(replace(TEMPLATE_ADT_INFO_SOURCE, macros))


def main():
    def next_sys_argv():
        next_sys_argv.idx += 1
        if next_sys_argv.idx >= len(sys.argv):
            return None
        return sys.argv[next_sys_argv.idx]
    next_sys_argv.idx = 0

    command = next_sys_argv()

    if command not in commands:
        print >> sys.stderr, 'Unknown command "%s"' % command
        exit_on_cmd_error()

    argv = next_sys_argv()
    while argv and argv[0] == '-':
        if argv[1] == 'a':
            apps_dirs.append(next_sys_argv())
        elif argv[1] == 'i':
            parse_opts.append('-I' + next_sys_argv())
        elif argv[1] == 'k':
            keys_dirs.append(next_sys_argv())
        elif argv[1] == 'o':
            objects_dirs.append(next_sys_argv())
        elif argv[1] == 'O':
            global output_dir
            output_dir = next_sys_argv()
            if not os.path.exists(output_dir):
                os.mkdir(output_dir)

        argv = next_sys_argv()

    analyze_user_code(command)

    if command == 'generate':
        generate_code()

if __name__ == '__main__':
    main()
