
This project consists of several bug-finding tools that look for memory
protection errors in C source code using R API, that is in the source code
of [R](http://www.r-project.org/) itself and packages.  The tools perform
whole-program static analysis on LLVM bitcode and run on Linux.  About
200-300 memory protection bugs have been found using rchk and fixed in R. 
rchk is now regularly used to check [CRAN
packages](https://github.com/kalibera/cran-checks/tree/master/rchk).

To use the tool, one needs to build R from source using a special compiler
wrapper, which builds LLVM bitcode in addition to native code (both shared
libraries and executables). R packages are then installed using this version
of R, providing LLVM bitcode for their shared libraries as well. The core of
rchk is implemented in C++ and analyzes the LLVM bitcode of R packages and R
itself. Several installation options are provided, including containers.

## Installation

The tool is available in pre-built containers, Docker and Singularity, for
*non-interactive* use. The container is invoked as a command to check a
particular package:

```
docker pull kalibera/rchk:latest
docker run kalibera/rchk:latest audio
```

```
singularity pull shub://kalibera/rchk:def
singularity run kalibera-rchk-master-def.simg jpeg
```

For more details, see [Docker rchk container](doc/DOCKER.md) and 
[Singularity rchk container](doc/SINGULARITY.md). This setup is good for
occasional checking of a single package. Docker clients are
available for Linux, macOS and Windows. Singularity only for Linux.

The tool can also be used interactively in a virtual machine running Ubuntu,
which can be automatically installed using Vagrant scripts. This setup is
good for Linux, Windows and macOS users and makes it faster to repeatedly
check the same package and easier to customize the process. See
[Automated installation (Docker/Virtualbox) for interactive use](doc/INSTALLATION.md).

Finally, the tool can be installed natively on Linux, compiled from source.
This setup is good for interactive use and reduces disk space overhead. The
setup is not automated, but only requires several steps described for recent
Linux distributions. See [Native installation on Linux for interactive use](doc/INSTALLATION.md).

An alternative docker image is also available from third parties on R-hub
(`rhub/ubuntu-rchk`,
[source](https://github.com/r-hub/rhub-linux-builders/tree/master/ubuntu-rchk)).

## Checking the first package (interactive use)

This part applies to interactive installation of rchk (natively or automated
install in Docker/Virtualbox).  For this that one also needs to install
`subversion`, `rsync` (`apt-get install subversion rsync`, but already
available in the automated install).  More importantly, one also needs any
dependencies needed by that package.

1. Build R producing also LLVM bitcode
	* `svn checkout https://svn.r-project.org/R/trunk`
	* `cd trunk`
	* `. ../scripts/config.inc` (*in automated install*, `. /opt/rchk/scripts/config.inc`)
	* `. ../scripts/cmpconfig.inc` (*in automated install*, `. /opt/rchk/scripts/cmpconfig.inc`)
	* `../scripts/build_r.sh` (*in automated install*, `/opt/rchk/scripts/build_r.sh`)
2. Install and check the package
	* `echo 'install.packages("jpeg",repos="http://cloud.r-project.org")' |  ./bin/R --no-echo`
	* `../scripts/check_package.sh jpeg` (in VM install, `/opt/rchk/scripts/check_package.sh jpeg`)

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

To check the next package, just follow the same steps, installing it into
this customized version of R.  When checking a tarball, one would typically
first install the CRAN/BIOC version of the package to get all dependencies
in, and then use `R CMD INSTALL` to install the newest version to check from
the tarball.

One can reduce the number of required R package dependencies by only
installing LinkingTo dependencies of the package and then installing the
package with `--libs-only` option (only shared libraries are built and
installed). This is enough to build shared libraries of most but not all
packages. Docker and singularity rchk containers for non-interactive use do
this, see `scripts/utils.r` and definitions of the containers for more
details.

Further information:

* [Installation](doc/INSTALLATION.md) - installation instructions.
* [User documentation](doc/USAGE.md) - how to use the tools and what they check.
* [Internals](doc/INTERNALS.md) - how the tools work internally.
* [Building](doc/BUILDING.md) - how to get the necessary bitcode files for R/packages; this is now encapsulated in scripts, but the background is here

[![https://www.singularity-hub.org/static/img/hosted-singularity--hub-%23e32929.svg](https://www.singularity-hub.org/static/img/hosted-singularity--hub-%23e32929.svg)](https://singularity-hub.org/collections/2534)