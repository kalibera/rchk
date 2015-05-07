#! /bin/bash

# annotates R sources with /* GC */ comments

# where the R is built and the ".bc" files are
RHOME=$1

RBC=$RHOME/src/main/R.bin.bc
CHECK=./csfpcheck

if [ ! -r $RBC ] ; then
  echo "Cannot read main file $RBC." >&2
  exit 2
fi

TMPF=annotations.$$

echo -n "$RBC..."
$CHECK $RBC >$TMPF
echo " Done."

find $RHOME -name "*.bc" | grep -v "a.out.bc" | grep -v "R.bin.bc"  | while read F ; do
  echo -n "$F..."
  $CHECK $RBC $F >>$TMPF
  echo " Done."
done

TMPFS=sorted_$TMPF
TMPFE=script_$TMPF

sort $TMPF > $TMPFS
cat $TMPFS | while read F L ; do 
  echo "sed -i '$L,$L"'s/$/ \/* GC *\//g'\'" $F"
done >$TMPFE

echo "Generated annotations into $TMPFS and the script into $TMPFE."
