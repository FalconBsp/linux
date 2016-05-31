#!/usr/bin/env python

import signal, sys, os, shutil, time
from optparse import OptionParser

######################################################################
# main
def main():
    kdir = os.environ.get('KBUILD_OUTPUT', '.')
    kdir = kdir.rstrip('/')

    (setup_options, config_options) = split_args(sys.argv[1:])
    (options, args) = do_option(setup_options)

    # --gen-config: do .config creation only
    #               copy defconfig and run config-modifiers
    if options.gen_config == 'gen':
        new_recipe(kdir, options.defconfig_base, config_options)
        copy_defconfig(kdir, options.defconfig_base)
        mod_config(kdir, options.defconfig_base, config_options)
        copy_result(kdir)
        return

    # --mod-config: do .config creation only
    #               run config-modifiers (no defconfig copy)
    if options.gen_config == 'mod':
        append_recipe(kdir, config_options)
        mod_config(kdir, options.defconfig_base, config_options)
        copy_result(kdir)
        return

    # --redo-config: do .config creation only
    #                redo according to .config_recipe
    if options.gen_config == 'redo':
        redo_config(kdir)
        return

    print "setup for %s for cross target" % options.full_name

    # Setup the environment.
    echo(options.cross_compile, kdir + "/.cross_compile")
    echo(options.arch_name    , kdir + "/.arch_name")
    echo(options.target_name  , kdir + "/.target_name")

    # Setup kernel header fixup script.
    if options.fixup_kernel_headers != None:
        shutil.copyfile(options.fixup_kernel_headers, kdir + "/.fixup_kernel_headers")

    # Setup the configration.
    new_recipe(kdir, options.defconfig_base, config_options)
    copy_defconfig(kdir, options.defconfig_base)
    mod_config(kdir, options.defconfig_base, config_options)
    copy_result(kdir)

    print "run '%s' to produce %s/%s" % (options.build_instruction,
                                         kdir, options.image)

######################################################################
# subs
#
def do_option(in_args):
    usage = '''usage: setup-BOARD [options] [config-modifiers]...

  setup-BOARD copies board-specific defconfig to .config, runs
  config-modifiers, and creates files needed for cross-compilation.

possible config-modifiers:
   NAME : if scripts/config-modifiers/NAME.cm exists, run it.
          otherwise if <defconfig_path>.NAME exists, copy it to .config
   --- SETCONFIG_ARGS... ---
          : run setconfig with specified args

For example,

   setup-BOARD patch-test --- CONFIG_NLS=n --- ltt_disable

copies BOARD_defconfig.patch-test to .config, change CONFIG_NLS to n
using setconfig, then run scripts/config-modifiers/ltt_disable.cm.  Note
that BOARD_defconfig is first copied to .config implicitly, then
overwritten by patch-test defconfig.'''

    parser = OptionParser(usage)
    parser.add_option("--gen-config", dest="gen_config",
                                 action="store_const", const="gen",
                                 help="skip generating files other than .config")
    parser.add_option("--mod-config", dest="gen_config",
                                 action="store_const", const="mod",
                                 help="run config-modifiers only "
                                      "(not preceeded by copying defconfig)")
    parser.add_option("--redo-config", dest="gen_config",
                                 action="store_const", const="redo",
                                 help="redo .config creation from .config_recipe")
    parser.add_option("--full-name", dest="full_name",
                                     help="full name of target (internal use)")
    parser.add_option("--cross-compile", dest="cross_compile",
                                     help="compiler prefix (internal use)")
    parser.add_option("--arch-name", dest="arch_name",
                                     help="architecture name (internal use)")
    parser.add_option("--target-name", dest="target_name",
                                       help="target name (internal use)")
    parser.add_option("--fixup-kernel-headers", dest="fixup_kernel_headers",
                      help="script for preparing target specific headers (internal use)")
    parser.add_option("--defconfig-base", dest="defconfig_base",
                                          help="basename for defconfig (internal use)")
    parser.add_option("--build-instruction", dest="build_instruction",
                                     help="command line to build the kernel (internal use)")
    parser.add_option("--image", dest="image",
                                 help="path for resulting kernel image (internal use)")
    (options, args) = parser.parse_args(in_args)

    if options.full_name == None:
        die("--full-name not specified")
    if options.cross_compile == None:
        die("--cross-compile not specified")
    if options.arch_name == None:
        die("--arch-name not specified")
    if options.target_name == None:
        die("--target-name not specified")
    if options.defconfig_base == None:
        die("--defconfig-base not specified")
    if options.build_instruction == None:
        die("--build-instruction not specified")
    if options.image == None:
        die("--image not specified")

    return (options, args)

def echo(str, path):
    file = open(path, 'w')
    file.write(str)
    file.write('\n')
    file.close()

def die(msg):
    sys.exit("%s: %s" % (sys.argv[0], msg))

def run(path, *args):
    vec = (path,) + args
    ret = os.spawnv(os.P_WAIT, path, vec)
    if ret != 0:
        die("spawn failed(%d): %s" %(ret,  ' '.join(vec)))

def gen_date_comment():
    return "# " + time.strftime("%Y-%m-%dT%H:%M")

def split_args(args):
    setup_options = []
    option_modifier_separators = ['---']

    while args:
        arg = args[0]
        if (not arg.startswith('-')
            or arg in option_modifier_separators):
            return (setup_options, args)
        if arg == '--':
            return (setup_options, args[1:])
        setup_options.append(arg)
        del args[0]
    return (setup_options, args)

def new_recipe(kdir, defconfig_base, config_options):
    outfile = open(kdir + "/.config_recipe", "w")
    print >>outfile, gen_date_comment()
    print >>outfile, defconfig_base
    for opt in config_options:
        print >>outfile, opt
    outfile.close()

def append_recipe(kdir, config_options):
    outfile = open(kdir + "/.config_recipe", "a")
    print >>outfile, gen_date_comment()
    for opt in config_options:
        print >>outfile, opt
    outfile.close()

def copy_defconfig(kdir, defconfig_base):
    shutil.copyfile(defconfig_base, kdir + "/.config")

def group_options(config_options):
    in_setconfig = False
    setconfig_args = []
    for opt in config_options:
        if opt == '---':
            if in_setconfig:
                yield ('setconfig', setconfig_args)
                in_setconfig = False
            else:
                in_setconfig = True
                setconfig_args = []
        else:
            if in_setconfig:
                setconfig_args.append(opt)
            else:
                yield ('name', opt)
    if setconfig_args:
        yield ('setconfig', setconfig_args)


def mod_config(kdir, defconfig_base, config_options):
    for (kind, val) in group_options(config_options):
        if kind == 'setconfig':
            run('scripts/setconfig.py', *val)
        elif kind == 'name':
            if val.startswith('#'):
                continue
            script_name = os.getcwd() + '/scripts/config-modifiers/' + val + '.cm'
            defconfig_name = defconfig_base + '.' + val
            if os.path.exists(script_name):
                run(script_name, kdir, defconfig_base)
            elif os.path.exists(defconfig_name):
                shutil.copyfile(defconfig_name, kdir + "/.config")
            else:
                die("no such .cm file or defconfig: " + val)
        else:
            assert False, "invalid kind"

def redo_config(kdir):
    infile = open(kdir + '/.config_recipe')
    lines = []
    for line in infile.readlines():
        line = line.strip()
        if line == '' or line.startswith('#'):
            continue
        lines.append(line)
    if not lines:
        die("empty .config_recipe")
    defconfig_base = lines.pop(0)
    copy_defconfig(kdir, defconfig_base)
    mod_config(kdir, defconfig_base, lines)
    copy_result(kdir)

def copy_result(kdir):
    shutil.copyfile(kdir + "/.config",
                    kdir + "/.config_copy")

######################################################################
# call main
if __name__ == "__main__":
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    main()
