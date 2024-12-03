Rchk is available also in a docker container intended to be run as a
command. It checks a package from tarball or CRAN/BIOC, dependencies from
CRAN/BIOC are installed automatically.

[Docker](https://www.docker.com/) is available for free for Linux, macOS and
Windows.  I've tested the rchk container on Ubuntu 20.04 (package docker.io,
19.03.8), on macOS (Docker Desktop, 19.03.13) and on Windows (Docker
Desktop, 19.03.13). Docker is not officially supported on Fedora 32.

The rchk docker image is inspired by an earlier image from Filip Krikava.

A pre-built image is available on Docker hub:

```
docker pull kalibera/rchk:latest
```

## Checking a package from CRAN/BIOC

```
docker run --rm kalibera/rchk:latest audio
```

The output ends with (for an earlier version of audio package)

```
Installed libraries of package  audio 
[1] "/rchk/packages/libsonly/audio/libs/audio.so"

ERROR: too many states (abstraction error?) in function strptime_internal
Analyzed 23 functions, traversed 465 states.
Library name (usually package name): audio
ERROR: did not find initialization function R_init_audio
WARNING Suspicious call (two or more unprotected arguments) to Rf_setAttrib at audio_player /rchk/packages/build/uwK7v2BW/audio/src/driver.c:241
WARNING Suspicious call (two or more unprotected arguments) to Rf_setAttrib at audio_recorder /rchk/packages/build/uwK7v2BW/audio/src/driver.c:263
WARNING Suspicious call (two or more unprotected arguments) to Rf_setAttrib at load_wave_file /rchk/packages/build/uwK7v2BW/audio/src/file.c:180
WARNING Suspicious call (two or more unprotected arguments) to Rf_setAttrib at load_wave_file /rchk/packages/build/uwK7v2BW/audio/src/file.c:181
WARNING Suspicious call (two or more unprotected arguments) to Rf_setAttrib at load_wave_file /rchk/packages/build/uwK7v2BW/audio/src/file.c:182
```

`ERROR: too many states (abstraction error?)` can be ignored, it informs
about that `strptime_internal` function in R is too complicated to be
analyzed.  `ERROR: did not find initialization function R_init_audio` is
from `fficheck` (checking foreign function call registration).  `WARNING
Suspicious call` are from `maacheck` (checking for likely PROTECT bugs due
to multiple allocating arguments in a function).  `src/driver.c:241` is

```
       Rf_setAttrib(ptr, Rf_install("class"), Rf_mkString("audioInstance"));
```

The unprotected result of `Rf_mkString` can be destroyed by GC during
allocating function `Rf_install`.

## Keeping container state

When invoked as above, R package dependencies are installed automatically on
every invocation of the container.

To avoid this overhead, one can create directory `packages` and make it
available to the container to keep state across invocations.

This allows to re-use R packages installed as dependencies (in
`packages/libs`), to keep record of built package sources (in
`packages/build`) and of checking outputs (in `packages/libsonly`).  These
directories also include additional state used by the container.  Also, this
directory can be used for storing tarballs of packages to check, so that the
container can access them. More details are provided in the following.

## Checking a package from a tarball

To check a package tarball (on Linux and macOS)

```
# wget https://cran.r-project.org/src/contrib/Archive/lazy/lazy_1.2-16.tar.gz
mkdir packages
cp lazy_1.2-16.tar.gz packages
docker run --rm -v `pwd`/packages:/rchk/packages kalibera/rchk:latest /rchk/packages/lazy_1.2-16.tar.gz
```

On Windows:

```
mkdir packages
copy lazy_1.2-16.tar.gz packages
docker run --rm -v %cd%/packages:/rchk/packages kalibera/rchk:latest /rchk/packages/lazy_1.2-16.tar.gz
````

In the above, directory `packages` is made available to the container under
`/rchk/packages`, hence the full path to the tarball
`packages/lazy_1.2-16.tar.gz` is `/rchk/packages/lazy_1.2-16.tar.gz`.

The output from the checking ends with

```
Installed libraries of package  lazy 
[1] "/rchk/packages/libsonly/lazy/libs/lazy.so"

ERROR: too many states (abstraction error?) in function strptime_internal

Function allocateOutput
  [PB] has too high protection stack depth results will be incomplete
  [UP] protect stack is too deep, unprotecting all variables, results will be incomplete
  [PB] has possible protection stack imbalance /rchk/packages/build/YndbRofp/lazy/src/lazy.c:476

Function getPar
  [UP] unprotected variable cbR while calling allocating function Rf_warning /rchk/packages/build/YndbRofp/lazy/src/lazy.c:259

Function packOutput
  [PB] has an unsupported form of unprotect (not constant, not variable), results will be incomplete /rchk/packages/build/YndbRofp/lazy/src/lazy.c:886
  [UP] unsupported form of unprotect, unprotecting all variables, results will be incomplete /rchk/packages/build/YndbRofp/lazy/src/lazy.c:886
Analyzed 23 functions, traversed 3452 states.
Library name (usually package name): lazy
Initialization function: R_init_lazy
Functions: 1
Checked call to R_registerRoutines: 1
```

Messages starting with `[PB]` (PROTECT balance checking) and `[UP]`
(unprotected pointers) above are from `bcheck`. The source file
`/rchk/packages/build/YndbRofp/lazy/src/lazy.c` is in the package tarball
but also in `packages/build/YndbRofp/lazy/src/lazy.c`. It is easy to see
that function `allocateOutput` really has PROTECT imbalance, at least
because it ends with (line numbers added)

```
  if (LOGICAL_DATA(I_out)[0]){
      out->lAns++;
      out->I=(intOut_t*)R_alloc(1,sizeof(intOut_t));
467:  PROTECT(out->I->R=NEW_INTEGER(aI->mzA*aI->q));
468:  PROTECT(dim=NEW_INTEGER(2));
      INTEGER_POINTER(dim)[0]=aI->mzA; INTEGER_POINTER(dim)[1]=aI->q;
      SET_DIM(out->I->R,dim);
471:  UNPROTECT(1);
      out->I->c=INTEGER_POINTER(out->I->R);
      out->noRptd++;
  }else
      out->I=0;
}
```
The UNPROTECT at line 471 matches the PROTECT at line 468, but the object
protected at line 467 will not be unprotected.

Split outputs from checking by tool are in `./packages/libsonly/lazy/libs/`:
`lazy.so.maacheck`, `lazy.so.bcheck`, `lazy.so.fficheck`.


## Checking a package with R package dependencies

R package dependencies are installed automatically, so running (omit `-p` on Windows)

```
mkdir -p packages
docker run --rm -v `pwd`/packages:/rchk/packages kalibera/rchk:latest xts
```

installs automatically dependencies `zoo` and `lattice`. These packages are
kept in `packages/libs`, so that when checking `xts` again, they won't have
to be re-installed (if we did not make `packages` available to the
container, it would use its own version, which it will build from scratch).

The container tries to be smart about installing a small number of
dependencies. First, it only installs LinkingTo dependencies with their
recursive default (Imports, Depends, LinkingTo) dependencies. If that fails
to provide a shared library of the package, it tries again but installing
all default dependencies of the checked package. This means also that a
package build/installation may be run twice, and when it fails for some
other reason, it will fail twice.

## Checking a package depending on unavailable library

For example, checking Rmpfr (omit `-p` on Windows)

```
mkdir -p packages
docker run --rm -v `pwd`/packages:/rchk/packages kalibera/rchk:latest Rmpfr
```

fails, because it needs mpfr library to be installed in the container, but
it is not, and as explained above compilation of specifically Rmpfr is tried
twice. It is not possible to include all libraries needed by CRAN/BIOC
packages in the container, because it would be too large. One can, however,
instruct the container to install these Ubuntu packages before checking
(omit `-p` on Windows):

```
mkdir -p packages
docker run --rm -v `pwd`/packages:/rchk/packages kalibera/rchk:latest --install-deb "libgmp-dev libmpfr-dev"  Rmpfr
```

Note that while `packages`, when made available to the container, can keep the
installed R packages, the installed Ubuntu packages are always lost when the
container run finishes, so `--install-deb "libgmp-dev libmpfr-dev"` (or the
non-dev alternatives) always have to be provided for `Rmfpr` R package to be
usable, unless the container is modified.

As per docker default, the user inside the container is root. Hence, it can
install Ubuntu packages as instructed. However, this has a consequence for
the `packages` directory: when provided to the container, it should always
be first created in the host file system. If it does not exist, it will be
created, but owned by root (even in the host file system), so e.g. not easy
to delete.

When `packages` is already created by regular user in the host file system,
rchk docker container, after installing Ubuntu packages, will create a
regular user to match the regular user owning `packages` and run under that
user.

## Modifying the container

To avoid the need of installing Ubuntu packages on every invocation of the
container, one can modify the container to already contain those. For
example, create this Dockerfile in an empty directory:

```
FROM kalibera/rchk:latest

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
  apt-get install -yq libgmp-dev libmpfr-dev && \
  rm -rf /var/lib/apt/lists/*
```

and run `docker build .`. The output should look something like:

```
Sending build context to Docker daemon  3.072kB
Step 1/3 : FROM kalibera/rchk:latest
 ---> 35bf1f9ce074
Step 2/3 : ARG DEBIAN_FRONTEND=noninteractive
 ---> Using cache
 ---> 04f31b02b84f
Step 3/3 : RUN apt-get update &&   apt-get install -yq libgmp-dev libmpfr-dev &&   rm -rf /var/lib/apt/lists/*
 ---> Using cache
 ---> 1be53728bdf4
Successfully built 1be53728bdf4
```

Now, the resulting container `1be53728bdf4` is a modified rchk container
with installed `libgmp-dev` and `libmpfr-dev`, so one can check Rmpfr as
follows (omit `-p` on Windows):

```
mkdir -p packages
docker run --rm -v `pwd`/packages:/rchk/packages 1be53728bdf4 Rmpfr
```

## Using the container interactively

In principle, one can also run an interactive shell in the container. This
is useful for troubleshooting the container, perhaps experimenting with
which Ubuntu packages are needed, etc:

```
docker run --rm -it --entrypoint /bin/bash kalibera/rchk:latest
```

The container entry point is in `/container.sh` script, which can be run
also in interactive sessions.

Note that rchk provides already other containers intended for interactive
use (installed automatically using vagrant, the virtual machine is backed by
virtualbox or docker), which have simpler setup for interactive use. Also,
Linux users can install rchk natively on their system.

## Building the container

The container description (Dockerfile) and entrypoint script (container.sh)
are in directory `image/docker`.  To build it, run

```
cd image/docker
docker build .
```
