
This project consists of several bug-finding tools that look for memory
protection errors in C source code using R API, that is in the source code
of [R](http://www.r-project.org/) itself and packages.  The tools perform
whole-program static analysis on LLVM bitcode and run on Linux.  About
200-300 memory protection bugs have been found using rchk and fixed in R. 
rchk is now regularly used to check [CRAN
packages](https://github.com/kalibera/cran-checks/tree/master/rchk).

The tools can be used from a pre-built singularity container on Linux
systems. To check R package `jpeg` from CRAN, one needs to do

```
singularity pull shub://kalibera/rchk:def
singularity run kalibera-rchk-master-def.simg jpeg
```

Note that the default image file name may be different (e.g. 
`rchk_def.sif`), based on the version of singularity used.  The results will
be printed and saved in `lib` directory (`lib/jpeg/libs/jpeg.so.bcheck` and
`lib/jpeg/libs/jpeg.so.maacheck`).  Full path to the package tarball can be
given instead to check a version of the package not yet on CRAN.  I've
tested this on Ubuntu 18.04 (singularity 2.6 from [Neuro
Debian](http://neuro.debian.net/install_pkg.html?p=singularity-container))
and on Debian 9.8 (singularity 2.6 from stretch-backports) and on Debian
buster/testing (singularity 3.0.3 from the distribution).

One can also build the singularity container from source, this is also fully
automated, it takes longer than downloading the pre-built container, but it
does not depend on external binaries and in Ubuntu 18.04 one can use the old
`singularity-container` package from the distribution.  On Debian 9.8, one
needs to use debootstrap from stretch-backports. Currently, Singularity Hub
used for pre-building packages uses an old version of debootstrap, which
does not support Ubuntu 20.04, hence an older version of the image is used,
based on Ubuntu 18.04.

See [Singularity Instructions](doc/SINGULARITY.md),
[Installation](doc/INSTALLATION.md) for more details how to use the
container and how to build it.  External libraries needed for some R
packages can be installed into the container using an *overlay* (also works
for the pre-built image) or using a *sandbox* (documented
[here](doc/SINGULARITY.md)).  An initial version of the singularity
container has been contributed by B.  W.  Lewis.

The tools can also be installed automatically into a Virtualbox or Docker
container and log into that virtual machine and use it from command line
interactively to check packages, check R itself, etc.  Virtualbox
installation is possible on Windows, macOS and Linux; Docker installation
only on Linux.  See [Installation](doc/INSTALLATION.md) and the steps below
on checking the first package.

On Linux, one can also install `rchk` natively, which has been tested on
recent Ubuntu, Debian and Fedora distributions.  This is the fastest and
most flexible way to use `rchk` for users working on Linux.  See
[Installation](doc/INSTALLATION.md) and the steps below on checking the
first package.

## Checking the first package (not for Singularity containers)

For this that one also needs to install `subversion`, `rsync` (`apt-get
install subversion rsync`, but already available in the automated install). 
More importantly, one also needs any dependencies needed by that package.

1. Build R producing also LLVM bitcode
	* `svn checkout https://svn.r-project.org/R/trunk`
	* `cd trunk`
	* `. ../scripts/config.inc` (*in automated install*, `. /opt/rchk/scripts/config.inc`)
	* `. ../scripts/cmpconfig.inc` (*in automated install*, `. /opt/rchk/scripts/cmpconfig.inc`)
	* `../scripts/build_r.sh` (*in automated install*, `/opt/rchk/scripts/build_r.sh`)
2. Install and check the package
	* `echo 'install.packages("jpeg",repos="http://cloud.r-project.org")' |  ./bin/R --slave`
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

Further information:

* [Installation](doc/INSTALLATION.md) - installation instructions.
* [User documentation](doc/USAGE.md) - how to use the tools and what they check.
* [Internals](doc/INTERNALS.md) - how the tools work internally.
* [Building](doc/BUILDING.md) - how to get the necessary bitcode files for R/packages; this is now encapsulated in scripts, but the background is here

[![https://www.singularity-hub.org/static/img/hosted-singularity--hub-%23e32929.svg](https://www.singularity-hub.org/static/img/hosted-singularity--hub-%23e32929.svg)](https://singularity-hub.org/collections/2534)