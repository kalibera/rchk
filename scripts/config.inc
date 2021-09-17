
# to be customized and sourced by bash before running other scripts

if [ X"`hostname -s`" == Xprg ] ; then

  export WLLVM=/usr/local/bin
  export LLVM=/usr
  export RCHK=/home/tomas/git/rchk

elif [ X"`hostname -s`" == Xr-lnx400 -o X"`hostname -s`" == Xr-lnx404 ] ; then

  export WLLVM=/space/tkalibera/.local/bin
  export LLVM=/usr
  export RCHK=/space/tkalibera/git/rchk

elif [ X"`hostname -s`" == Xra ] ; then

  export WLLVM=/usr/local/bin
  export LLVM=/usr
  export RCHK=/home/tomas/git/rchk

elif [ X"`hostname -s`" == Xvagrant ] ; then

  export WLLVM=/home/vagrant/.local/bin
  export LLVM=/usr
  export RCHK=/opt/rchk

elif [ X"`hostname -s`" == Xpod ] ; then

  export WLLVM=/usr/local/bin
  export LLVM=/usr/lib/llvm-8
  export RCHK=/home/tomas/work/git/rchk

else
  echo "Please specify directories in config.inc" >&2
fi
