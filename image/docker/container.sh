#! /bin/bash

# Entrypoint / command for Docker container
#
# Builds shared library of a given R package, installing R packages needed
# by that package.  Extracts LLVM bitcode for the shared library and runs
# rchk checking tools on it.  Prints the outputs.  On request, installs also
# Ubuntu packages before building the package.
#
# Outputs are kept in /rchk/packages (libsonly contains shared libraries of
# packages and check results, libs contains full package installations,
# build contains temporary build directories with LLVM bitcode available for
# extraction).  By default, /rchk/packages is lost when the container stops,
# but one can mount it using "-v".  When mounted, the container will, after
# installing Ubuntu packages, switch to a new user matching the UID of
# /rchk/packages (so that it does not unnecessarily create files owned by
# /root on the host machine).
#
# (running the checks and printing the outputs is similar to the Singularity
# container)
#

if test -z "$1"; then
  echo "Usage: [--install-deb \"<ubuntu-packages>\"] <package_tarball_full_path>"
  echo "       [--install-deb \"<ubuntu-packages>\"] <package_name>"
  echo "       [--install-deb \"<ubuntu-packages>\"] R"
  exit 1
fi

CMD=$1

# install deb packages
if test "$CMD" = "--install-deb" ; then
  shift
  DEBS="$1"
  shift
  EFUID=`id -u`
  if test "$EFUID" = "0" ; then
    apt-get update
    apt-get install -y $DEBS
  else
    echo "Need to be root to install Ubuntu packages." >&2
    exit 2
  fi
fi

# become the user that owns $RCHK/packages
EFUID=`id -u`
if test "$EFUID" = "0" ; then
  PKUSER=`stat -c '%U' $RCHK/packages`
  if test "$PKUSER" = "UNKNOWN" ; then
    PKUID=`stat -c '%u' $RCHK/packages`
    useradd -c"rchk docker user" -s /bin/bash -d /rchk -u $PKUID rchk
    PKUSER="rchk"
  fi
  if test "$PKUSER" != "root" ; then
    # re-invoke the script under regular user to do the rest of the
    # work, preserving environment variables
    sudo -u $PKUSER env "PATH=$PATH" /bin/bash /container.sh $* 
    exit
  fi
fi

# figure out R package name and tarball path
if test -n "$2"; then
  PKG_NAME="$1"
  PKG_PATH="$2"
else
  PKG_PATH="$1"
  if echo "$1" | grep -q "tar\.gz$" ; then
    PKG_NAME=`echo $1 | sed -e 's/.*\///g' | sed -e 's/\(.*\)_.*/\1/g'`
  else
    PKG_NAME="$1"
  fi
fi

if test "$PKG_NAME" = R ; then
  shift
  R --vanilla $*
  exit
fi

# run the checks
mkdir -p $R_LIBS $R_LIBSONLY $PKG_BUILD_DIR
echo "source(\"${RCHK}/scripts/utils.r\"); install_package_libs(\"${PKG_PATH}\")"
echo "source(\"${RCHK}/scripts/utils.r\"); install_package_libs(\"${PKG_PATH}\")" | \
  R --vanilla --no-echo
  
find "${RCHK}/packages/libsonly/${PKG_NAME}/libs" \( -name "*.bcheck" -o -name "*.maacheck" -o -name "*.fficheck" \) -exec rm {} \;
env WLLVM_BC_STORE="$RCHK/bcstore" check_package.sh "$PKG_NAME"

# print results
echo
find "${RCHK}/packages/libsonly/${PKG_NAME}/libs" \( -name "*.bcheck" -o -name "*.maacheck" -o -name "*.fficheck" \) -exec cat {} \;

# print version information
echo
echo "Rchk version: "`cat ${RCHK}/git_version`
echo "R version: "`echo 'cat(paste(R.version$svn, R.version$version.string, sep="/"))'  | R --vanilla --no-echo`
echo "LLVM version: "`llvm-config --version`
exit
