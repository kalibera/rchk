
# to be sourced before building

if [ "X$RCHK" == X ] ; then
  echo "rchk variables (scripts/config) not set." >&2
else
  export CFLAGS="-Wall -g -O0"
  export CC=$WLLVM/wllvm
  export CXX=$WLLVM/wllvm++
  export PATH=$LLVM/bin:$PATH
  export LLVM_COMPILER=clang
  export PKG_BUILD_DIR=`pwd`/packages/build
  export R_LIBS=`pwd`/packages/lib
fi
