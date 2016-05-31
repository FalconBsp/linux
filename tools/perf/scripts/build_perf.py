#!/usr/bin/python

import re
import os
import linecache
import sys
import commands
import fileinput
import string

def build_perf(arch):
	if arch == "x86":
		print "building"
		os.system("rm -f debian/rules")
                os.system("cp debian/rules_perf debian/rules")
		os.system("tar -cvzf ../perf_3.10.y.orig.tar.gz ../perf/")
		os.system("dpkg-buildpackage -uc -us")
	elif arch == "arm":
		print "building for arm"
		os.system("rm -f debian/rules")
                os.system("cp debian/rules_perf debian/rules")
		os.system("tar -cvzf ../perf_3.10.y.orig.tar.gz ../perf/")
		os.system("dpkg-buildpackage -uc -us -j32")
		os.system("mv debian/control debian/control_bk")
		s = open("debian/control_bk","r")
		s1 = open("debian/control","w")
		for line in s.readlines():
		   s1.write(line.replace("perf", "perf-mini"))
		s.close()
		s1.close()
		os.system("mv debian/changelog debian/changelog_bk")
		s2 = open("debian/changelog_bk","r")
		s3 = open("debian/changelog","w")
		for line in s2.readlines():
		   s3.write(line.replace("perf", "perf-mini"))
		s2.close()
		s3.close()
		os.system("rm -f debian/rules")
		os.system("cp debian/rules_perf-mini debian/rules")
		os.system("tar -cvzf ../perf-mini_3.10.y.orig.tar.gz ../perf/")
		os.system("dpkg-buildpackage -uc -us -j32")
		os.system("mv debian/control_bk debian/control")
                os.system("rm -f debian/control_bk")
		os.system("mv debian/changelog_bk debian/changelog")
                os.system("rm -f debian/changelog_bk")
		os.system("rm -f debian/rules")
	else:
		print "No arch"

argc = len(sys.argv)
if argc < 2:
	print "Usage ./build_perf.py x86|arm"
	sys.exit();
build_perf(sys.argv[1])
