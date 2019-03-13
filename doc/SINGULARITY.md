The singularity support and this text is originally by B. W. Lewis.

The rchk project, https://github.com/kalibera/rchk is an important tool for
detecting memory protection errors and related subtle bugs in R packages that
contain compiled code and the R source code itself.

The project uses the LLVM compiler toolchain with the whole program LLVM
extensions.  The rchk project includes recipes for Docker and Vagrant
systems to help automate the set up process to build either containers or
virtual machines that can then be used to check R packages.

This note and the corresponding `singularity.def` file present an alternative
simple container recipe using the Singularity container system
(https://www.sylabs.io/docs/). Singularity is a lightweight, serverless (that
is, no daemon process), container system for GNU Linux popular in HPC settings.

This note outlines system requirements and installation of Singularity,
building a container for rchk, and testing R packages using the container.


## Installing singularity

Singularity requires a GNU Linux operating system. Most modern GNU Linux
systems include Linux kernels that will work.

On Ubuntu/Debian, one can install using `apt-get install
singularity-container`.  See https://www.sylabs.io/guides/3.0/user-guide/installation.html
for installation examples and instructions for other Linux systems. 
Alternatively, you may install Singularity directly from its source code in
GitHub with (requires the `git` command line client, GNU make and a C
compiler:

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

The `singularity.def` file includes a singularity definition file for
building a singularity container image based on Ubuntu 18.04 and the
LLVM-6.0.  Singularity containers may be built as single files or, for
experimentation, sandbox directories.

Note! If you're running on Red Hat or CentOS, you'll need the `debootstrap`
program: `sudo yum install debootstrap`. See the Singularity documentation for
more information.

Build the rchk singularity image with:
```
sudo singularity build rchk.img singularity.def       # makes rchk.img
```
See the Singularity documentation and the example below
for alternative output formats (like
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

## Sandboxed images

Some R packages may require additional operating system library dependencies
not included in the container recipe `singularity.def` above. You can add
additional Ubuntu-packaged libraries to the container recipe before building
it. Alternatively, you can build a container `sandbox` directory instead of a
single container image file, and dynamically add required libraries to the
sandbox directory as needed.

The following example builds a sandboxed container directory. We then try to
install the Rmpfr library for multi-precision arithmetic, which fails due to
unsatisifed library dependencies in the container image. The example proceeds
to manually install the required dependencies and then chkecks the package.


### Step 1. Build the sandboxed container

```
sudo singularity build --sandbox rchk singularity.def     # makes rchk directory
```

### 2. Try to check the Rmpfr package

```
PKG_ROOT=/tmp singularity run rchk  Rmpfr                 # try to check package

## ...
## ERROR: dependency ‘gmp’ is not available for package ‘Rmpfr’
## ...
```

### 3. Modify the sandboxed container to include required libraries

```
sudo singularity exec -w rchk /bin/bash

apt-get install -y libgmp-dev libmpfr-dev
## ... output of apt installation process

exit
```

Note that at this point, we can do anything to the container image that we
desire, including for instance installing library dependencies manually or
otherwise.

The sandboxed singularity container is the most flexible approach for checking
packages with dependencies.

### 4. Try to check the Rmpfr package again

```
PKG_ROOT=/tmp singularity run rchk  Rmpfr

## ... output of R package build process, which should finish without error

# Let's check the output:
cat /tmp/lib/Rmpfr/libs/Rmpfr.so.bcheck 

## Function MPFR_as_R
##   [UP] unprotected variable exp_R while calling allocating ...
##   [UP] unprotected variable prec_R while calling allocating ...
## ... (truncated output)
```

Now that the library dependencies are satisfied, we're able to check the
package with rchk. In this case, at least at the time of this writing,
we see a few potential issues.
