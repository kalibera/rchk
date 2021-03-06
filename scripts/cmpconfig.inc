
# to be sourced before building

if [ "X$RCHK" == X ] ; then
  echo "rchk variables (scripts/config) not set." >&2
else

  # these options were tested on Ubuntu 16.04.2
  #   they allow to build most but not all packages

  # rJava cannot be checked without -fPIC in CFLAGS, but we need to set also
  # CPICFLAGS to -fPIC, because otherwise it on some systems becomes -fpic,
  # and then we get conficting PIC levels (with which MonetDBLite fails)

  export CPICFLAGS="-fPIC"
  export CFLAGS="-Wall -g -O0 $CPICFLAGS"

  export CXXFLAGS="$CFLAGS -I/usr/include/libcxxabi"
  export CC=$WLLVM/wllvm
  export CXX="$WLLVM/wllvm++ -stdlib=libc++"
  export PATH=$LLVM/bin:$PATH
  export LLVM_COMPILER=clang
  export PKG_BUILD_DIR=`pwd`/packages/build
  export R_LIBS=`pwd`/packages/lib
  export R_LIBSONLY=`pwd`/packages/libsonly
  export LD="clang++ -stdlib=libc++"

  # it is too easy to forget and then not find packages to check
  mkdir -p $R_LIBS
fi
