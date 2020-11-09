The original version of the Singularity support and of this note has been
contributed by B. W. Lewis. Both has been updated by rchk author.

This note and the corresponding `Singularity.def` and `Singularity.bionic`
files present an alternative simple container recipe using the Singularity
container system (https://www.sylabs.io/docs/).  Singularity is a serverless
(that is, no daemon process) container system for GNU Linux popular in HPC
settings.

These two configurations are used to build container images via Singularity
hub. Those images can be used for checking R package tarballs and packages
from CRAN/BIOC repositories. Required dependencies (R packages) are
automatically downloaded and installed from CRAN/BIOC. One does not need a
root account on the machine to use these pre-built containers.

However, some R packages require OS libraries and not all of those needed by
all CRAN and BIOC packages are present in the image (it would be too large,
otherwise). More effort is needed to install such libraries into the
container and with Singularity, this requires root account. Building the
singularity container from its definition also requires root account.

This note outlines system requirements and installation of singularity,
building a container for rchk, and testing R packages using the container.

## Installing singularity

Singularity requires a GNU Linux operating system. Most modern GNU Linux
systems include Linux kernels that will work. 

For Ubuntu/Debian, the package name is `singularity-container` and it is
available in the main distribution (Debian Sid, Ubuntu 18.04) or from 
[Neuro Debian](http://neuro.debian.net/install_pkg.html?p=singularity-container)
(Debian 9, 10, 11, Ubuntu 20.04, 18.04, 16.04). For Fedora and openSUSE, the
package name is `singularity` and it is available from the main
distribution.

More information can be found at
https://www.sylabs.io/guides/3.0/user-guide/installation.html.

## Building an rchk container image

The `Singularity.def` file in directory `image` includes a singularity
definition file for building a singularity container image based on Ubuntu
20.04 (focal) and the LLVM-10.0.  The `Singularity.bionic` file in directory
`image` is an older version based on Ubuntu 18.04 (bionic). It does not get
all the updates and improvements, but is still provided to build an image on
Singularity hub for use with older versions of singularity (Ubuntu 20.04 on
Singularity hub is only supported by a builder which requires a newer
version of singularity than available on some systems).

Singularity containers may be built as single files or, for experimentation,
sandbox directories.

Note!  If you're running on Red Hat, Fedora or CentOS, you'll need the
`debootstrap` program: `sudo yum install debootstrap`.  See the singularity
documentation for more information.  You need to use a recent version of
`debootstrap` that supports Ubuntu 20.04 (focal), or you need to build the
older container based on Ubuntu 18.04 (bionic).

Build the rchk singularity image with (run in directory `image`):
```
sudo singularity build rchk.img singularity.def       # makes rchk.img
```

See the singularity documentation and the example below for alternative
output formats (like a sandboxed directory that you can investigate easily).

## Checking a package with the rchk.img container

We've set the container up to make it easy to check R packages installed
from CRAN or BIOC or from a local source file.  The packages are built and
installed into a directory determined by the `PKG_ROOT` shell variable.  If
that variable is not set then the current working directory is used for
output.  Outputs are placed in ${PKG_ROOT}/build, ${PKG_ROOT}/lib and
${PKG_ROOT}/libsonly directories, which are created if they do not exist.

Depending on the configuration of singularity, it may not be possible to
save the outputs in the current directory for security reasons (it worked
for me with the default configuration on Ubuntu 18.04 and 20.04 but not
Debian 9.7; setting PKG_ROOT=/tmp worked in both cases).

Generic container invocation is:
```
singularity run <container image file> <package name>
singularity run <container image file> <full path to package tarball>
singularity run <container image file> R
```
The last form is to run R interactively, e.g. to install packages needed as
dependencies. Note though that for CRAN packages and tarballs of new
versions of CRAN packages, this is done automatically, so this should not be
really needed.

In Singularity, environment variables and home directories are by default
inherited from the host system. Some care is taken for R running inside the
container to be robust against this (setting R_LIBS variables, not running R
init files, etc), but it is possible that some of these variables have been
left unhandled, and that has been the case in the past. In case of
surprising behavior of the container, this may be worth checking.

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

The container uses `install_package_libraries` from `utils.r` to install
just shared libraries of the package to check (using `R CMD INSTALL
--libs-only`).  Packages (shared libraries) installed this way are kept in
`libsonly`.  Dependent R packages are installed fully and are kept in
`libs`.  `build` includes build directories of both kinds of packages (what
normally is in temporary session directory when using R, but with rchk,
these are needed to extract LLVM bitcode).  `libsonly` is in fact not
re-used across runs of the container, but `lib` and `build` are when
available.  They may be deleted when the container is not running, and
dependencies will then be re-installed as needed. Outputs from the checks
are kept in files ending with `check` next to the shared library objects
checked, so under `libsonly` with the def image and `libs` with the bionic
image.


## Sandboxed images

Some R packages may require additional operating system library dependencies
not included in the container recipe above.  You can add additional
Ubuntu-packaged libraries to the container recipe before building it. 
Alternatively, you can build a container `sandbox` directory instead of a
single container image file, and dynamically add required libraries to the
sandbox directory as needed.  You can also use an `overlay` (see below),
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
dd if=/dev/zero of=myoverlay.img bs=1M count=500 && mkfs.ext3 myoverlay.img
sudo singularity shell --overlay myoverlay.img rchk.simg
  apt-get update
  apt-get install libxml2-dev
singularity run --overlay myoverlay.img rchk.simg BoolNet
```

The above should work both in Singularity 2.x and 3.x.  In Singularity 2.x,
one could also create the image using `image.create` instead of
`dd`/`mkfs.ext3`:

```
singularity image.create myoverlay.img
```

The overlay image can be inserted into the same SIMG container image file.
Also, one can build a writeable Singularity image from the container
definition.  See [Singularity documentation](https://www.sylabs.io/docs/)
for more information.
