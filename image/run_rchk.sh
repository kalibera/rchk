#! /bin/bash

# Get the latest revision of R-devel (or revision provided as argument)
#
# If the it has not been checked yet, build it and check it.
# Otherwise, take an older version (by granularity of 1 day), build it and check it.
#
 
CHKDIR=`pwd`
WLLVM=/opt/whole-program-llvm/
LLVM=/opt/clang+llvm-3.6.1-x86_64-linux-gnu
RCHK=/opt/rchk/src

echo "`date`: Started." >&2

# check dependencies

if [ ! -x $WLLVM/wllvm ] ; then
  echo "Please set WLLVM directory." >&2
  exit 2
fi

if [ ! -x $LLVM/bin/clang ] ; then
  echo "Please set LLVM directory." >&2
  exit 2
fi

if [ ! -x $RCHK/bcheck ] ; then
  echo "Please set RCHK directory." >&2
  exit 2
fi

# get R-devel source

if [ ! -d R-devel ] ; then
  svn checkout -q http://svn.r-project.org/R/trunk R-devel || exit 1
fi

cd R-devel || exit 1
SDIR=$CHKDIR/R-devel

# the working copy may be locked due to failed previous run
svn cleanup

REVARG=$1

if [ "X$REVARG" != X ] ; then
  # check revision provided by user

  svn update -r $REVARG -q || exit 1
  
else
  # get latest R-devel

  svn update -q || exit 1
fi

REV=`svn info | grep 'Last Changed Rev' | cut -d':' -f2 | cut -d' ' -f2` 
if ! expr "$REV" : '[0-9]\+' >/dev/null  ; then
  echo "Invalid svn revision $REV" >&2
  exit 1
fi

echo "`date`: Current/requested svn revision is $REV." >&2

# check if it has already been checked

mkdir -p $CHKDIR/res
RDIR=$CHKDIR/res/$REV
if [ -d $RDIR ] ; then
  echo "`date`: Revision $REV has already been checked, looking for some older revision to check." >&2
  
  while [ -d $RDIR ] ; do
    CDATE=`svn info | grep 'Last Changed Date' | cut -d':' -f2`
    PDATE=`date --date "$CDATE yesterday" +%Y-%m-%d`  # the day before the current revision
    svn update -q -r '{'$PDATE'}' || exit 1
    REV=`svn info | grep 'Last Changed Rev' | cut -d':' -f2 | cut -d' ' -f2` 
    RDIR=$CHKDIR/res/$REV
  done
  
  echo "`date`: Will check older revision $REV." >&2
fi

if [ -d $RDIR ] ; then
  echo "`date`: Did not get a version to check $REV." >&2
  exit 3
fi

mkdir $RDIR || exit 1

# get recommended packages

cd $SDIR || exit 1
while ! ./tools/rsync-recommended >/dev/null ; do
  # the server is sometimes overloaded
  sleep 10s
done

# build R

echo "`date`: Building R $REV." >&2

BDIR=/tmp/build.$$
mkdir $BDIR || exit 1
cd $BDIR || exit 1

export CFLAGS="-Wall -g -O0"
export CC=$WLLVM/wllvm
export CXX=$WLLVM/wllvm++
export PATH=$LLVM/bin:$PATH
export LLVM_COMPILER=clang

export BITCODE_DIR=`pwd`/bitcode

$SDIR/configure --with-blas --with-lapack --enable-R-static-lib >$RDIR/configure.out 2>&1
make >$RDIR/make.out 2>&1

if [ ! -x ./src/main/R.bin ] ; then
  echo "`date`: Build failed to produce R binary." >&2
  exit 1
fi

$WLLVM/extract-bc src/main/R.bin
find . -name *.so -exec $WLLVM/extract-bc {} \;
rm -rf $BITCODE_DIR

if [ ! -r ./src/main/R.bin.bc ] ; then
  echo "`date`: Build failed to produce R bitcode." >&2
  exit 1
fi

# check it

echo "`date`: Checking R $REV." >&2

for TOOL in bcheck maacheck ; do
  mkdir -p $RDIR/src/main
  $RCHK/$TOOL ./src/main/R.bin.bc >$RDIR/src/main/R.bin.bc.$TOOL 2>$RDIR/src/main/R.bin.bc.${TOOL}_err
  
  find . -name "*.bc" | grep -v R.bin.bc | while read F ; do
    mkdir -p `dirname $RDIR/$F`
    $RCHK/$TOOL ./src/main/R.bin.bc $F >$RDIR/$F.$TOOL 2>$RDIR/$F.${TOOL}_err
  done
done

# filter the outputs

cd $RDIR
  # remove randomly generated parts of (anyway unavailable) temporary directories
find . -type f -exec sed -i 's/\/tmp.*R.INSTALL[^\/]*/<R.INSTALL>/g' {} \;
  # remove the directory prefix (SDIR) from path
QUOTED_SDIR=`echo $SDIR/ | sed -e 's/[\/&]/\\\&/g'`
find . -type f -exec sed -i 's/'"${QUOTED_SDIR}"'//g' {} \; 


echo "`date`: Done with $REV." >&2
rm -rf $BDIR

echo "`date`: Done." >&2
