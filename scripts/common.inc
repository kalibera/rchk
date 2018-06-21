
# checks if environment variables seem to be set fine

function check_config {

  RES=0
  if [ ! -x $WLLVM/wllvm ] ; then
    echo "Please check WLLVM installation ($WLLVM)." >&2
    RES=2
  fi

  if [ ! -x $LLVM/bin/llvm-config ] ; then
    echo "Please check LLVM installation ($LLVM)." >&2
    RES=2
  fi

  if [ ! -r $RCHK/src/bcheck ] ; then
    echo "Please check RCHK installation ($RCHK)." >&2
    RES=2
  fi
  
  return $RES
}

# adds annotations to source file
# the input is a text file of format
# <filename> <sed_command>
#
# the sed commands must not change number of lines
#   they are e.g. adding a comment to a line
#   and they must not conflict
#
# the commands could be run one-by-one using sed
#   but that would be slow; they are instead batched
#   creating a script per file
#
# the input is sorted and made unique
# silently ignores files that do not exist (anymore)

function sed_multifile {

  SEDNOW=/tmp/sednow.$$
  rm -rf $SEDNOW
  sort | uniq | { 
    PREVF="none"
    while read F CMD ; do
      if [ $PREVF = $F ] ; then
        echo "$CMD" >> $SEDNOW
      elif [ $PREVF != none ] ; then
        if [ -r $SEDNOW ] && [ -r $PREVF ] ; then
          sed -i -f $SEDNOW $PREVF
        fi
        echo "$CMD" > $SEDNOW  # overwrite the file
      fi
      PREVF=$F
    done
    if [ -r $SEDNOW ] ; then
      if [ -r $PREVF ] ; then
        sed -i -f $SEDNOW $PREVF
      fi
      rm -f $SEDNOW
    fi
  }
}
