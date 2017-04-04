
# to be sourced before building

if [ "X$RCHK" == X ] ; then
  echo "rchk variables (scripts/config) not set." >&2
else

  # these options were tested on Ubuntu 16.04.2
  #   they allow to build most but not all packages

  export CFLAGS="-Wall -g -O0"
  export CXXFLAGS="$CFLAGS -I/usr/include/libcxxabi"
  export CC=$WLLVM/wllvm
  export CXX="$WLLVM/wllvm++ -stdlib=libc++"
  export PATH=$LLVM/bin:$PATH
  export LLVM_COMPILER=clang
  export PKG_BUILD_DIR=`pwd`/packages/build
  export R_LIBS=`pwd`/packages/lib
  export LD="clang++ -stdlib=libc++"
fi
