#! /bin/bash

# rebuilds R from scratch
# to be run from freshly unpacked R source directory

if [ ! -r $RCHK/scripts/config.inc ] ; then
  echo "Please set RCHK variables (scripts/config.inc)" >&2
  exit 2
fi

. $RCHK/scripts/common.inc

if ! check_config ; then
  exit 2
fi

. $RCHK/scripts/cmpconfig.inc

if [ ! -r ./src/main/Rmain.c ] ; then
  echo "This script has to be run from the root of R source directory (e.g. have file src/main/Rmain.c)." >&2
  exit 2
fi

mkdir -p $R_LIBS

 # patch R to support PKG_BUILD_DIR
 # (no longer needed, now part of R)

 if ! grep -q 'PKG_BUILD_DIR' src/library/tools/R/install.R ; then
  patch -p0 < $RCHK/scripts/installr_build_dir.diff || exit 1  
fi

# build R creating bitcode files for modules (.o files)

./tools/rsync-recommended
./configure --with-blas=no --with-lapack=no --enable-R-static-lib
make

# create bitcode for R binary (R.bin) and shared objects (.so files)

$WLLVM/extract-bc src/main/R.bin
find . -name *.so -exec $WLLVM/extract-bc {} \;
