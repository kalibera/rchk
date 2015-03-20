
This project consists of several bugfinding tools that look for memory
protection errors in the C source code of the [GNU
R](http://www.r-project.org/) core and packages.

On input, these tools need the LLVM bitcode of the R binary. To check an R
package, the tool also needs the bitcode of the shared library of that
package. To get these bitcode files one needs to build R with the CLANG
compiler, with LTO (link time optimizations) enabled but all other compiler
optimizations disabled (-O0), and telling CLANG to emit the LLVM bitcode in
addition to executable files. Detailed instructions (tested on Ubuntu 14.04 and
Fedora Core 20) are available [here](todo).

The tools need [LLVM 3.5.0](http://llvm.org/releases/download.html) to build
(one can download one of the Clang pre-built binaries). One just needs to
modify the LLVM installation path in `src/Makefile` and customize the
maximum number of checking states. Then the tools are build using `make`.

## Marking Allocating Functions

To use the `PROTECT` macros correctly in R one needs to know which functions
may allocate. This is, however, often difficult to find out. The `csfpcheck`
tool does the job automatically:

`csfpcheck ./src/main/R.bin.bc >lines`

produces a list of source files with line numbers that have allocation
(ignore error messages about some functions having too many states, a
conservative detection is used for those).  To have add a comment at the end of
each such line, run this

`cat lines | while read F L ; do echo "sed -i '$L,$L"'s/$/ \/* GC *\//g'\'" $F" ; done | bash`

The detection of allocators can have false positives, e.g. a call may not in
fact allocate in the given context, but the tool does not know and marks the
call as allocating. However, unless there is a bug in the tool, all calls
possibly allocating will be marked. For example, in

```
SEXP attribute_hidden do_enablejit(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    int old = R_jit_enabled, new;
    checkArity(op, args);
    new = asInteger(CAR(args)); /* GC */
    if (new > 0)
        loadCompilerNamespace(); /* GC */
    R_jit_enabled = new;
    return ScalarInteger(old); /* GC */
}
```

the allocation of `ScalarInteger` is correct and obvious to any developer,
the allocation of loadCompilerNamespace is correct but perhaps less obvious,
and the allocation in `asInteger` is surprising.  But it is correct!  The
function will allocate when displaying a warning, e.g.  when the conversion
loses accuracy or produces NAs.

A patch for the R-devel version 67741 with the generated "GC" comments by
the tool is available in the `examples` directory for convenience. To get
the annotated sources, one just needs to

```
cd examples
svn checkout -r 67741 http://svn.r-project.org/R/trunk
cd trunk
zcat ../csfpcheck.67741.patch.gz | patch -p0
```

