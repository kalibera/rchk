
This project consists of several bug-finding tools that look for memory
protection errors in C source code using R API, that is in the source code
of [R](http://www.r-project.org/) itself and packages.  About 200-300 memory
protection bugs have been found using rchk and fixed in R. rchk is
now regularly used to check
[CRAN packages](https://github.com/kalibera/cran-checks/tree/master/rchk).

rchk depends on LLVM and CLANG.  R and packages to check are built with
CLANG, producing executable code and also LLVM bitcode, then linking the
LLVM bitcode for the R executable and shared libraries of packages.  In the
past, installation of LLVM for this had to be done manually and required a
number of steps.  At that point I've created scripts to automatically
install into a virtual machine (using chef, initially into virtualbox and
then also to docker; B.  W.  Lewis provided support for singularity). 
Today, however, LLVM support in Linux distributions is much better and one
can use Debian, Ubuntu or Fedora packaging system to install all required
LLVM components.  Only WLLVM scripts (python) are installed using `pip`. 
Today, native installation can be recommended.  The best tested distribution
is Ubuntu, on which rchk is run regularly, but below are instructions also
for Debian and Fedora (all tested in clean docker images of these systems).

## Ubuntu 18.04 (Bionic Beaver)

These instructions are for LLVM 4.

0. Install build dependencies for R:
	* enable source repositories in `/etc/apt/sources.list`
	* `apt-get update`
	* `apt-get build-dep -y r-base-dev`
1. Install clang and llvm:
	* `apt-get install clang-4.0 llvm-4.0-dev clang\+\+-4.0 llvm-4.0 libllvm4.0 libc\+\+-dev libc\+\+abi-dev`
2. Install [WLLVM scripts](https://github.com/travitch/whole-program-llvm):
	* `apt-get install python-pip`
	* `pip install wllvm`
3. Install [rchk](https://github.com/kalibera/rchk.git):
	* `apt-get install git`
	* `git clone https://github.com/kalibera/rchk.git`
	* `cd rchk/src ; env LLVM=/usr/lib/llvm-4.0 make ; cd ..`
	* customize `scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM
	would be `/usr/lib/llvm-4.0`, WLLVM would be `/usr/local/bin`, RCHK would be the
	path to rchk directory created by git in step (8).

LLVM 4 is now the version best tested with rchk, but one can also use LLVM
6, which is the Ubuntu default.  To use rchk with LLVM6, modify the steps
above as follows:

* Install clang and llvm:
	* `apt-get install clang llvm-dev clang\+\+ llvm libllvm libc\+\+-dev libc\+\+abi-dev`
* Install rchk:
	* `cd rchk/src ; make ; cd ..`
	* customize `scripts/config.inc` to include export LLVM=/usr`

## Debian 9 (Stretch)

These instructions are for LLVM 3.8.

0. Install build dependencies for R:
	* enable source repositories in `/etc/apt/sources.list`
	* `apt-get update`
	* `apt-get build-dep -y r-base-dev`
1. Install clang and llvm:
	* `apt-get install clang-3.8 llvm-3.8-dev clang\+\+-3.8 llvm-3.8 libllvm3.8 libc\+\+-dev libc\+\+abi-dev`
2. Install [WLLVM scripts](https://github.com/travitch/whole-program-llvm):
	* `apt-get install python-pip`
	* `pip install wllvm`
3. Install [rchk](https://github.com/kalibera/rchk.git):
	* `apt-get install git`
	* `git clone https://github.com/kalibera/rchk.git`
	* `cd rchk/src ; env LLVM=/usr/lib/llvm-3.8 make ; cd ..`
	* customize `scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM
	would be `/usr/lib/llvm-3.8`, WLLVM would be `/usr/local/bin`, RCHK would be the
	path to rchk directory created by git in step (8).

## Fedora 28

These instructions are for LLVM 6.

0. Install development tools and build dependencies for R:
	* `yum install yum-utils`
	* `yum-builddep R`
	* `yum install redhat-rpm-config hostname java-1.8.0-openjdk-devel`
	* `yum groupinstall "Development Tools"`
	* `yum groupinstall "C Development Tools and Libraries"`
1. Install clang and llvm:
	* `yum install llvm llvm-devel clang`
2. Install [WLLVM scripts](https://github.com/travitch/whole-program-llvm):
	* `pip install wllvm`
3. Install [rchk](https://github.com/kalibera/rchk.git):
	* `yum install git`
	* `git clone https://github.com/kalibera/rchk.git`
	* `cd rchk/src ;  make ; cd ..`
	* customize `scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM
	would be `/usr`, WLLVM would be `/usr/bin`, RCHK would be the
	path to rchk directory created by git in step (8).


## Testing the installation

The installation can be tested by checking a package.  For that one also
needs to install `subversion`, `rsync`, and any dependencies needed by that
package.

1. Build R producing also LLVM bitcode
	* `svn checkout https://svn.r-project.org/R/trunk`
	* `cd trunk`
	* `. ../scripts/config.inc`
	* `. ../scripts/cmpconfig.inc`
	* `../scripts/build_r.sh`
2. Install and check the package
	* `echo 'install.packages("jpeg",repos="http://cloud.r-project.org")' |  ./bin/R --slave`
	* `../scripts/check_package.sh jpeg`

The output of the checking is in files
`packages/lib/jpeg/libs/jpeg.so.*check`. For version 0.1-8 of the package,
`jpeg.so.maacheck` includes

```
WARNING Suspicious call (two or more unprotected arguments) to Rf_setAttrib at read_jpeg /rchk/trunk/packages/build/IsnsJjDm/jpeg/src/read.c:131
```

which is a true error. `bcheck` does not find any errors, `jpeg.so.bcheck`
only contains something like

```
Analyzed 15 functions, traversed 1938 states.
```

## Automated installation

One can use a pre-installed virtual machine with rchk (or, more precisely,
use an automated script that installs such machine without user
intervention, into virtualbox or docker).  Also, one can install into a
singularity container ([instructions](image/README_SINGULARITY.md) and
configuration contributed by B. W. Lewis, not tested nor maintained by
rchk author).

One can install automatically into a VirtualBox image (this will now use
LLVM 4.0 and Ubuntu 16.04.2). The image uses a fixed version of rchk that I
tested last, so it will not have the latest fixes on the master branch.

1. Install (manually) [VirtualBox](https://www.virtualbox.org/wiki/Downloads), e.g. `apt-get install virtualbox`
2. Install (manually) [Vagrant](https://www.vagrantup.com/), e.g. `apt-get install vagrant`
3. Install (automatically) R build dependencies, LLVM, WLLVM and rchk: run `vagrant up` in `image` directory

Instead of virtualbox, one can also use docker, so the tool can run inside a
container.  To install the tool into a docker container, run `vagrant up
--provider docker`. The automatic installation a fixed version of rchk
to reduce the risk of breakage, but this also means it does not immediately
get latest updates from the trunk.

Note that the automated installation may take long, as it will be
downloading an Ubuntu 16.04.2 image and installing the R build dependencies
to a fresh Ubuntu image. Should the installation fail or time out, it can
be re-started by `vagrant provision`. One can log in to the machine by
`vagrant ssh` to use the tools after successful install or to fix issues
should the installation fail. Note that a recent version of vagrant is
needed, e.g. on Ubuntu 14.04 one can install the `.DEB` package of vagrant
available from the [vagrant website](https://www.vagrantup.com/downloads.html)
using `dpkg -i`. At least since Ubuntu 16.04, one can use the distribution
version of vagrant.

## Installing on older systems

rchk is likely to work on older Linux systems as long as LLVM 4 is used (or
3.8, 5 or 6).  In particular, I've been using rchk on many earlier versions
of Ubuntu, so Ubuntu and Debian are likely to be easiest to use.  An older
version of rchk working with LLVM 3.8 is on the `llvm-38` branch and for
LLVM 3.6 on the `llvm-36` branch, but those branches are no longer updated
and their use is not recommended.  LLVM 4 is not that new by now, so it is
widely supported. Problems on Gentoo have been reported (rchk crashes
observed and it was not clear why). 

Further information:

* [User documentation](doc/USAGE.md) - how to use the tools and what they check.
* [Internals](doc/INTERNALS.md) - how the tools work internally.
* [Building](doc/BUILDING.md) - how to get the necessary bitcode files for R/packages; this is now encapsulated in scripts, but the background is here
