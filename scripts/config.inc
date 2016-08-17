
# to be customized and sourced by bash before running other scripts

if [ X"`hostname -s`" == Xprg ] ; then

  export WLLVM=/home/tomas/work/git/whole-program-llvm
  export LLVM=/home/tomas/work/opt/clang+llvm-3.8.1-x86_64-linux-gnu-ubuntu-16.04
  export RCHK=/home/tomas/work/git/rchk
  
elif [ X"`hostname -s`" == Xr-lnx400 ] ; then

  export WLLVM=/var/scratch/tomas/opt/whole-program-llvm
  export LLVM=/var/scratch/tomas/opt/llvm/clang+llvm-3.8.0-x86_64-fedora23
  export RCHK=/var/scratch/tomas/opt/rchk

else
  echo "Please specify directories in config.inc" >&2
fi
