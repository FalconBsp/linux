#! /usr/bin/python
# -*- python -*-
######################################################################
# setconfig.py
#
# see setconfig.py --help for usage

import os, sys, re, fileinput
from optparse import OptionParser

######################################################################
# constants

# e.g. 'CONFIG_NAME = y' , 'CONFIG_NAME+="str str str"'
set_pattern = re.compile(r'^CONFIG_(?P<name>\w+)\s*(?P<op>\+?=)\s*(?P<val>.+$)')
# e.g. '# CONFIG_NAME is not set'
unset_pattern = re.compile(r'^# CONFIG_(?P<name>\w+) (?P<op>)(?P<val>is not set)')

######################################################################
# struct ChangeSpec

class ChangeSpec:
	def __init__(self, name, op, val, orig_str):
		self.name = name
		self.op = op
		self.val = val
		self.orig_str = orig_str
		self.done = False
        def str(self):
		print ("ChangeSpec(%s, %s, %s, %s, %s)"
		       % (self.name, self.op, self.val,
			  self.orig_str, self.done))

######################################################################
# subroutines

def config_val(op, name, old_val, new_val):
	config = 'CONFIG_' + name
	if op=="y" or op=="m" or op=="1" or op=="s":
		return "%s=%s\n" % (config, new_val)
	if op=="n":
		return "# %s is not set\n" % (config)
	if op=="s+":
		# trim the quotes
		old_str = old_val.strip()[1:-1]
		new_str = new_val.strip()[1:-1]

		# append the old string and new string
		return '%s="%s%s"\n' % (config, old_str, new_str)
	assert False, "unexpected op: " + op

def do_conv_config(infile, change_specs):
	for orig_line in infile:
		line = orig_line.strip()
		tuple = split_config_line(line)
		if not tuple:
			if line == '' or line[0] == '#':
				sys.stdout.write(orig_line)
				continue
			die("invalid config line: " + line)
		(name, eq, old_val) = tuple
		found = False
		for cs in change_specs:
			if name == cs.name:
				new_val = config_val(cs.op, name,
						     old_val, cs.val)
				sys.stdout.write(new_val)
				cs.done = True
				found = True
		if not found:
			sys.stdout.write(orig_line)

def do_append_config(outfile_path, change_specs):
	outfile = open(outfile_path, 'a')
        for cs in change_specs:
		if cs.done:
			continue
		if cs.op == "s+":
			die("missing CONFIG_%s, cannot append value: %s"
			    % (cs.name, cs.orig_str))
		new_val = config_val(cs.op, cs.name, None, cs.val)
		outfile.write(new_val)

def is_comment(line):
	line = line.strip()
	if not line:
		return True
	# '# ... is not set' is not a comment
	if unset_pattern.search(line):
		return False
	if line[0] == '#':
		return True
	return False

def read_change_spec_file(file):
	ans = []
	for line in file:
		line = line.strip()
		if is_comment(line):
			continue
		ans.append(line)
	return ans

def split_config_line(line):
	match = set_pattern.search(line) or unset_pattern.search(line)
	if match:
		return match.group('name', 'op', 'val')
	else:
		return None

def arg_to_change_spec(str):
	tuple = split_config_line(str)
	if not tuple:
		die("invalid change spec: " + str)
	(name, eq, val) = tuple
	if val == 'is not set':
		op = 'n'
	else:
		op = val[0]
	if eq == '+=':
		if op != '"':
			die("can't append non-string: " + str)
		op = 's+'
	elif op == '"':
		op = 's'
	op = op.lower()
	if op >= '0' and op <= '9':
		op = '1'
	# check op for legal value:
	if op not in ('y','n','m','s','s+','1'):
		die("unsupported change spec value: " + str)
	return ChangeSpec(name, op, val, str)

def die(msg):
    sys.exit("%s: %s" % (sys.argv[0], msg))

def do_option(kbuild):
	usage = '''%prog [options] [change_spec]...
       %prog [options] -f change_spec_file

change_specs are:
   CONFIG_<name>=y
   CONFIG_<name>=m
   CONFIG_<name>=n
   # CONFIG_<name> is not set
   CONFIG_<name>=123
   CONFIG_<name>="str"
   CONFIG_<name>+="str"
   (Note: "" should be escaped when passed from shell.)'''

	desc = '''Modify kernel configuration (default: .config) as
specified by arguments or change_spec_file.
'''
	parser = OptionParser(usage=usage, description=desc)
	parser.set_defaults(orig_config = kbuild + '/.config',
			    inplace_flag = 1)
	parser.add_option('--change-spec-file', '-f', dest='change_spec_file',
			  help="change spec file ('-' means stdin)")
	parser.add_option('--orig-config', dest='orig_config',
			  help="original configuration file")
	parser.add_option('--inplace', dest='backup_ext',
			  help="overwrite original configuration file. "
			       "(default: --inplace=.bak)")
	parser.add_option('--no-inplace', dest='inplace_flag',
			  action='store_const', const=None,
			  help='no overwrite.  output to stdout')
	return parser.parse_args()

######################################################################
# main function
def main():
	kbuild = os.environ.get('KBUILD_OUTPUT', '.')
	kbuild = kbuild.rstrip('/')

	(options, args) = do_option(kbuild)

	if options.backup_ext:
		if options.inplace_flag == None:
			die("both --inplace and --no_inplace specified")
		bak = options.backup_ext
	else:
		bak = '.bak'

	orig_config_file = fileinput.input([options.orig_config],
					   inplace=options.inplace_flag,
					   backup=bak)

	if options.change_spec_file:
		if options.change_spec_file == '-':
			infile = sys.stdin
		else:
			infile = open(options.change_spec_file)
		args = read_change_spec_file(infile) + args
	change_specs = [ arg_to_change_spec(arg) for arg in args ]
	do_conv_config(orig_config_file, change_specs)
	orig_config_file.close()
	do_append_config(options.orig_config, change_specs)

######################################################################
if __name__=="__main__":
	main()
##### end of setconfig
