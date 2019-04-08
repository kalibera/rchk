The original version of the Singularity support and of this note has been
contributed by B. W. Lewis.

<!--
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
-->

This note outlines system requirements and installation of singularity,
building a container for rchk, and testing R packages using the container.

## Installing singularity

Singularity requires a GNU Linux operating system. Most modern GNU Linux
systems include Linux kernels that will work. 

On Ubuntu/Debian, one can install using `apt-get install singularity-container`, but 
a newer version may be available from
[Neuro Debian](http://neuro.debian.net/install_pkg.html?p=singularity-container).

More information can be found at
https://www.sylabs.io/guides/3.0/user-guide/installation.html.

## Building an rchk container image

The `singularity.def` file in directory `image` includes a singularity
definition file for building a singularity container image based on Ubuntu
18.04 and the LLVM-6.0.  Singularity containers may be built as single files
or, for experimentation, sandbox directories.

Note! If you're running on Red Hat or CentOS, you'll need the `debootstrap`
program: `sudo yum install debootstrap`. See the singularity documentation for
more information. You need to use a recent version of `debootstrap` that
supports Ubuntu 18.04 (bionic). On Debian Stretch as host system, one needs
to install `debootstrap` from backports (1.0.110 works fine, 1.0.89 does
not).

Build the rchk singularity image with (run in directory `image`):
```
sudo singularity build rchk.img singularity.def       # makes rchk.img
```

See the singularity documentation and the example below for alternative
output formats (like a sandboxed directory that you can investigate easily).

<!--
The container build process concludes with a usage message, or an error if
something goes wrong.
-->

## Checking a package with the rchk.img container

We've set the container up to make it easy to check R packages installed from
CRAN or from a local source file. The packages are built and installed into a
directory determined by the `PKG_ROOT` shell variable. If that variable is not
set then the current working directory is used for output.  Outputs are placed
in ${PKG_ROOT}/build and ${PKG_ROOT}/lib directories, which are created if they
do not exist.

Depending on the configuration of singularity, it may not be possible to
save the outputs in the current directory for security reasons (it worked
for me with the default configuration on Ubuntu 18.04 but not Debian 9.7;
setting PKG_ROOT=/tmp worked in both cases).

Generic container invocation is:
```
singularity run <container image file> <package name>
singularity run <container image file> <full path to package tarball>
singularity run R
```
The last form is to run R interactively, e.g. to install packages needed as
dependencies. Note though that for CRAN packages and tarballs of new
versions of CRAN packages, this is done automatically.

For compatibility reasons, one can also invoke as:

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
This output means that `rchk/bcheck` found no problems.

The following example checks a local source package, placing the rchk
output in `/tmp`:
```
wget https://cran.r-project.org/src/contrib/irlba_2.3.1.tar.gz
PKG_ROOT=/tmp singularity run rchk.img irlba $(pwd)/irlba_2.3.1.tar.gz

cat /tmp/lib/irlba/libs/irlba.so.bcheck 

# Analyzed 71 functions, traversed 489 states.
```

If installation of a package from a tarball fails, but the package of that
name has not previously been installed, the container automatically attempts
to install the same package from CRAN and then again from the local tarball. 
This is to handle automatically the common case when the installation has
originally failed because of missing package dependencies.

## Sandboxed images

Some R packages may require additional operating system library dependencies
not included in the container recipe `singularity.def` above. You can add
additional Ubuntu-packaged libraries to the container recipe before building
it. Alternatively, you can build a container `sandbox` directory instead of a
single container image file, and dynamically add required libraries to the
sandbox directory as needed. You can also use an `overlay` (see below),
which works also with the pre-built image available from Singularity hub.

The following example builds a sandboxed container directory. We then try to
install the Rmpfr library for multi-precision arithmetic, which fails due to
unsatisifed library dependencies in the container image. The example proceeds
to manually install the required dependencies and then checks the package.


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

## Overlays

The pre-built image available from Singularity hub is read-only, but one can
install Ubuntu packages into a writeable overlay.  To check package
`BoolNet`, which needs package `XML`, which needs Ubuntu package
`libxml2-dev`, one can proceed as follows (unfortunately, to become root
inside the container, one needs to run singularity as root):

```
singularity image.create myoverlay.img
sudo singularity shell --overlay myoverlay.img rchk.simg
  apt-get update
  apt-get install libxml2-dev
singularity run --overlay myoverlay.img rchk.simg BoolNet
```

Also, one can build a writeable Singularity image from the container
definition.  See (https://www.sylabs.io/docs/)[Singularity documentation]
for more information.
