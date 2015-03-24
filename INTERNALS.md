
Overall the choice of algorithms so far was the most naive one that still
solves the problem for R (slightly over 3,000 functions) in reasonable time
and memory requirements and for the constructs seen in the code of R.

## Detection of Error Paths

An *error basic block* is a basic block that never returns (its terminator
is not reachable).  The core of the error path detection is finding
*returning basic blocks*, that is basic blocks from which the return
statement is reachable.  All other basic blocks are error basic blocks. 
An *error function* is a function with error basic block as as the entry
basic block. This is a simple reachability problem (one for basic blocks
within a function, one for the callgraph).

```
errorBBs =
  foreach bb from f
    include bb if its terminator is known unreachable to LLVM
       or the bb calls into an error function
      
returningBBs =
  foreach bb from f
    include bb if it ends with return instruction

repeat
  foreach unclassified bb from f
    if bb has a returningBlock successor, add bb to returningBlocks

  repeat if added any blocks

if entry block of f is not returning
  mark f as errorFunction
```

The above algorithm is run on all functions and is then repeated as long as
any new error functions have been detected. Once this terminates, we have
identified all error functions. During checking a (non-error) function, the
tools usually avoid error basic blocks - the blocks are detected again using
the algorithm above applied only on the function of interest, as we already
know all error functions.

## Simple Allocator Detection

The goal of allocator detection is to identify all *allocating functions*
(functions that may allocate, hence may trigger garbage collection) and
*allocators* (allocating functions that may return a newly allocated
and unprotected object (SEXP)).

Detecting allocating functions is a reachability problem on the callgraph: a
function may allocate if it may reach `R_gc_internal`.  We build the
transitive closure of the full callgraph using a trivial fixed-point
algorithm working on the adjacency matrix and adjacency list.  This has been
fast enough so far.  We exclude error basic blocks - allocations on error
paths are ignored.

```
repeat
  foreach function f1
    foreach function f2 reachable from f1 (list)
      foreach function f3 reachable from f2 (list)

      if f1 is not yet known to reach f3 (matrix)
        add f3 as reachable from f1 (matrix and list)

   repeat if added any edges
```

Allocator detection is also a reachability problem, but on the subset of the
callgraph.  Only functions that return `SEXP` can be allocators.  We treat
all functions that return `SEXP` and call directly into `R_gc_internal` as
allocators.  For each function that returns `SEXP`, we check whether that
`SEXP` returned may come from a call to another function, and if so, we take
such call edge into account for callgraph closure calculation.  So far we
assume functions do not protect or implicitly protect objects prior to
returning them - we only treat specially the `install` functions
(`Rf_install`, `Rf_installTrChar`, `Rf_installChar`,
`Rf_installS3Signature`), because they are notorious examples of functions
that may allocate a new SEXP and return it, but they protect it internally
by storing it into the symbol table. This would be worth improving.

The core of the algorithm is detecting *wrapped* (possibly allocator)
functions for a (possibly allocator) function.

```
possiblyReturnedVariables of f =
  insert all variables returned directly by return statement
  
  repeat
    foreach store of var1 into var2
      if var2 is known possibly returned, but var1 is not yet
        add var2
      
    repeat if added any vars


identify nodes and edges for call graph

  include node R_gc_internal
  foreach function f
    if f is install, ignore it
    if f does not return SEXP, ignore it

    foreach call from f to f1
      if f1 is R_gc_internal
        include edge f->f1

      if f1 returns SEXP and is not install call
        if result of f1 is passed to return statement
        or is stored to a possibly returned variable
          include edge f->f1
          include node f1

compute transitive closure of call graph
allocators =
  all nodes that have an edge to R_gc_internal
```

The algorithm has a number of flaws, indeed it may happen that a variable
may be returned by a function, but not at the point when the result of a
particular allocator is written to it. Currently the implementation does not
handle phi nodes (which is technically an error, it may miss an allocator). 

## Symbol Detection

R symbols are often used in conditional expressions, including conditions
that are important in allocating calls (e.g. `getAttrib(, R_ClassSymbol)`
does not allocate, but `getAttrib(, R_NamesSymbols)` might. To handle that
context correctly, symbol variables (such as `R_NamesSymbol`) have to be
identified.

For symbol detection, *symbol* is a global variable which is at least once
assigned the result of `Rf_install("sym")`, that is a call to `Rf_install`
with a compile-time constant string as the argument. The variable may be
assigned multiple times, but always the result of `Rf_install` and always of
the same constant symbol name. It must not be assigned any other value. The
implementation is straightforward as LLVM allows to iterate through the
use-sites of any global variable.

For simplicity, symbols are in checking tools regarded as constants (that
is, already initialized to their value, even though in fact they may still
be uninitialized).

## Data-Flow and State Checking

### Integer Guards

### SEXP Guards

## Context-Sensitive Allocator Detection

Context-sensitive allocator detection aims to detect more precisely which
functions may allocate and which may be allocators, given some information
about the function arguments.  Currently only symbols are supported: some
arguments of a call may be known to be concrete symbols (with concrete known
symbol names).

## Balance Checking

### Unallocated Pointers Checking

## Heuristical Checks

### Multiple-Allocating Arguments
