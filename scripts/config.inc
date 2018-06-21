
# to be customized and sourced by bash before running other scripts

if [ X"`hostname -s`" == Xprg ] ; then

  export WLLVM=/home/tomas/.local/bin
  export LLVM=/usr
  export RCHK=/home/tomas/work/git/rchk

elif [ X"`hostname -s`" == Xr-lnx400 ] ; then

  export WLLVM=/var/scratch/tomas/opt/whole-program-llvm
  export LLVM=/var/scratch/tomas/opt/llvm/clang+llvm-3.8.0-x86_64-fedora23
  export RCHK=/var/scratch/tomas/opt/rchk

elif [ X"`hostname -s`" == Xra ] ; then

  export WLLVM=/home/tomas/.local/bin
  export LLVM=/usr
  export RCHK=/home/tomas/git/rchk

elif [ X"`hostname -s`" == Xvagrant ] ; then

  export WLLVM=/home/vagrant/.local/bin
  export LLVM=/usr/lib/llvm-4.0
  export RCHK=/opt/rchk

else
  echo "Please specify directories in config.inc" >&2
fi
