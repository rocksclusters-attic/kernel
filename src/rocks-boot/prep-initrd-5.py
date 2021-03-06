#!/opt/rocks/bin/python
#
# $Id: prep-initrd-5.py,v 1.4 2012/11/27 00:49:05 phil Exp $
#
# @Copyright@
# 
# 				Rocks(r)
# 		         www.rocksclusters.org
# 		         version 5.6 (Emerald Boa)
# 		         version 6.1 (Emerald Boa)
# 
# Copyright (c) 2000 - 2013 The Regents of the University of California.
# All rights reserved.	
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
# 
# 1. Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
# 
# 2. Redistributions in binary form must reproduce the above copyright
# notice unmodified and in its entirety, this list of conditions and the
# following disclaimer in the documentation and/or other materials provided 
# with the distribution.
# 
# 3. All advertising and press materials, printed or electronic, mentioning
# features or use of this software must display the following acknowledgement: 
# 
# 	"This product includes software developed by the Rocks(r)
# 	Cluster Group at the San Diego Supercomputer Center at the
# 	University of California, San Diego and its contributors."
# 
# 4. Except as permitted for the purposes of acknowledgment in paragraph 3,
# neither the name or logo of this software nor the names of its
# authors may be used to endorse or promote products derived from this
# software without specific prior written permission.  The name of the
# software includes the following terms, and any derivatives thereof:
# "Rocks", "Rocks Clusters", and "Avalanche Installer".  For licensing of 
# the associated name, interested parties should contact Technology 
# Transfer & Intellectual Property Services, University of California, 
# San Diego, 9500 Gilman Drive, Mail Code 0910, La Jolla, CA 92093-0910, 
# Ph: (858) 534-5815, FAX: (858) 534-7345, E-MAIL:invent@ucsd.edu
# 
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS''
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
# BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
# IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# 
# @Copyright@
#
# $Log: prep-initrd-5.py,v $
# Revision 1.4  2012/11/27 00:49:05  phil
# Copyright Storm for Emerald Boa
#
# Revision 1.3  2012/05/06 05:49:14  phil
# Copyright Storm for Mamba
#
# Revision 1.2  2012/04/05 22:02:27  phil
# Don't create md5sums when creating the local distro
#
# Revision 1.1  2012/01/23 20:49:32  phil
# Support for build under 5 or 6
#
# Revision 1.37  2011/07/23 02:31:14  phil
# Viper Copyright
#
# Revision 1.36  2010/11/03 16:23:14  bruno
# nuke code that gets older kernels for installation
#
# Revision 1.35  2010/11/03 16:21:33  bruno
# don't include older kernels for the installation environment
#
# Revision 1.34  2010/10/22 22:43:50  bruno
# cleanup the installation kernels
#
# Revision 1.33  2010/10/05 23:30:11  bruno
# still need to bring in the old kernels
#
# Revision 1.32  2010/09/23 19:37:50  bruno
# it looks like kernel version 2.6.18-194.11.4.el5 no longer panics during
# installation on rocks 5.4, so let's disable the code that downloads known-good
# kernel RPMS.
#
# Revision 1.31  2010/09/09 21:40:48  bruno
# new
#
# Revision 1.30  2010/09/07 23:53:23  bruno
# star power for gb
#
# Revision 1.29  2010/08/26 01:10:49  bruno
# tweak for the beta -- ensure the 164 version of the kernel are used for
# install
#
# Revision 1.28  2010/02/25 05:45:48  bruno
# makin' progress
#
# Revision 1.27  2009/10/03 00:19:40  bruno
# can build compute nodes with RHEL 5.4
#
# Revision 1.26  2009/08/28 21:49:53  bruno
# the start of the "most scalable installer in the universe!"
#
# Revision 1.25  2009/05/01 19:07:20  mjk
# chimi con queso
#
# Revision 1.24  2008/10/18 00:56:12  mjk
# copyright 5.1
#
# Revision 1.23  2008/05/22 21:02:07  bruno
# rocks-dist is dead!
#
# moved default location of distro from /export/home/install to
# /export/rocks/install
#
# Revision 1.22  2008/04/17 21:59:17  bruno
# tweaks to build driver disks on V
#
# Revision 1.21  2008/03/06 23:41:56  mjk
# copyright storm on
#
# Revision 1.20  2007/08/16 00:31:18  bruno
# package up a xen installation kernel and initrd
#
# Revision 1.19  2007/06/23 04:03:49  mjk
# mars hill copyright
#
# Revision 1.18  2006/09/11 22:49:12  mjk
# monkey face copyright
#
# Revision 1.17  2006/08/10 00:11:15  mjk
# 4.2 copyright
#
# Revision 1.16  2006/06/05 17:57:40  bruno
# first steps towards 4.2 beta
#
# Revision 1.15  2006/01/16 06:49:12  mjk
# fix python path for source built foundation python
#
# Revision 1.14  2005/12/21 00:22:02  bruno
# support for adding device drivers to initrd.img
#
# Revision 1.13  2005/10/12 18:10:02  mjk
# final copyright for 4.1
#
# Revision 1.12  2005/09/22 22:45:26  bruno
# correctly pull the mini_httpd package from the local roll on i386
#
# Revision 1.11  2005/09/16 01:03:39  mjk
# updated copyright
#
# Revision 1.10  2005/09/15 18:21:10  bruno
# don't run the post scripts when mini_httpd is installed in initrd.img
#
# Revision 1.9  2005/09/02 00:13:38  bruno
# when installing the kernel package locally, don't run the scripts in the
# RPM
#
# Revision 1.8  2005/08/19 05:45:16  bruno
# update for new rocks-dist that handles making torrent files
#
# Revision 1.7  2005/07/27 01:54:40  bruno
# checkpoint
#
# Revision 1.6  2005/06/30 19:15:36  bruno
# patch netstg2.img in kernel roll, not with rocks-dist
#
# Revision 1.5  2005/05/24 21:23:02  mjk
# update copyright, release is not any closer
#
# Revision 1.4  2005/05/24 05:23:39  bruno
# touch up for new rocks-roll building method
#
# Revision 1.3  2005/03/29 02:45:18  bruno
# tweaks for x86_64
#
# Revision 1.2  2005/03/03 00:08:17  bruno
# for RHEL 4
#
# Revision 1.1  2004/11/23 02:41:13  bruno
# made the kernel roll bootable.
#
# moved 'rocks-boot' here -- it is now uses vmlinuz and builds the initrd.img
# file from the local (if present in the local RPMS directory) or from the
# current distro.
#
# if you want to use a specific kernel, just drop it in the RPMS directory.
#
# Revision 1.25  2004/09/28 18:47:46  bruno
# increase the boot.img size for itanium
#
# Revision 1.24  2004/09/02 02:05:20  bruno
# more changes for x86_64/ia32e release
#
# Revision 1.23  2004/08/30 21:44:58  bruno
# i386 now uses kernel package
#
# all arches now incorporate the kernel-unsupported modules (bug 31) -- this
# should increase the number of hardware devices that the installer supports
#
# Revision 1.22  2004/07/27 17:51:06  fds
# Done in base class.
#
# Revision 1.21  2004/07/21 22:56:08  fds
# Need to point to the lan/ distro.
#
# Revision 1.20  2004/03/25 03:15:15  bruno
# touch 'em all!
#
# update version numbers to 3.2.0 and update copyrights
#
# Revision 1.19  2004/02/09 23:13:24  fds
# Using project name from rocksrc for buildinstall.
#
# Revision 1.18  2004/01/29 23:43:12  fds
# Using the rocksrc info to set version in buildinstall
#
# Revision 1.17  2004/01/16 02:38:15  fds
# Changed behavior slightly to make new buildinstall happy.
#
# Revision 1.16  2003/11/13 05:02:59  bruno
# added changes in order to support ia64
#
# Revision 1.15  2003/11/04 19:17:01  bruno
# added rocks-boot-netstage package
#
# Revision 1.14  2003/11/03 21:43:33  bruno
# no more quotes around Rocks -- mjk can "blow me"
#
# Revision 1.13  2003/10/30 22:57:00  bruno
# updated for RHEL 3
#
# Revision 1.12  2003/10/23 18:44:07  fds
# Uses kickstart.applyRPM logic.
#
# Revision 1.11  2003/10/16 20:28:55  fds
# Small fixes
#
# Revision 1.10  2003/10/16 20:08:13  fds
# This logic is now in rocks.kickstart.Application
#
# Revision 1.9  2003/10/08 23:07:17  bruno
# closer to taroon
#
# Revision 1.8  2003/10/08 18:12:13  fds
# Requires mini_httpd package now.
#
# Revision 1.7  2003/10/07 23:28:07  fds
# Merged in brunos changes.
#
# Revision 1.5  2003/10/02 20:12:31  fds
# Using new DistRPMList Exception structure.
#
# Revision 1.4  2003/10/01 02:11:15  bruno
# fixes for anaconda 9
#
# Revision 1.3  2003/09/28 21:56:58  fds
# Made a rocks.kickstart application.
#
# Revision 1.2  2003/09/25 23:23:06  bruno
# tweaks for 9
#
# Revision 1.1  2003/09/18 22:31:40  fds
# Opteron image. Cleaner Makefile. Common python helper scripts.
#
#

import string
import popen2
import rocks.kickstart
import os
import os.path
import sys
import rocks.bootable
from rocks.dist import DistError


class Distribution:

	def __init__(self, arch, name='rocks-dist'):
		self.arch = arch
		self.tree = None
		self.name = name

		#
		# the 'native' cpu is always first
		#
		self.cpus = []
		i86cpus = [ 'i686', 'i586', 'i486', 'i386' ]

		native = os.uname()[4]
		self.cpus.append(native)

		if native in i86cpus:
			self.cpus += i86cpus

		self.cpus.append('noarch')

	
	def getPath(self):
		return os.path.join(self.name, self.arch)
		
	def generate(self, flags=""):
		rocks.util.system('/opt/rocks/bin/rocks create distro md5=no')

		self.tree = rocks.file.Tree(os.path.join(os.getcwd(), 
			self.getPath()))
		
	def getRPMS(self):
		return self.tree.getFiles(os.path.join('RedHat', 'RPMS'))

	def getSRPMS(self):
		return self.tree.getFiles('SRPMS')
		
	def getRPM(self, name):
		l = []
		for rpm in self.getRPMS():
			try:
				if rpm.getPackageName() == name:
					l.append(rpm)
			except:
				pass
		if len(l) > 0:
			return l
		return None

	

class App(rocks.app.Application):
	"""Preps the initrd for the rocks-boot package by
	using the native kernel pakcage to build a custom
	initrd image. Uses the kickstart Application to locate 
	packages in the local distribution."""
	
	def __init__(self, argv):
		rocks.app.Application.__init__(self, argv)
		self.usage_name = 'Prep initrd'
		self.usage_version = '@VERSION@'
		

	def thinkLocally(self, name, arch):
		import rocks.file

		localtree = rocks.file.Tree(os.path.join(os.getcwd(),
			'..', '..', '..', '..', '..', '..', 'RPMS'))	

		locallist = {}
		for dir in localtree.getDirs():
			if 'CVS' in string.split(dir, os.sep):
				continue # skip CVS metadata

			for rpm in localtree.getFiles(dir):
				try:
					s = '%s-%s' % (name, arch)
					# print 's (%s), rpm (%s)' % \
						# (s, rpm.getUniqueName())
					if s == rpm.getUniqueName():
						return rpm
				except:
					pass
		

		return None


	def patchImage(self):
		#
		# patch stage2.img 
		#
		stage2_path = os.path.join(self.dist.getPath(),
					'images','stage2.img')

		self.boot.patchImage(stage2_path)

		return


	def run(self):
		print "Prep-initrd starting..."
		print "Arch:", self.getArch()

		self.dist = Distribution(self.getArch())
		self.dist.generate()

		self.boot = rocks.bootable.Bootable(self.dist)

		#
		# install some packages to this local build tree
		#
		pkgs = [ 'anaconda-runtime', 'lighttpd', 'rocks-tracker' ]

		for pkg in pkgs:
			RPM = self.thinkLocally(pkg, self.getArch())
			if not RPM:
				RPM = self.boot.getBestRPM(pkg)

			if not RPM:
				raise DistError, "Could not find %s rpm" % (pkg)

			self.boot.applyRPM(RPM, 
				os.path.join(os.getcwd(), pkg),
					flags='--noscripts --excludedocs')

		#
		# make stage2.img 
		#
		cwd = os.getcwd()
		os.chdir('anaconda-runtime/usr/lib/anaconda-runtime')
		version = "%s.%s.%s" % \
			(self.projectVersionMajor, 
			self.projectVersionMinor, 
			self.projectVersionMicro)

		# Capitalize first letter in project. Selfishly rocks biased.
		projectName = self.projectName[0].upper() + self.projectName[1:]

		cmd = './buildinstall --product "%s" ' % projectName \
			+'--prodpath "RedHat" ' \
			+ '--version "%s" ' % version \
			+ '--release "0" ' \
			+ os.path.join(cwd, self.dist.getPath())
		print "Buildinstall cmd:", cmd
		os.system(cmd)
		os.chdir(cwd)

		if self.getArch() == 'ia64':
			images = os.path.join(self.dist.getPath(), 'images')
			os.system('cp %s/boot.img .' % (images))

			os.system('mount -o loop boot.img mnt')
			os.system('cp mnt/initrd.img . ')
			os.system('umount mnt')
		else:
			cmd = 'cp -f %s initrd-boot.img' % \
				os.path.join(self.dist.getPath(),
                                'isolinux', 'initrd.img')
			rc = os.system(cmd)
			if rc != 0:
				raise DistError, "Could not find initrd. " + \
					"Did buildinstall complete?"

			cmd = 'cp -f %s initrd-xen.img' % \
				os.path.join(self.dist.getPath(),
                                'images', 'xen', 'initrd.img')
			rc = os.system(cmd)

		#
		# install the kernel RPMs. this is used when building 
		# driver disks
		#
		pkgs = [ 'kernel', 'kernel-devel', 'kernel-xen',
			'kernel-xen-devel' ]

		if self.getArch() == "i386":
			kernelarch = "i686"
			pkgs.append('kernel-PAE')
			pkgs.append('kernel-PAE-devel')
		else:
			kernelarch = self.getArch()

		for pkg in pkgs:
			RPM = self.thinkLocally(pkg, kernelarch)
			if not RPM:
				RPM = self.boot.getBestRPM(pkg)

			if not RPM:
				raise DistError, "Could not find %s rpm" % (pkg)

			self.boot.applyRPM(RPM, 
				os.path.join(os.getcwd(), 'kernels'),
					flags='--noscripts --excludedocs')

		return


# Main
app = App(sys.argv)
app.parseArgs()
app.run()

