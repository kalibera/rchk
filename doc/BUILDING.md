## Getting Bitcode Files

Building R with CLANG normally is not a problem and it is also mentioned in
[R Installation and Administration]
(http://cran.r-project.org/doc/manuals/r-release/R-admin.html#Clang). 
During such build, however, one does not get the R binary or shared
libraries in the LLVM bitcode format. We need those for the bugfinding
tools. There are two problems:

1. System utilities like `ld`, `ranlib`, `ar`, `nm` cannot work with LLVM bitcode

2. The R build is a complicated process which involves execution of files
previously built (such as the R binary), so we need real executable files to
perform the build.

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
switch the whole system to `gold` linker.

### Building R, generating bitcode files

For (2), one needs to use LTO (link-time-optimization) and tell the linker
to generate LLVM bitcode in addition to the executables.  With LLVM 3.4, one
can do this using `-Wl,-plugin-opt=also-emit-llvm` (passing option
`also-emit-llvm` to the `LLVM gold plugin`).  This option has been removed
as of `LLVM 3.6`.  So far we've been only generating the bitcode files using
LLVM 3.4 and we haven't yet run into problems with that the tools use LLVM
3.5.

### More reading

[LLVM Gold Plugin](http://llvm.org/docs/GoldPlugin.html)
