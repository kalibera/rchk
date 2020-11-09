#! /bin/bash

# runs rchk tools on installed R packages (those not included in the R distribution)
#
# the packages have to be installed with cmpconfig.inc included into shell
#   they are placed into packages/lib and their build dirs are kept in packages/build
#
# packages can also be installed using install_package_libs from utils.r, then only their
#   shared libraries are installed (--libs-only) into packages/libsonly
#
# this script is to be run from the R source directory
#
# Usage:
#
#   check_package.sh package_name tool1 tool2 etc
#
# Examples:
#
#   check png package with default tools:   ./check_package.sh png
#   check ggplot2 package with bcheck tool: ./check_package.sh ggplot2 bcheck


if [ ! -r $RCHK/scripts/config.inc ] ; then
  echo "Please set RCHK variables (scripts/config.inc)" >&2
  exit 2
fi

for T in $TOOLS ; do
  if [ ! -x $RCHK/src/$T ] ; then
    echo "Please set RCHK variables (scripts/config.inc) and RCHK installation - cannot find tool $T." >&2
    exit 2
  fi
done

. $RCHK/scripts/common.inc

if ! check_config ; then
  exit 2
fi

if [ ! -r ./src/main/R.bin.bc ] ; then
  echo "This script has to be run from the root of R source directory with bitcode files (e.g. src/main/R.bin.bc)." >&2
  exit 2
fi

. $RCHK/scripts/cmpconfig.inc

PKGDIR=$R_LIBS
PKGARG=$1

if [ "X$PKGARG" != X ] ; then
  PKGDIR=${R_LIBSONLY}/$PKGARG
  if [ ! -d $PKGDIR ] ; then
    PKGDIR=$R_LIBS/$PKGARG
    if [ ! -d $PKGDIR ] ; then
      echo "Cannot find package $PKGARG ($PKGDIR does not exist)." >&2
      exit 2
    fi
  fi
fi

shift 1
if [ X"$*" == X ] ; then
  TOOLS="bcheck maacheck fficheck"
else
  TOOLS="$*"
fi


# extract bitcode if needed

find $PKGDIR -name "*.so" | while read SOF ; do
  if [ ! -r $SOF.bc ] || [ $SOF.bc -nt $SOF ] ; then
    $WLLVM/extract-bc $SOF
  fi
done

# run the tools

for T in $TOOLS ; do
  find $PKGDIR -name "*.bc" | grep -v '\.o\.bc' | while read F ; do
    FOUT=`echo $F | sed -e 's/\.bc$/.'$T'/g'`
    if [ ! -r $FOUT ] || [ $F -nt $FOUT ] || [ ./src/main/R.bin.bc -nt $FOUT ] ; then
      $RCHK/src/$T ./src/main/R.bin.bc $F >$FOUT 2>&1
    fi
  done
done
