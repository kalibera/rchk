#! /bin/bash

# Convert checking results in "res" into a form usable for presentation at
# github
#
# 1. clone the github repo
# 2. delete its content
# 3. for each revision checked in "res"
#   add the revision to the github repo
#     converting checker outputs to .md files with hyperlinks
#   commit that revision

# modify this 
PREPO="${RCHK_PUBLISH_REPO:-___PUBLISH_REPO___}"

if expr "$PREPO" : '^___' >/dev/null ; then 
  echo "Publish git repository must be set." >&2
  exit 1
fi

# ---------------

WDIR=`pwd`
RDIR=$WDIR/res
IDIR=$WDIR/github-R-devel
IDXFILE=$WDIR/github-R-devel-index
PDIR=/tmp/rdevchk

echo "`date`: Started." >&2

# get the index repository

if [ ! -d $IDIR ] ; then
  git clone https://github.com/wch/r-source $IDIR -q || exit 1
fi

cd $IDIR || exit 1
git pull origin trunk -q || exit 1

# compute the index

git log | egrep '^commit|git-svn-id' | sed 'N;s/\n/ /' | sed -e 's/commit \([^ \t]*\).*git-svn-id:.*trunk@\([0-9]\+\) .*/\2 \1/g' > $IDXFILE

if [ ! -r $IDXFILE ] ; then
  echo "Failed to create index file" >&2
  exit 1
fi

    # convert SVN version number to git version id
function svn_revision_to_git {
  SVNVER=$1
  cat $IDXFILE | grep "^$SVNVER " -m 1 | cut -d' ' -f2
}

echo "`date`: Updated git-svn index." >&2

# clone the presentation repository

rm -rf $PDIR
git clone $PREPO $PDIR -q || exit 1
cd $PDIR || exit 1

# delete its content

rm -rf $PDIR/*
rm -rf .git
git init -q

cat <<EOF >README.md
# rdevchk
Generated on `date` using [rchk](https://github.com/kalibera/rchk).

[rchk](https://github.com/kalibera/rchk) is a set of offline bug-finding
tools that look for PROTECT bugs in the C code of GNU-R and R packages. 
This site presents results from the tool run periodically on recent
versions of GNU-R ([R-devel](http://svn.r-project.org/R/trunk/)).

The directory structure of the presented results corresponds to where
binaries and shared libraries are left after building R.  Hence, bug reports
for the core are in [src/main](src/main).  *Rchk* contains several bug
finding tools; the most interesting is *bcheck*, hence the most interesting
results for the R binary are [src/main/R.bin.bcheck.md](src/main/R.bin.bcheck.md).

Each report has a hyperlinked markdown version (suffix \`.md\`) suitable for
viewing online and a raw ascii version (no suffix).

The commits into this automatically generated git repository correspond to
commits to the R subversion repository, preserving the original commit
messages.  One can thus view reports also for older SVN revisions.  Still,
not all SVN revisions are checked individually.

Most errors currently reported for GNU-R are (hopefully still) false alarms,
because we have fixed the real errors.  This site thus also shows errors
that were [possibly introduced](possibly_broken.md) between two consecutive
checked versions and errors that were [possibly fixed](possibly_fixed.md). 
The hypelinks go to where the suspected errors are (new version for
introduced errors, old version for fixed errors).

EOF

git add .
git commit -m "Initial commit" -q
git remote add origin $PREPO
git push -u --force origin master -q

echo "`date`: Re-initialized presentation repository." >&2

# fill in the presentation repo

RDEV=$WDIR/R-devel-publish
if [ ! -d $RDEV ] ; then
  svn checkout -q http://svn.r-project.org/R/trunk $RDEV || exit 1
fi

cd $RDEV || exit 1
svn cleanup

SDIR=/tmp/rsrc
rm -rf $SDIR
mkdir -p $SDIR || exit 1

touch $PDIR/possibly_fixed
touch $PDIR/possibly_broken
touch $PDIR/possibly_fixed.md
touch $PDIR/possibly_broken.md

cd $RDIR || exit 1
ls -1 | sort -n | { PREVV="none" ; while read V ; do

  if [ ! -d $V ] ; then
    continue
  fi
  
  cd $RDIR/$V
  
  # convert SVN version $V to git version $GV
  GV=`svn_revision_to_git $V`
  
  # copy error messages with hyperlinks to R sources
  find . -name *check | while read F ; do
    mkdir -p `dirname $PDIR/$F`
    
    # do not introduce empty files
    if [ ! -s $F ] && [ ! -r $PDIR/$F.md ] ; then
      continue
    fi 
    
    PFNAME=`echo $PDIR/$F | sed -e 's/\.bc\././g'` # remove the ".bc" from already long file names
    if [ "X$GV" != X ] ; then
      # have git version, so annotate the files
      #  change references to files to hyperlinks
      #  add line breaks
      #  highlight function name to make it more readable
      
      sed -e 's/[ \t]\([^ \t<>]\+\):\([0-9]\+\)/ [\1:\2](https:\/\/github.com\/wch\/r-source\/blob\/'$GV'\/\1\/#L\2)/g' <$F | \
      	sed -e 's/^Function [^ \t]\+$/__\0__/g' | \
      	sed -e 's/$/  /g' >$PFNAME.md
    fi
    
    # also copy the original file
    cat <$F >$PFNAME
  done
  
  # create a list of possibly introduced errors

  cp -Rpdf $RDEV $SDIR/$V || exit 1
  cd $SDIR/$V
  svn cleanup
  svn update -r $V -q || exit 1
  
    # extract error messages and create sed commands to annotate sources

  SEDSCR=/tmp/sed.$$
  rm -rf $SEDSCR
  find $RDIR/$V -name *check -exec cat {} \; | grep '[ \t].*:[0-9]\+$' | grep -v R.INSTALL | \
    sed -e 's/^[ \t]*\(.*\)[ \t]\([^ \t<>]\+\):\([0-9]\+\)$/\2 \3,\3s\/$\/|RCHKERR|\1\/g/g' | \
    sort | uniq > $SEDSCR

    # filename sed_command_to_annotate
    # so we have to split this into individual sed scripts and run sed

  SEDNOW=/tmp/sednow.$$
  rm -rf $SEDNOW
  cat $SEDSCR | { 
    PREVF="none"
    while read F CMD ; do
      if [ $PREVF = $F ] ; then
        echo "$CMD" >> $SEDNOW
      elif [ $PREVF != none ] ; then
        if [ -r $SEDNOW ] ; then
          sed -i -f $SEDNOW $PREVF
        fi
        echo "$CMD" > $SEDNOW  # overwrite the file
      fi
      PREVF=$F
    done
    if [ -r $SEDNOW ] ; then
      sed -i -f $SEDNOW $PREVF
      rm -f $SEDNOW
    fi
  }
  rm -f $SEDSCR
  
    # now the sources are annotated

  if [ $PREVV != none ] ; then

    BROKEN=/tmp/broken.$$
    rm -rf $BROKEN
    touch $BROKEN
    FIXED=/tmp/fixed.$$
    rm -rf $FIXED
    touch $FIXED
    
    find . -type f | grep -v '\.svn' | cut -d/ -f 2- | while read F ; do
      if [ ! -r $SDIR/$PREVV/$F ] ; then
        grep -n RCHKERR $F | tr -t ':' ' ' | while read N REST ; do
          echo "$F:$N" >> $BROKEN
          echo "$REST" | sed -e 's/|RCHKERR|/\n  /g' | tail -n +2 >> $BROKEN
        done
      else
        diff --unchanged-line-format="" --old-line-format="" --new-line-format="%dn %L" $SDIR/$PREVV/$F $F | grep RCHKERR | while read N REST ; do
          echo "$F:$N" >> $BROKEN
          echo "$REST" | sed -e 's/|RCHKERR|/\n  /g' | tail -n +2 >> $BROKEN
        done
        diff --unchanged-line-format="" --new-line-format="" --old-line-format="%dn %L" $SDIR/$PREVV/$F $F | grep RCHKERR | while read N REST ; do
          echo "$F:$N" >> $FIXED
          echo "$REST" | sed -e 's/|RCHKERR|/\n  /g' | tail -n +2 >> $FIXED
        done
      fi
      # we do not report bugs in removed files as fixed
    done


      # write the list of broken/fixed errors into a file
      #   it is better to keep full history there, the diffs are not practical for this

    function print_code_changes {
      if [ `expr $V - $PREVV` -lt 4 ] ; then
        echo "Changes between versions $PREVV and $V:"
        svn log -r $PREVV:$V http://svn.r-project.org/R/trunk | sed -e 's/^/  /g'
        echo
      else
        echo "Many changes between versions $PREVV and $V, the last one is:"
        svn log -r $PREVV http://svn.r-project.org/R/trunk | sed -e 's/^/  /g'
        echo
      fi    
    }

      # creates a hyperlinked version of the change-list
    function annotate_changelist {
      ANNV=$1
      
      GITV=`svn_revision_to_git $ANNV`
      
	# highlight lines that are not indented in the original
	# change lines of the form file:number to hyperlinks
	# add hard linebreaks where breaks are in the original
      sed -e 's/^[^ \t]\+.*/__\0__/g' | \
        sed -e 's/^    \(.*\)/\&nbsp;\&nbsp;\&nbsp;\&nbsp;\0/g' | \
        sed -e 's/^  \(.*\)/\&nbsp;\&nbsp;\0/g' | \
        sed -e 's/\([^ \t]\+\):\([0-9]\+\)$/[\1:\2 ('$ANNV')](https:\/\/github.com\/wch\/r-source\/blob\/'$GITV'\/\1\/#L\2)/g' | \
        sed -e 's/$/  /g'
    }
    
      # adds new reports on top of all reports
    function print_bug_changes {
      NREPORTS=$1
      REPORTS=$2
      KIND=$3
      ANNV=$4 # version for linking the sources

      if [ -s $NREPORTS ] ; then
      
	# update plaintext output      
        mv $REPORTS $REPORTS.old
        rm -f $REPORTS.cur

        print_code_changes >> $REPORTS.cur
        echo "Possibly $KIND errors between versions $PREVV and $V:" >> $REPORTS.cur
        cat $NREPORTS | sed -e 's/^/  /g' >> $REPORTS.cur
        echo >> $REPORTS.cur
        echo >> $REPORTS.cur
    
        cat $REPORTS.cur $REPORTS.old >> $REPORTS
        rm -f $REPORTS.old
        
        # update hyperlinked output
        mv $REPORTS.md $REPORTS.oldmd
        annotate_changelist $ANNV <$REPORTS.cur >$REPORTS.curmd
        cat $REPORTS.curmd $REPORTS.oldmd >> $REPORTS.md
        rm -f $REPORTS.cur $REPORTS.oldmd $REPORTS.curmd
        
      fi
    }
    
    print_bug_changes $BROKEN $PDIR/possibly_broken "introduced" $V
    print_bug_changes $FIXED $PDIR/possibly_fixed "fixed" $PREVV
    rm -f $BROKEN $FIXED
  fi

  cd $PDIR  
  git add .
  
  # get the commit log from svn
  svn log -l 1 -r $V http://svn.r-project.org/R/trunk | grep -v '^-*$' | git commit -F - -q
  
  echo "`date`: Committed version $V." >&2

  # delete some old versions to save disk space
  
  ls -1 $SDIR | grep -v '^'$V'$' | while read D ; do
    rm -rf $SDIR/$D
  done

  PREVV=$V
  cd $RDIR
done
}

rm -rf $SDIR

cd $PDIR
git push origin master -q || exit 1
echo "`date`: Pushed changes to presentation repo." >&2

rm -rf $PDIR

echo "`date`: Done." >&2
