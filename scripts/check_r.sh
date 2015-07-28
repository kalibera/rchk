#! /bin/bash

# runs rchk tools on R and package included in the R distribution
# to be run from R source directory, after the bitcode files have been created (e.g. using build_r.sh)
#

# tools to run can be specified as arguments

if [ X"$*" == X ] ; then
  TOOLS="bcheck maacheck"
else
  TOOLS="$*"
fi

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

# run the tools

for T in $TOOLS ; do
  if [ ! -r ./src/main/R.bin.$T ] || [ ./src/main/R.bin.bc -nt ./src/main/R.bin.$T ] ; then
    $RCHK/src/$T ./src/main/R.bin.bc >./src/main/R.bin.$T 2>&1
  fi
  
  find . -name "*.bc" | grep -v R.bin.bc | grep -v '\.o\.bc' | grep -v '\.svn' | grep -v '^./packages' | while read F ; do
    FOUT=`echo $F | sed -e 's/\.bc$/.'$T'/g'`
    if [ ! -r $FOUT ] || [ $F -nt $FOUT ] || [ ./src/main/R.bin.bc -nt $FOUT ] ; then
      $RCHK/src/$T ./src/main/R.bin.bc $F >$FOUT 2>&1
    fi
  done
done
