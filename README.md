
This project consists of several bug-finding tools that look for memory
protection errors in C source code using R API, that is in the source code
of [R](http://www.r-project.org/) itself and packages.  About 200-300 memory
protection bugs have been found using rchk and fixed in R. rchk is
now regularly used to check
[CRAN packages](https://github.com/kalibera/cran-checks/tree/master/rchk).

rchk depends on LLVM.  In the past, installing LLVM was complicated to the
point that I've created scripts to automatically install into a virtual
machine (using chef, initially into virtualbox and then also to docker;
B. W. Lewis provided support for singularity). However, now the LLVM
distribution that is part of Ubuntu allows seamless installation natively,
and this is the recommended way as the number of dependencies needed is high
(to build R, to build packages).

# Ubuntu 18.04

These steps worked for me in Ubuntu 18.04:

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
	* `cd rchk/src ; env LLVM=/usr/lib/llvm-4.0 make`
	* customize `../scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM
	would be `/usr/lib/llvm-4.0`, WLLVM would be `/usr/local/bin`, RCHK would be the
	path to rchk directory created by git in step (8).

Ubuntu allows multiple versions of LLVM/CLANG to be installed at a time.
rchk now requires LLVM 4 and.

# Testing the installation by checking a package

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
`maacheck` reports

```
WARNING Suspicious call (two or more unprotected arguments) to Rf_setAttrib at read_jpeg /rchk/trunk/packages/build/IsnsJjDm/jpeg/src/read.c:131
```

which is a true error. `bcheck` does not find any errors, it only reports

```
Analyzed 15 functions, traversed 1938 states.
```

#Automated installation and installation in earlier OS versions

One can use a pre-installed virtual machine with rchk (or, more precisely,
use an automated script that installs such machine without user
intervention, into virtualbox or docker).  Also, one can install into a
singularity container ([instructions](image/README_SINGULARITY.md) and
configuration contributed by B. W. Lewis, not tested nor maintained by
rchk author).

The native (manual) installation of dependencies for the tool is still
somewhat involved, but has been working with binary releases of LLVM
packaged in Ubuntu.  Branch llvm-38 has been tested on Ubuntu 16.04.2, but
it is better to use the trunk version of rchk and LLVM 4.0 (already
available in Ubuntu 16.04.2).  rchk has also been tested with Ubuntu 17.04
and Ubuntu 17.10 with LLVM from the Ubuntu distributions.  The tool can be
used also with [LLVM binary distributions](http://llvm.org/releases/download.html).

Manual installation on Ubuntu 16.04.2:

0. Install build dependencies for R:
	1. enable source repositories in `/etc/apt/sources.list`
	2. `apt-get build-dep -y r-base-dev`
	3. `apt-get install libcurl4-openssl-dev`
1. Install clang and llvm: `apt-get install clang-4.0 llvm-4.0-dev clang\+\+-4.0 llvm-4.0 libllvm4.0 libc\+\+-dev libc\+\+abi-dev`
2. Install [WLLVM scripts](https://github.com/travitch/whole-program-llvm):
	1. `apt-get install python-pip`
	2. `pip install --upgrade pip`
	3. `pip install --user DIR` where DIR is checked-out WLLVM
3. Install [rchk](https://github.com/kalibera/rchk.git):
	1. `env LLVM=/usr/lib/llvm-4.0 make`
	2. modify script `scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM can be `/usr/lib/llvm-4.0` on Ubuntu 16.04.2

It is extremely unlikely that the `master` version of the tool will work
with any other version of LLVM than 4.0 due to frequent changes in LLVM API. 
An older version working with LLVM 3.8 is on the `llvm-38` branch and for
LLVM 3.6 on the `llvm-36` branch, but those branches are no longer updated.

Alternatively, one can install automatically into a VirtualBox image (this
will now use LLVM 4.0 and Ubuntu 16.04.2).

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

For both native and virtual installation, to check R:

4. Get latest version of R: `svn checkout https://svn.r-project.org/R/trunk`
5. Build it using for rchk (run in R source tree)
	1. `. <rchk_root>/scripts/config.inc` (`. /opt/rchk/scripts/config.inc`)
	2. `<rchkroot>/scripts/build_r.sh` (`. /opt/rchk/scripts/build_r.sh`)
6. Run default rchk tools on R: `<rchkroot>/scripts/check_r.sh` (`/opt/rchk/scripts/check_r.sh`). Look for
files with suffixes `.maacheck` and `.bcheck` under `src`, e.g. 
`src/main/R.bin.bcheck` is the result of running `bcheck` tool on the R
binary. `<rchk_root>` is `/opt/rchk` with the virtual installation.

To check a package:

1. Prepare the environment for build
	1. `. <rchk_root>/scripts/config.inc` (`. /opt/rchk/scripts/config.inc`)
	2. `. <rchk_root>/scripts/cmpconfig.inc` (`. /opt/rchk/scripts/cmpconfig.inc`)
2. Install packages from within R: `./bin/R` (use `install.packages` or
`biocLite` or any other mechanism that in the end uses `install.packages`)
3. Check all installed packages: `<rchkroot>/scripts/check_package.sh`
4. The results of the checks will appear under `packages/lib/<package_dir>`,
again look for files with suffixes `.maacheck` and `.bcheck`.

For example, to check the "curl" package in the virtual installation:

```bash
svn checkout https://svn.r-project.org/R/trunk
cd trunk
. /opt/rchk/scripts/config.inc
/opt/rchk/scripts/build_r.sh

. /opt/rchk/scripts/config.inc
. /opt/rchk/scripts/cmpconfig.inc
echo 'install.packages("jpeg",repos="http://cloud.r-project.org")' |  ./bin/R --slave
/opt/rchk/scripts/check_package.sh jpeg
less packages/lib/jpeg/libs/jpeg.so.maacheck
```

Further information:

* [User documentation](doc/USAGE.md) - how to use the tools and what they check.
* [Internals](doc/INTERNALS.md) - how the tools work internally.
* [Building](doc/BUILDING.md) - how to get the necessary bitcode files for R/packages; this is now encapsulated in scripts, but the background is here
