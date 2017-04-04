
This project consists of several bug-finding tools that look for memory
protection errors in the C source code of [GNU
R](http://www.r-project.org/) and packages.  

A number of bugs has been found in
[R-devel](https://svn.r-project.org/R/trunk/) using this tool and fixed.
The tool can also be used to find errors in R packages (e.g.  from
CRAN/BIOC).

The installation of dependencies for the tool is somewhat involved. It has
now been tested on Ubuntu 16.04.2. One can also use a pre-installed virtual
machine with `rchk` (or, more precisely, use an automated script that
installs such machine without user intervention).

Manual installation on Ubuntu 16.04.2:

0. Install build dependencies for GNU-R:
	1. enable source repositories in `/etc/apt/sources.list`
	2. `apt-get build-dep -y r-base-dev`
	3. `apt-get install libcurl4-openssl-dev`
1. Install clang and llvm: `apt-get install clang-3.8 llvm-3.8-dev clang\+\+-3.8 clang llvm-dev`
2. Install [WLLVM scripts](https://github.com/travitch/whole-program-llvm):
	1. `apt-get install python-pip`
	2. `pip install --upgrade pip`
	3. `pip install --user DIR` where DIR is checked-out WLLVM
3. Install [rchk](https://github.com/kalibera/rchk.git):
	1. `make`
	2. modify script `scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM can be `/usr` on Ubuntu 16.04.2

The clang/llvm version in this version of Ubuntu is 3.8.  The tool can be
used also with [LLVM binary
distributions](http://llvm.org/releases/download.html).  It is extremely
unlikely that the `master` version of the tool will work with any other
version of LLVM than 3.8 due to frequent changes in LLVM API.  An older
version working with LLVM 3.6 is available on the `llvm-36` branch but is no
longer updated.

Alternatively, one can install automatically into a VirtualBox image

1. Install (manually) [VirtualBox](https://www.virtualbox.org/wiki/Downloads), e.g. `apt-get install virtualbox`
2. Install (manually) [Vagrant](https://www.vagrantup.com/), e.g. `apt-get install vagrant`
3. Install (automatically) R build dependencies, LLVM, WLLVM and rchk: run `vagrant up` in `image` directory

Note that the automated installation will take long, as it will be
downloading an Ubuntu 16.04.2 image and installing the R build dependencies
to a fresh Ubuntu image. Should the installation fail or time out, it can
be re-started by `vagrant provision`.  One can log in to the machine by
`vagrant ssh` to use the tools after successful install or to fix issues
should the installation fail.

For both native and virtual installation, to check GNU-R:

4. Get latest version of GNU-R: `svn checkout https://svn.r-project.org/R/trunk`
5. Build it using for rchk (run in R source tree)
	1. `. <rchk_root>/scripts/config.inc` (`. /opt/rchk/scripts/config.inc`)
	2. `<rchkroot>/scripts/build_r.sh` (`. /opt/rchk/scripts/cmpconfig.inc`)
6. Run default rchk tools on GNU-R: ``<rchkroot>/scripts/check_r.sh` (/opt/rchk/scripts/check_r.sh). Look for
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
again look for files with suffices `.maacheck` and `.bcheck`.

For example, to check the "curl" package in the virtual installation:

```bash
svn checkout https://svn.r-project.org/R/trunk
cd trunk
. /opt/rchk/scripts/config.inc
/opt/rchk/scripts/build_r.sh

. /opt/rchk/scripts/config.inc
. /opt/rchk/scripts/cmpconfig.inc
echo 'install.packages("curl",repos="http://cran.ma.imperial.ac.uk")' |  ./bin/R --slave
/opt/rchk/scripts/check_package.sh curl
less packages/lib/curl/libs/curl.so.bcheck
```

Further information:

* [User documentation](doc/USAGE.md) - how to use the tools and what they check.
* [Internals](doc/INTERNALS.md) - how the tools work internally.
* [Building](doc/BUILDING.md) - how to get the necessary bitcode files for GNU-R/packages; this is now encapsulated in scripts, but the background is here
