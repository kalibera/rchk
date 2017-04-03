
This project consists of several bug-finding tools that look for memory
protection errors in the C source code of [GNU
R](http://www.r-project.org/) and packages.  

A number of bugs have been found in [R-devel](https://svn.r-project.org/R/trunk/)
using this tool and later fixed. The tool can also be used to find errors in
R packages (e.g. from CRAN/BIOC).

Manual installation on Ubuntu 16.04.2:

0. Install build dependencies for GNU-R: `apt-get build-dep -y r-base-dev`
1. Install clang and llvm: `apt-get install clang clang++ llvm-dev`
2. Install (download, `pip install --upgrade pip`, `pip install --user .`) [WLLVM scripts](https://github.com/travitch/whole-program-llvm).
3. Install [rchk](https://github.com/kalibera/rchk.git):
	1. `make`
	2. modify script `scripts/config.inc` (set root of LLVM, WLLVM, and rchk)

The clang/llvm version on Ubuntu is 3.8. The tool can be used also with
[binary distributions](http://llvm.org/releases/download.html) of LLVM. It
is extremely unlikely that the `master` version of the tool will work with
another version of LLVM due to frequent changes in LLVM API. An older
version working with LLVM 3.6 is available on the `llvm-36` branch but is no
longer updated. 

Alternatively, one can install automatically into a VirtualBox image
(this is not working at the moment - **I am in the process of updating
this**)

1. Install (manually) [VirtualBox](https://www.virtualbox.org/wiki/Downloads)
2. Install (manually) [Vagrant](https://www.vagrantup.com/).
3. Install (automatically) R build dependencies, LLVM, WLLVM and rchk: run
`vagrant up` from `image` directory.  Note that the installation will take
long, as it will be downloading an Ubuntu 16.04.2 image and installing indeed
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
