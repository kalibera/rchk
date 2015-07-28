#! /bin/bash

# annotates source code of R and packages included in the R distribution
# to be run from R source directory, after the bitcode files have been created (e.g. using build_r.sh)
# it automatically runs tools that generate the annotations (e.g. csfpcheck)

# careful - this modifies source files in current directory

if [ ! -r $RCHK/scripts/config.inc ] ; then
  echo "Please set RCHK variables (scripts/config.inc)" >&2
  exit 2
fi

. $RCHK/scripts/common.inc

if ! check_config ; then
  exit 2
fi

if [ ! -r ./src/main/R.bin.bc ] ; then
  echo "This script has to be run from the root of R source directory with bitcode files (e.g. src/main/R.bin.bc)." >&2
  exit 2
fi

# run the csfpcheck tool

if grep -q '/\* GC \*/' src/main/eval.c ; then
  echo "The GC annotations seem already to have been applied (see e.g. eval.c)" >&2
  exit 2
fi

$RCHK/scripts/check_r.sh csfpcheck

  # csfpcheck output is "filename line_no"
  # by the sed command below it is converted to
  #   "filename sed_command_to_add_annotation"
find . -name *.csfpcheck -exec cat {} \; | \
  sed -e 's/\([^ \t]\+\) \(.*\)/\1 \2,\2s\/$\/\\\\\/* GC *\\\\\/\/g/g' | \
  sed_multifile
