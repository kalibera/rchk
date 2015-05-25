## Detecting Allocating Functions

The key to the correct use of the `PROTECT` macros is knowing which
functions may allocate and thus trigger garbage collection.  Except for few
obvious cases this is hard to find out.

The `csfpcheck` tool does the job automatically:

`csfpcheck src/main/R.bin.bc >lines`

produces a list of source files with line numbers where allocation may
happen. This script puts a comment at the end of each such line:

`cat lines | while read F L ; do echo "sed -i '$L,$L"'s/$/ \/* GC *\//g'\'" $F" ; done | bash`

There is a shell script `gcannotate.sh` which generates annotations using `csfpcheck` for
the core R and packages included in the R distribution. The script generates
two textual files, a list of annotations and a script to annotate the
sources:

`gcannotate.sh path_to_R`

where `path_to_R` includes the build three of R with the bitcode files, e.g.
`path_to_R/src/main/R.bin.bc`. The generated script is just a sequence of
sed in-place text insertions.


The tool errs on the safe side, which is saying that a function may
allocate.  It may be that in fact the function won't allocate for the given
inputs.  The tool, however, takes some function arguments into account: e.g. 
it can detect that `getAttrib(,R_ClassSymbol)` will not allocate, but
`getAttrib(,R_NamesSymbol)` might.  In the following snippet from `eval.c`,

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

`ScalarInteger` indeed allocates and it would be obvious to any developer,
`loadCompilerNamespace` really allocates but perhaps it is less obvious, and
the detected allocation in `asInteger` is surprising.  But it is correct! 
The function will allocate when displaying a warning, e.g.  when the
conversion loses accuracy or produces NAs.

A patch for the R-devel version 67741 with the generated "GC" comments by
the tool is available in the `examples` directory for convenience. To get
the annotated sources, one just needs to

```
cd examples
svn checkout -r 67741 http://svn.r-project.org/R/trunk
cd trunk
zcat ../csfpcheck.67741.patch.gz | patch -p0
```

Having such annotated sources may be useful when manually looking for
protection bugs or when checking the reports of other bug finding tools, and
perhaps also when diagnosing errors found by [runtime
checking](http://cran.r-project.org/doc/manuals/r-release/R-exts.html#Checking-memory-access)
(valgrind, gctorture, the barrier, etc).

Note that sometimes the tool can give surprising annotations. A particular
case is that it conservatively assumes that any call to external code may
allocate from the R heap, which is often not the case.

## Detecting Multiple-Allocating-Arguments Bugs

The `maacheck` tools for a very special but common bug pattern, like here:

```
PROTECT( expr = lang5(install("gsub"), ScalarLogical(1), pattern, replacement, x) );
```

I've learned about these bugs from Radford Neal who fixed a number of them
manually in [pqR](http://www.pqr-project.org/) and I've ported those fixes
to R-devel.  In the example above, `ScalarLogical` indeed allocates and
returns the allocated object.  `install` may also allocate and return the
allocated object, but it would also protect it implicitly by putting it into the symbol
table.  The `lang5` function is callee-protect, it protects its arguments. 
If `ScalarLogical` is executed before `install`, `install` may allocate and
kill the object allocated by `ScalarLogical`.  In C, the order of execution
of `install` and `ScalarLogical` is undefined, so this can happen.

The tool looks for similar errors, not necessarily only those including
`install`, even though such are most common. To check the `RGtk2` package
from CRAN, run

```
maacheck src/main/R.bin.bc RGtk2.Rcheck/00_pkg_src/RGtk2/src/RGtk2.so.bc
```

This will produce a number of suspected errors, including

```
WARNING Suspicious call (two or more unprotected arguments) to retByVal
  at S_pango_layout_get_size RGtk2.Rcheck/00_pkg_src/RGtk2/src/pangoFuncs.c:3596
```

This is a real error:

```
_result = retByVal(_result, "width", asRInteger(width), "height", asRInteger(height), NULL);
```

`asRInteger` is just an alias for `ScalarInteger`, so if the call to
`asRInteger` that executes second allocates, it will kill the object
allocated by the previous call to `asRInteger`.

The `maacheck` by design can have false alarms. It looks for multiple
allocating expressions passed to a function, where at least one of the
expressions may return a newly allocated object.  The tool, however, does
not detect if a function implicitly protects an object before returning it
(e.g.  `install` or `getPrimitive` are such functions).  The `install` calls
are built-in exceptions in the tool.  Experience with the tool so far
suggests that it has very few false alarms in practice (and much less than
the following tools).  It should be said that running the tools on recent
versions of the R core is not a good way to assess the rate of false alarms,
because we're fixing the errors as we find them.

`ueacheck` is a more general variant of `maacheck`. It looks also for
errors where some of the allocating expressions is given as local variable:

```
SEXP pa = allocVector(INTSXP, 1);
INTEGER(pa)[0] = ci->pid;
setAttrib(rv, install("pid"), pa);
```

In the example, `install` may allocate and kill the object pointed to from
`pa`, allocated earlier by `allocVector`.  The tool is internally based on a
number of heuristics and has more false alarms than `maacheck` (but can also
detect more errors).  A typical example of a false alarm with this tool is
implicit protection, such as linking a newly allocated object to an object
already protected: the tool does not detect this an may think the newly
allocated object is still ``fresh''.

## Detecting Error Functions

Non-local returns (`setjmp`/`longjmp`) are used in R to handle errors. It is
a good practice to annotate functions that never return normally (that is
via the `return` statement or through reaching the end of a `void`
function).  The annotation helps the compiler generate better (faster) code
and more precise warnings.  In R, such functions are annotated using
`NORETURN` macro, but this macro is sometimes forgotten.  The `errcheck`
tool detects functions that should be marked `NORETURN` but are not, as well
as functions that are marked but shouldn't (yet I didn't find such case). 
E.g. checking CRAN package `mets`:

```
errcheck src/main/R.bin.bc mets.Rcheck/00_pkg_src/mets/src/mets.so.bc
```

reports also

```
UNMARKED error function void arma::arma_stop<std::string>(std::string const&) RcppArmadillo/include/armadillo:94
```

this is a correct report, this function should be marked `NORETURN`.

This tool should be precise by design.  The tool is a byproduct of the
detection of error functions that had to be implemented for the memory
checking tools: error paths often include allocation (at least allocating
the error message), and they can easily be the only allocation a function
does.  So, by design, all error functions (and error paths within a
function) have been excluded from the checking for memory errors.

## Detecting Protection Stack Imbalance

A function has pointer protection stack imbalance if the pointer protection
stack depth at function exit is not the same as at function entry. This is
sometimes the correct/intended behavior (e.g.  in generated parsers), but
commonly it is an error. Pointer protection imbalance is sometimes checked
and reported at runtime, but with the `bcheck` tool, one can detect it also
in rarely executed branches (and without tests with a good coverage):

```
PROTECT(sa = coerceVector(CAR(args), CPLXSXP));
PROTECT(sb = coerceVector(CADR(args), CPLXSXP));
na = XLENGTH(sa); nb = XLENGTH(sb);
if ((na == 0) || (nb == 0)) return(allocVector(CPLXSXP, 0));
```

In the example above, if the then-branch is taken, the the pointers added to
the pointer protection stack just above will remain there after the function
exits.  It is a common pattern of such errors - one forgets that an
(exceptional) function return also has to release pointers from the
protection stack, or releases the wrong number of such pointers.  The tool
can handle different versions of the protect calls, a single protection
counter per function (commonly named ``nprotect``), certain kinds of loops,
saving and restoring the pointer protection stack explicitly, and also some
forms of conditionals (e.g.  ``if (nprotect) UNPROTECT(nprotect)'').  There
are still some false alarms and bailouts for code that is still too
complicated, and indeed there are not-useful reports for functions that have
protection stack imbalance by design.

To check the CRAN `BMN` package, run

```
bcheck ./src/main/R.bin.bc BMN.Rcheck/00_pkg_src/BMN/src/BMN.so.bc
```

the report also includes

```
Function runJTAlgSecMomVec
  has negative depth BMN.Rcheck/00_pkg_src/BMN/src/JTAlgWrapper.cc:152
  has possible protection stack imbalance BMN.Rcheck/00_pkg_src/BMN/src/JTAlgWrapper.cc:153

```

The reported function is shown below

```
SEXP runJTAlgSecMomVec(SEXP adjMat, SEXP thetaMat, SEXP varR, SEXP maxRuntime)
{
    // protect the items
    PROTECT(adjMat);
    PROTECT(thetaMat);
    PROTECT(varR);
     
    int size = INTEGER(GET_DIM(adjMat))[0];
    int var = INTEGER(varR)[0];
    time_t quittingTime = time(NULL) + INTEGER(maxRuntime)[0];

     
    // get space for the expectation and covariance matrix
    SEXP covVec, expec;
    PROTECT(covVec = allocVector(REALSXP, size));
    PROTECT(expec = allocVector(REALSXP, size));

    double *covVecPtr = REAL(covVec);
    double *expecPtr = REAL(expec);

    // get the probgraph object
    try
    {
        ProbGraph graph(size, INTEGER(adjMat), REAL(thetaMat), quittingTime);
        graph.getExpectSecMomSingleVar(var,expecPtr, covVecPtr);
    }
    catch(char const* errorMsg)
    {
        error(errorMsg);
    }
     
    // generate a list for the return values
    SEXP retList, dimnames;
    PROTECT(retList = allocVector(VECSXP,2));
    SET_VECTOR_ELT(retList,0,expec);
    SET_VECTOR_ELT(retList,1,covVec);
    PROTECT(dimnames = allocVector(STRSXP,2));
    SET_STRING_ELT(dimnames, 0, mkChar("Expectation"));
    SET_STRING_ELT(dimnames, 1, mkChar("SecondMomentVector"));
    setAttrib(retList,R_NamesSymbol, dimnames); 

    // unprotect them again
    UNPROTECT(8);    // <========================= line 152
    return(retList); // <========================= line 153
}
```

It is easy to see that the tool is right, only 7 pointers have been
protected, but 8 are being unprotected.

The tool gets confused by wrappers (functions) for the standard
protection/unprotection functions, reporting then false alarms.  Also, the
tools is confused when a `switch` statement handles all cases that can
happen in practice (and part of that handling is pointer unprotection), but
there is no `default` statement - the tool does not know that all cases have
been handled. Also, some code uses patterns such as 
`UNPROTECT(nprotect + 2)`, which are not supported by the tool.

Currently the `bcheck` tool also checks for unprotected pointers at calls
(described below).  Even though these two kinds of bugs are unrelated, the
underlying working of the tool is the same (interpreting the guards,
conditions, etc).

## Detecting Unprotected Objects At Allocating Calls

The tool attempts to detect unprotected (live) objects at allocating calls. 
Live objects means objects that will still be used after the call.  This is
a hard problem in the general case, but the `bcheck` code can do this in
many common situations, such as

```
SEXP attribute_hidden do_gctorture2(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    int gap, wait;
    Rboolean inhibit;
    SEXP old = ScalarInteger(gc_force_gap); /* GC */

    checkArity(op, args);
    gap = asInteger(CAR(args)); /* GC */
    wait = asInteger(CADR(args)); /* GC */
    inhibit = asLogical(CADDR(args));
    R_gc_torture(gap, wait, inhibit);

    return old;
}
```

the example has ''GC'' annotations created using `csfpcheck`. The `old`
variable is the only pointer to a live, unprotected object.  At calls to
`asInteger`, this object can be erroneously collected.  The tool attempts to
detect errors in more complicated cases, such as when an object is
protected, later unprotected, then an allocation function is called, and
later the object is used again.  But, with non-trivial uses of `UNPROTECT`
the tool gets confused and produces false alarms.  The tool produces a
warning when it gets confused. 

The tool also detects when an allocating function is called with an
allocating argument, which is often an error (by convention, most R
functions should be called with arguments protected by callers), but there
are many exceptions - functions that protect their arguments.  The tool
cannot yet detect this, but it has a hard-coded list of such callee-protect
functions from the R source code.  This is indeed error prone as the R code
can change between versions.

One source of false alarms is when the tool thinks that some function
returns a newly allocated object, but in fact it does not in the particular
context.  The experience is that the tool finds many errors, but the false
alarms rate is rather high.  A common source of true errors is a failure to
protect the result of `getAttrib` when retrieving an attribute that may be
automatically generated/converted (e.g.  `names`, `dimnames`).

The tool also tries to detect implicit protection, when a pointer is stored
into an object already protected, such as

```
PROTECT(klass = allocVector(STRSXP, 2)); /* GC */
SET_STRING_ELT(klass, 0, mkChar("POSIXlt")); /* GC */
SET_STRING_ELT(klass, 1, mkChar("POSIXt")); /* GC */
```

where the strings allocated by `mkChar` are implicitly protected by
`SET_STRING_ELT`, which connects them into the string vector `klass`, which
is already protected (so, there is no protection error in the example).  In
order to do this, the tool has a hard-coded set of implicitly protecting
functions like `SET_STRING_ELT`.


Sometimes, the errors are very unsophisticated. Checking the CRAN `ccgarch`
package also generates this report.

```
Function uni_vola_sim
  unprotected variable el2 while calling allocating function 
    Rf_allocVector ccgarch.Rcheck/00_pkg_src/ccgarch/src/R_uni_vola_sim.c:16
```

```
PROTECT(z = allocVector(REALSXP, nobs));
PROTECT(output = allocVector(VECSXP, 2));
el2 = allocVector(REALSXP, 1);
hl = allocVector(REALSXP,1); <===================== line 16
rh = REAL(h);
```

This report is indeed a true error, the call to `allocVector` may trigger GC
and kill the object pointed to by `el2`.

## Bizarre False Alarms and Approximations at LLVM Bitcode Level

Most false alarms are due to approximations sketched in this text so far. 
But some false alarms, in practice it seems very few, may seem rather
bizarre.  This may be caused by approximation at the level of intepretting
the LLVM bitcode - phi nodes are often not supported or their semantics is
simplified.  Also, the tool only maintains some state for local variables,
but not for other LLVM bitcode registers; this can lead to errors when the
compiler generates unusal code (e.g.  an outdated value of a local variable
is read from an LLVM register); note the CLANG compiler has to be run with
all optimizations disabled.

To reduce the risks of missing true errors due to these limitations
of the tools, one can make all value transfers between basic blocks go
through local variables ("memory") using LLVM's `opt` tool:

`opt -reg2mem R.bin.bc > R.bin.reg2mem.bc`

This also happens to reduce the number of phi nodes (or eliminates them). On
the other hand, the resulting bitcode is harder to check as it has indeed
more local variables, so one may need a lot of RAM for the checking.  Using
the tool does not eliminate these errors entirely.  It may not remove all
phi nodes (even in practice it seems to), and it will not remove unusual
code (e.g.  the use of an oudated variable value) that happens within a
single basic block.
