
This branch **is not going to be updated anymore**, it is for LLVM 3.6.

This project consists of several bug-finding tools that look for memory
protection errors in the C source code of [GNU
R](http://www.r-project.org/) and packages.  

We are fixing bugs in [R-devel](https://svn.r-project.org/R/trunk/),
both in the core R and in some packages that are part of the distribution. 
We have not been fixing the CRAN/BIOC packages, where a number of errors can
be found as well. We are happy to give advice to interested package
maintainers on how to use the tool.

Manual installation on Ubuntu 15.04/16.04:

0. Install build dependencies for GNU-R: `apt-get build-dep -y r-base-dev`
1. Install a binary version of [CLANG+LLVM 3.6.1](http://llvm.org/releases/download.html#3.6.1).
2. Install (download) [WLLVM scripts](https://github.com/travitch/whole-program-llvm).
3. Install [rchk](https://github.com/kalibera/rchk.git):
  1. `make LLVM=<llvm_root> CXX=g++-4.8`
  2. modify script `scripts/config.inc` (set root of LLVM, WLLVM, and rchk)

The `llvm-36` branch is for LLVM 3.6 and is not going to be updated anymore. 
The virtual installation as mentioned below no longer works, but if needed,
it could be fixed following fixes in master.  The manual installation still
seems to be working.  A version for LLVM 3.8 is available in branch
`llvm-38` (it has been tested on Ubuntu 16.04 and Fedora Core 23 using the
system versions of GCC C++ compiler; it does not work with the CLANG
compiler).  We used again LLVM 3.8 (3.8.0 and 3.8.1) [binary
distributions](http://llvm.org/releases/download.html).

Alternatively, one can install automatically into a VirtualBox image:

1. Install (manually) [VirtualBox](https://www.virtualbox.org/wiki/Downloads)
2. Install (manually) [Vagrant](https://www.vagrantup.com/).
3. Install (automatically) R build dependencies, LLVM, WLLVM and rchk: run
`vagrant up` from `image` directory.  Note that the installation will take
long, as it will be downloading an Ubuntu 15.04 image and installing indeed
the R build dependencies onto a fresh Ubuntu image.  Log in to the machine
by `vagrant ssh`.

For both native and virtual installation, to check GNU-R:

4. Get latest version of GNU-R: `svn checkout http://svn.r-project.org/R/trunk`
5. Build it using for rchk (run in R source tree)
  1. `. <rchk_root>/scripts/config.inc`
  2. `<rchkroot>/scripts/build_r.sh`
6. Run default rchk tools on GNU-R: ``<rchkroot>/scripts/check_r.sh`. Look for
files with suffixes `.maacheck` and `.bcheck` under `src`, e.g. 
`src/main/R.bin.bcheck` is the result of running `bcheck` tool on the R
binary. `<rchk_root>` is `/opt/rchk`.

To check a package:

1. Prepare the environment for build
  1. `. <rchk_root>/scripts/config.inc`
  2. `. <rchk_root>/scripts/cmpconfig.inc`
2. Install packages from within R: `./bin/R` (use `install.packages` or
`biocLite` or any other mechanism that in the end uses `instal.packages`
3. Check all installed packages: `<rchkroot>/scripts/check_package.sh`
4. The results of the checks will appear under `packages/lib/<package_dir>`,
again look for files with suffices `.maacheck` and `.bcheck`.

To check the "curl" package from the virtual installation:

```
svn checkout http://svn.r-project.org/R/trunk
cd trunk
. /opt/rchk/scripts/config.inc
/opt/rchk/scripts/build_r.sh

. /opt/rchk/scripts/config.inc
. /opt/rchk/scripts/cmpconfig.inc
echo 'install.packages("curl",repos="http://cran.ma.imperial.ac.uk")' |  ./bin/R --slave
/opt/rchk/scripts/check_package.sh curl
less /home/vagrant/trunk/packages/lib/curl/libs/curl.so.bcheck
```

Further information:

* [User documentation](doc/USAGE.md) - how to use the tools and what they check.
* [Internals](doc/INTERNALS.md) - how the tools work internally.
* [Building](doc/BUILDING.md) - how to get the necessary bitcode files for GNU-R/packages; this is now encapsulated in scripts, but the background is here
