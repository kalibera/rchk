The rchk project, https://github.com/kalibera/rchk is an important tool for
detecting memory protection errors and related subtle bugs in R packages that
contain compiled code and the R source code itself.

The project uses the LLVM compiler toolchain with the whole program LLVM
extensions. As noted in the rchk project README, setting up the toolchain is a
complicated process requiring careful attention to a very specific set of
software dependencies.

The rchk project includes recipes for Docker and Vagrant systems to help
automate the set up process to build either containers or virtual machines that
can then be used to check R packages.

This note and the corresponding `singularity.def` file present an alternative
simple container recipe using the Singularity container system
(http://singularity.lbl.gov/). Singularity is a lightweight, serverless (that
is, no daemon process), container system for GNU Linux popular in HPC settings.
It's a very lightweight an minimalist containerization framework that we feel
is ideal for projects like rchk.

This note outlines system requirements and installation of Singularity,
building a container for rchk, and testing R packages using the container.


## Installing singularity

Singularity requires a GNU Linux operating system. Most modern GNU Linux
systems include Linux kernels that will work.

See http://singularity.lbl.gov/docs-installation for installation examples
specific to Ubuntu and CentOS/RHEL operating systems. Alternatively,
you may install Singularity directly from its source code in GitHub with
(requires the `git` command line client, GNU make and a C compiler:

```
git clone https://github.com/singularityware/singularity.git
cd singularity
./autogen.sh
./configure --prefix=/usr/local
make
sudo make install
``` 

Singularity is simply a program. No daemon process/server is needed.


## Building an rchk container image

The `rck.def` file includes a singularity definition file for building a
singularity container image based on Ubuntu 17.04 and the LLVM-4.0 and
corresponding WLLVM toolchains.  Singularity containers may be built as single
files or, for experimentation, sandbox directories.

Note! If you're running on Red Hat or CentOS, you'll need the `debootstrap`
program: `sudo yum install debootstrap`. See the Singularity documentation for
more information.

Build the rchk singularity image with:
```
sudo singularity build rchk.img singularity.def       # makes rchk.img
```
See the Singularity documentation for alternative output formats (like
a sandboxed directory that you can investigate easily).

The container build process concludes with a usage message, or an error if
something goes wrong.

## Checking a package with the rchk.img container

We've set the container up to make it easy to check R packages installed from
CRAN or from a local source file. The packages are built and installed into a
directory determined by the `PKG_ROOT` shell variable. If that variable is not
set then the current working directory is used for output.  Output are placed
in ${PKG_ROOT}/build and ${PKG_ROOT}/lib directories, which are created if they
do not exist. The container uses the http://cran.ma.imperial.ac.uk repository
for network-installed packages.

Generic container invocation is:
```
singularity run <container image file> <package name>  [source package path]
```

Here is an example that checks the R package `curl` installed from CRAN,
placing rchk output in the current directory:
```
singularity run rchk.img curl
```

Inspect the rchk output with, for instance:
```
cat ./lib/curl/libs/curl.so.bcheck 

## Analyzed 86 functions, traversed 864 states.
```

The following example checks a local source package, placing the rchk
output in `/tmp`:
```
wget https://cran.r-project.org/src/contrib/irlba_2.3.1.tar.gz
PKG_ROOT=/tmp singularity run rchk.img   irlba   $(pwd)/irlba_2.3.1.tar.gz

cat /tmp/lib/irlba/libs/irlba.so.bcheck 

# Analyzed 71 functions, traversed 489 states.
```
