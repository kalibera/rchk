# Getting Bitcode Files

Building R with CLANG normally is not a problem and it is also mentioned in
[R Installation and Administration]
(http://cran.r-project.org/doc/manuals/r-release/R-admin.html#Clang). 
During such build, one gets the executable R binary (`R.bin`) and shared
libraries in executable format (e.g.  `stats.so`).  But, we need to have a
bitcode version of these files (`R.bin.bc` or`stats.so.bc`).

We need the C compiler (CLANG) to generate bitcode object files and
ultimately link them into the bitcode version of the binary `R.bin.bc` or
`stats.so.bc`.  At the same time, we also need executable binaries, e.g. 
`R.bin`, because the build process needs them to compile packages, install
packages, etc.  Also autotools/configure generate C source files, compile
them and run them when gathering the platform configuration.  So, we need
the C compiler to generate both executable binaries and bitcode files.

## Using a compiler wrapper script

The simple way is to run the C compiler twice for each source file, once
generating the object file in executable format and once in bitcode format. 
This leads indeed to long compile time, but it is easy to set up.  One can
use our modified version of the
[whole-program-llvm](https://github.com/kalibera/whole-program-llvm) wrapper
script by Tristan Ravitch.

```
export CFLAGS="-Wall -g -O0"
export CC=/opt/whole-program-llvm/wllvm
export CXX=/opt/whole-program-llvm/wllvm++
export PATH=/opt/clang+llvm-3.6.1-x86_64-linux-gnu/bin:$PATH
export LLVM_COMPILER=clang

export BITCODE_DIR=`pwd`/bitcode
#rm -rf $BITCODE_DIR

./configure --with-blas --with-lapack --enable-R-static-lib
make
```

As a result, one gets executable binaries (`R.bin`, `stats.so`) and bitcode
versions of the object files (in `bitcode` directory). The binaries have
encoded in their meta-data from which object files they have been linked. To
get actually the bitcode version of the binaries, this meta-data is read and
respective bitcode versions of the object files are linked (by `extract-bc`).

```
export PATH=/home/tomas/work/opt/clang+llvm-3.6.1-x86_64-linux-gnu/bin:$PATH
export BITCODE_DIR=`pwd`/bitcode

~/work/wllvm/whole-program-llvm/extract-bc src/main/R.bin
find . -name *.so -exec /opt/whole-program-llvm/extract-bc {} \;

```

Our version of
[whole-program-llvm](https://github.com/kalibera/whole-program-llvm) adds
support for the `BITCODE_DIR` to the original version, which leaves the
bitcode files in the directories where object files are compiled.  This,
however, does not work when installing R packages, which are built in
temporary directories that are deleted during the build process.

## Getting LLVM

Both the wrapper script and `rchk` itself work with the binary distribution
of [CLANG+LLVM](http://llvm.org/releases/download.html#3.6.1), so one does
not have to build LLVM on the supported platforms.  On unsupported
platforms, such as Fedora 20, one can build the binary distribution of LLVM
using the test-release.sh script, e.g.

```
./test-release.sh -release 3.61 -final -j 32 -triple x86_64-fedora20 -disable-objc
```

The build-time dependencies include `chrpath` and Ocaml modules `ctypes` and
`OUnit2`, and, when run as shown, the build takes very long (multiple
phases, tests). The script downloads the needed sources directly from SVN.

One can also build the LLVM sources manually, which is faster and one can
disable the Ocaml bindings:

1. Download individually these components and unpack them

[llvm core](http://llvm.org/releases/3.6.1/llvm-3.6.1.src.tar.xz), 
[clang/cfe] (http://llvm.org/releases/3.6.1/cfe-3.6.1.src.tar.xz),
[compiler-rt] (http://llvm.org/releases/3.6.1/compiler-rt-3.6.1.src.tar.xz),
[libcxx] (http://llvm.org/releases/3.6.1/libcxx-3.6.1.src.tar.xz),
[libcxxabi] (http://llvm.org/releases/3.6.1/libcxxabi-3.6.1.src.tar.xz)

2. Go to `llvm-3.6.1.src/tools and link` `../../cfe-3.6.1.src` as `clang`. 
Go to `llvm-3.6.1.src/projects` and link `../../compiler-rt-3.6.1.src` as
`compiler-rt` and in the same way link into `tools` also `libcxx` and `libcxxabi`.

3. From a fresh directory, run configure (part of `llvm-3.6.1.src`) and make

```
llvm-3.6.1.src/configure --prefix choose_one \
  --enable-optimized=yes --enable-assertions=no --disable-timestamps --disable-bindings
make
make install
```


## Using LTO and bitcode object files

The double-building of each source file is a waste of resources. 
Conceptually, it should be possible to only generate bitcode object files by
the C compiler and use them in the end to get both executable binaries and
bitcode files.  In principle, this should work as LLVM supports link-time
optimizations (LTO) which already need the bitcode files at link-time.  But,
in practice it is hard to get working.  We were doing this with LLVM 3.4,
but not with the newer versions of LLVM.  Now the wrapper script is
preferred now and more reliable.

The problems with the LTO method are:

1. System utilities like `ld`, `ranlib`, `ar`, `nm` cannot work with LLVM
bitcode given as input.

2. We need the linker to generate both bitcode and executable binary, as the
R build process always needs to run generated binaries.

### System utilities working with bitcode

For (1), one needs to use the [GNU Gold
Linker](http://en.wikipedia.org/wiki/Gold_%28linker%29) instead of the `BFD`
linker, one needs recent `binutils` that support `plugins`, and one needs
the `LLVM gold` plugin. Moreover, one then has to modify environment
variables that influence the R build so that the `LLVM gold plugin` is used
with `ld`, `ranlib`, `ar`, and `nm`. The LLVM gold plugin is not part of the
binary `CLANG+LLVM` distribution, so one needs to build LLVM from sources to
get it.

With Ubuntu 14.04, a usable version of LLVM (including the gold plugin),
CLANG, and binutils is available in the standard Ubuntu package system (see
packages `llvm`, `llvm-3.4`, `llvm-3.4-dev`, `llvm-3.4-runtime`).  One can
also switch the system to the `gold` linker (make `/usr/bin/ld` point to
`/usr/bin/ld.gold`).

With Fedora 20, we built `binutils` from source and then we built LLVM 3.4.2
against these binutils, getting also the LLVM gold plugin.  We have modified
the `PATH` variable to switch to the `gold` linker, we did not have to
switch the whole system to `gold` linker (so one does not need root access).

### Building R, generating bitcode files

For (2), one needs to use LTO (link-time-optimization) and tell the linker
to generate LLVM bitcode in addition to the executables.  With LLVM 3.4, one
can do this using `-Wl,-plugin-opt=also-emit-llvm` (passing option
`also-emit-llvm` to the `LLVM gold plugin`).  This option has been removed
as of LLVM 3.6. We were using bitcode files obtained with LLVM 3.4 for a
while with `rchk` built using LLVM 3.5, and it worked fine.

### More reading

[LLVM Gold Plugin](http://llvm.org/docs/GoldPlugin.html)

[LLVM Release Process](http://llvm.org/docs/ReleaseProcess.html)
