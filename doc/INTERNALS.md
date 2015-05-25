
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
within a function, one for the call graph).

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

Detecting allocating functions is a reachability problem on the call graph: a
function may allocate if it may reach `R_gc_internal`.  We build the
transitive closure of the full call graph using a trivial fixed-point
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
call graph.  Only functions that return `SEXP` can be allocators.  We treat
all functions that return `SEXP` and call directly into `R_gc_internal` as
allocators.  For each function that returns `SEXP`, we check whether that
`SEXP` returned may come from a call to another function, and if so, we take
such call edge into account for call graph closure calculation.  So far we
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

The context-sensitive allocator detection, balance checking, and unprotected
objects checking (all described later) are all based on the same core
checking algorithm.  This algorithm is (based on) a work-list algorithm for
forward data flow analysis.  It ''simulates'' execution of the checked
function, maintaining a *state*.  The state includes the current instruction
pointer and more, depending on the precision of checking and the tool.  The
checker remembers states that were already explored, at basic block
granularity, to avoid checking states multiple times (e.g.  due to loops). 
When reaching the terminator of a basic block, the tool discovers possibly
new states to explore (conservatively allowing all successor basic blocks,
but with more precise tools only a subset of them).  The simulation
terminates when there are no new states to explore.  To ensure termination,
the additional information in the states must not be too precise compared to
the precision of adding successors - e.g.  remembering in the state the
exact value of the loop control variable, but not being able to tell when
the loop should finish, would lead to infinite simulation.

```
workList =
  entry basic block

visitedSet =
  entry basic block

while workList nonempty
  take state from worklist

  for each non-terminator instruction of the state's basic block in execution order
    simulate execution as needed to maintain state of guards
    simulate execution as needed to maintain state of checking tool
      and report suspected errors if found

  for the terminator instruction
    simulate execution as needed to maintain state of checking tool
      and report suspected errors if found

    if it is a branch on a (supported form of) a guard
      treat addition of new states specially

    otherwise if it is a branch (goto, loop back-branch, switch, etc)
      for each possible successor basic block
        if that block with the current state information is not in visitedSet
          add new state to visitedSet
          add the new state also to workList

```

The `workList` is implemented as a stack, so we do depth-first traversal.
The `visitedSet` is implemented as a hashset and the quality of the hashing
function in practice turned out very important for the performance of the
checking.

### Integer Guards

We treat specially conditional expressions that check whether an integer
variable (which includes a boolean in C) is zero, e.g.

```
if (intVar) {
  <trueBranch>
} else {
  <falseBranch>
}
```
When a function has expressions like this, we remember in the checking state
some information about `intVar` (an integer guard variable): whether it is
known to be zero, known to be non-zero, or has unknown value.  In the above
example, if `intVar` had unknown value at the condition, we would include
two successor states, one for the true branch (in which we will set `intVar`
to be known non-zero) and one for the false branch (in which we will set
`intVar` to be known zero).  If we knew that `intVar` was zero (or
non-zero), we would only consider the false branch (true branch) as the
successor.

When an integer guard variable is written, we set its state to unknown,
unless it is set to a compile-time constant.  More cases could be supported,
such as copying a value from another integer guard. As elsewhere in the
tool, we only supported cases that we saw happening in the code.

For performance reasons, we only remember information about integer
variables that are used in conditional expressions and for which we may
benefit: currently if the variable is used in two or more conditions, or
just in one condition and is assigned a constant once. Also, guards can be
enabled only adaptively when some errors have been found when checking
without guards (a form of refinement) as described later.

### SEXP Guards

We treat specially conditional expressions that check the state of SEXP
variables - we call these variables SEXP guards. Again we only keep track of
selected SEXP variables, based on a heuristic of when it may benefit. The
support for SEXP guards is more sophisticated than that for integer guards,
as we found SEXP guards to be more important for allocation-related
checking. About an SEXP guard variable, we remember whether it is known to
be nil (`R_NilValue`), known to be a symbol of a known name, known to be
non-nil (not `R_NilValue`), or whether if it is of unknown value. Note that
a symbol is always non-nil.

Knowledge about values of SEXP guards is propagated from

* arguments of a function (with context-sensitive checking)
* assignment of R_NilValue: `sexpVar = R_NilValue`
* from another guard `sexpVar1 = sexpVar2`
* assignment of a symbol, e.g. `sexpVar = R_ClassSymbol`
* optionally assignment of allocated value: `sexpVar = allocVector(...`

The propagation from assignment from an allocator is optional, because the
allocator detection is only approximate, it may specifically report as
allocators functions that do not always return a freshly allocated object
(so we cannot really be sure the returned object is non-nil).  It might be
worth for this purpose having one more alternative allocator detection that
would err on the negative side.

Like for the integer guards, the knowledge about SEXP guards is propagated
through conditional expressions. The following forms of conditions are
handled

* comparison against `R_NilValue`
* comparison using `isNull(sexpVar)`
* comparison against a symbol, e.g. against `R_ClassSymbol`
* type tests, e.g. `isSymbol(sexpVar)`, `isString(sexpVar)`, etc

The type tests are handled both when inlined (macros used in the R core) and
when calling a function (e.g. from a package). We currently do not track
information whether a guard is of a type other than symbol (or nil), but it
might be useful to do so. We do not track information about possible
reference count values of a guard, this might again be useful (e.g. when
there is duplication only if the object is shared, but we would know it was
private).

## Context-Sensitive Allocator Detection

Context-sensitive allocator detection aims to detect more precisely which
functions may allocate and which may be allocators, given some information
about the function arguments.  Currently only symbols are supported: some
arguments of a call may be known to be concrete symbols (with concrete known
symbol names).

A *called function* is a function with some knowledge about how it is
called, such as `Rf_getAttrib(?,S:row.names)` (the second argument is known
to be symbol ''row.names) or `do_subset2_dflt(?,S:[[,?,?)`. A *called
allocating function* is a called function that may allocate, a *called
allocator* is a called allocating function that may also return a newly
allocated (unprotected) object. By definition, if for a called allocating
function  we remove the argument information, the resulting function will be
an allocating function (detected in context-insensitive way). The same
applies to *called allocators*. This is used also to improve performance of
called allocator detection -- we only check allocating functions and
allocators.

We find for each called function:

* the set of possibly called called functions (edges for detection of allocating
  functions)

* the set of possibly wrapped called function (edges for detection of
  allocators)

Similarly to the context-insensitive allocator detection, we then compute
the transitive closure of the two ''call graphs'' to find functions that may
call into `R_gc_internal`. Again we use a simple fixed-point algorithm using
the adjacency matrix and the adjacency list.

The sets of called and wrapped functions are computed using the data-flow
and state checking algorithm described above, with both integer guards and
SEXP guards always enabled.  In addition to the basic block and guards
information, it remembers in the state:

* set of (called) functions called since function entry on this path
* SEXP variable origins - for each SEXP variable 
    set of (called) functions result of which may have been assigned to the variable

Maintaining the set of functions called is easy, whenever a call instruction
is visited, a called function is added to the set of remembered in the
checking state.  When the return instruction is reached, the set is copied
from the state into the per-function result.  For performance, we restrict to
functions known to be allocating by the context-insensitive algorithm.

The variable origins are tracked at store instructions and the return. At
store, assignment to a variable replaces its origins (by origins from
another variable or by one origin, if it is a call). At return instruction,
depending on which variable is being returned, the respective origins are
copied into the per-function results.

The detection is only approximate. E.g., the algorithm may even miss some
allocators when a function does not allocate, but returns one of its
arguments allocated by the caller.

## Balance Checking

The purpose of balance checking is finding functions that cause pointer
protection stack imbalance. This tool uses the data flow and state checking
algorithm described above. Integer and SEXP guards are only turned on if
needed. The given function is first checked with the guards enabled and if
no errors are found, they couldn't be found even with the guards enabled, so
the tool is done with that function. Only in case of errors, it adaptively
enables integer guards and does the checking again. Only if there are still
errors found, it re-runs with SEXP guards enabled as well.

The tool supports constructs like

* `PROTECT(var)`, `PROTECT_WITH_INDEX(var, %idx)`, 
* `UNPROTECT(3)`, `UNPROTECT_PTR(var)`
* `UNPROTECT(nprotect)`, `nprotect = 1`, `nprotect += 3`,`if (nprotect) UNPROTECT(nprotect)`
* `UNPROTECT(intGuard ? 3 : 4)`
* `savestack = R_PPStackTop`, `R_PPStackTop = savestack`

with certain restrictions. Only one *protection counter variable* per
function is allowed (like `nprotect` above). Only one *top save variable*
per function is allowed (like `savestack` above).

The tool initially attempts to track the exact relative depth of the pointer
protection stack and remember it in the checking state.  Likewise, if there
is a pointer protection variable, the tool initially attempts to track the
exact value of that variable. For some functions this approach works fine
and allows the tool to find most balance errors. However, it would fail on
examples such as

```
for( non-constant loop bound ) {
  ...
  PROTECT(x);
  nprotect++;
  ...
}
...
UNPROTECT(nprotect);
```

that is, whenever the stack depth increases in each iteration of a loop. In
the example above (which is e.g.  present in `do_sprintf` in R), one can
however see that the `nprotect` counter correctly increases with the stack
depth -- this is the intended behavior, correct code could look like this. 
To verify this correct behavior, the tool does not need to know the exact
depth and the exact value of `nprotect`, but only *by how much they differ*. 
Hence, when the tool detects a too high exact stack depth in a state (*exact
state*), it switches to a *differential state*, where it only tracks the
*difference* between the stack depth and the counter value.

In the differential state, it is still possible to handle
`UNPROTECT(nprotect)`, and more than that, after executing it we again know
the true (absolute) depth. `if (nprotect) UNPROTECT(nprotect)` is
interestingly used in the code, even though it is equivalent to simply
`UNPROTECT(nprotect)` and is treated by the tool even in the differential
state when we do not know whether `nprotect` is zero or not. In the exact
state, a conditional on an exact value of `nprotect` is handled similarly to
integer guards.

The tool also remembers in the state the depth at the time when it was saved
to a variable (`saveddepth = R_PPStackTop`), so that it can simulate the
reverse operation of restoring it later (`R_PPStackTop = saveddepth`). This
is only supported in the exact state (when exact depth is known).

### Unprotected Objects Checking

The objective is to detect when an unprotected object may be killed by a
call to an allocating function, but used afterwards. 

From the allocator detection we know (with certain error rate) which
functions are allocators, and hence where unprotected objects are created;
we also know which functions are allocating, and hence where unprotected
objects may be killed.  Still, tracking state of individual objects as of
whether they are "unprotected" or "protected" (~reachable from roots) is
challenging for a static analysis tool, as objects can be linked together
and then disconnected in various ways.

When an object is returned by the allocator, it is still easy - the tool
knows that the object is unprotected.  Then the tool can handle simple cases
of `PROTECT` calls paired by `UNPROTECT` with constant arguments, when
applied on isolated (unconnected) objects, which is relatively common.  In
particular, the tool can thus handle a premature unprotection error of an
isolated object, e.g.

```
UNPROTECT(1); /* ans */
if(sorted) sortVector(ans, FALSE);
return ans;
```

here `ans' is unprotected prematurely, because it can still be killed by
`sortVector` (`sortVector` must be called with its argument protected). As
the tool works only on isolated objects, it identifies objects by variables
that hold pointers to them (or, that held the pointer at time of
protection). All these cases are treated as protection of variable `x`

```
PROTECT(x = allocVector(VECSXP, 2));
x = PROTECT(allocVector(VECSXP, 2));
x = allocVector(VECSXP, 2); PROTECT(x);
```

The tool in fact `tracks` local pointer variables that hold unprotected
objects or objects that have been protected by `PROTECT`, but may become
unprotected again via `UNPROTECT`.  For each tracked variable, the tool
keeps a counter how many times the variable has been protected (0 initially
when freshly allocated).  Also, the tool maintains a pointer protection
stack, each element of which is a local variable (or `NULL' when the
protected value is anonymous -- not linked to a variable). An interesting
and not completely uncommon case is when a protected variable is being
overwritten. It's protection counter is then set to zero in order to detect
bugs, an possibly subsequent `UNPROTECT` calls that would make the counter
go below zero have no effect; legal code patterns like this are therefore
supported without a false alarm:

```
PROTECT(x);  
x = allocVector(VECSXP, 2);  
UNPROTECT(1);
PROTECT(x);
```

The tool also supports `ProtectWithIndex` and `REPROTECT`, to a level, on
isolated objects (`ProtectWithIndex` is treated like `PROTECT` and
`REPROTECT` keeps the original positive protect count, or if zero makes it
as heuristics `1`). Whenever there is a `PROTECT` call on an untracked
variable, the variable becomes tracked, assuming there was a reason to
`PROTECT` it (but the the possibly didn't know). 

The protection stack has a limited depth, so that the tool does not run
indefinitely for C loops that increase the pointer protection stack depth in
every iteration.  In this regard, this tool is less sophisticated than
balance checking - it cannot handle the protection counter variable and
direct manipulation with the protection stack top.  The `PreserveObject`
call is supported in a trivial way, the variable passed to this call becomes
untracked (the object will never be again treated as unprotected, even
though if `ReleaseObject` was called on it).


The tool assumes that usual function calls, other than various protect calls
mentioned so far, do not cause their arguments to be protected after these
calls return.  Hence, passing an unprotected object to a function does not
protect it.  Less obvious exceptions handled explicitly are setter calls,
e.g.

```
SEXP ans = PROTECT(allocVector(VECSXP, 2)); /* GC */
SEXP nm = allocVector(STRSXP, 2); /* GC */
setAttrib(ans, R_NamesSymbol, nm); /* GC */
SET_STRING_ELT(nm, 0, mkChar("x")); /* GC */
```

here `setAttrib` (a setter call) links `nm` to `ans`, which is protected,
and hence `nm` is also protected (the object `nm` points to is reachable
from the root `ans` on the pointer protection stack).  Whenever an object is
passed to a setter function, and the first argument of the setter function
is protected (untracked or tracked with positive protection counter), then
the argument being passed is made untracked (will forever be treated as
protected).  In the example above, `nm` after the call to `setAttrib` will
always be treated as protected, even though indeed in theory `ans` could be
unprotected, hence indirectly unprotecting also `nm`. Currently the setter
calls are hardcoded in the tool (various `SET*` and `SET_*` calls).

Whenever a tracked variable is stored into a global (meaning a global object
will then point to the object pointer by that tracked variable), the
variable is untracked. The same happens whenever a tracked variable is
stored into a memory location derived from a local variable (e.g. this can
be an inlined setter call - and this is mostly a heuristics, indeed in fact
such operation will not always mean protection). A simple assignment between
local variables, such as `y = x`, will cause variable `y` to be untracked
(assignment of an unknown thing), but the state of `x` will not change, if
it is tracked it will remain so. These are all heuristics. It would have
been perhaps better to detect inlined setters, yet it would be some more
work.

The tool uses the data flow and state checking algorithm described above,
including the adaptive enabling of guards (currently the tool is integrated
with the balance checking, but it could easily be made standalone).  In the
state, it remembers a set of tracked variables, protection counters for
these tracked variables, the protection stack model, and conditional error
messages (possibly multiple for each variable).

*Conditional* messages are conditional on that a given variable will ever be
used later.  This is a form of live variable analysis implemented in a
forward-flow checking tool.  Normally it would be natural to implement live
variable analysis using backward flow, but it would be very complicated to
do with the context sensitivity implemented now.  So, the current
implementation remembers the conditional messages in the checking state.  It
maintains a map of variables to messages.  If a variable is being used
(read), it is looked up in this map, and if there are any conditional
messages for it, they are printed and deleted from the state.  If a variable
is rewritten (killed), it is also looked up in this map, and any messages
conditional on it are removed.

So, when the tool gets to an allocating function, it will create conditional
error messages for unprotected variables (variables tracked with protect
count zero).  The tool has a hardcoded list of callee-protect functions,
which is functions that will on their own protect arguments passed to them
and ensure they are protected through all possible allocations; hence, the
tool does not produce falls alarms for unprotected variables being passed to
callee-protect functions.  The tool also reports when a result of an
allocator call is passed directly into a (non-callee-protect) allocation
function.

Possibly, callee-protect and maybe setter calls could be detected
automatically by the tool, even though certainly with limited precision.  It
would be more robust, less precise, and possibly could be combined: custom
functions in packages can be callee-protect, but the tool will not know. 
Sadly the unprotected objects checking currently produces a large number of
false alarms.  It also fails on a number of functions which use the
mentioned non-trivial protection features.  It is not immediately obvious
how to extend the tool to support those.

### Multiple-Allocating Arguments

The tools to detect multiple allocating arguments at calls are simple
heuristics and do not use the data flow algorithms described above directly,
but they take advantage of the (simple) allocator detection.

`maacheck`, the simpler one, for each call to a function counts the number
of arguments that are

1. allocated (result of a call to allocator)
2. allocating (result of an expression that allocates, so it includes
previous group)
3. non-allocating

The tool reports a warning whenever `nAllocating >= 2 AND nAllocated >= 1`,
because in that case the allocated argument may be killed by another
allocating argument. The tool perform these checks linearly for all calls in
all functions. 

`ueacheck` is a more complicated variant of this tool. It tries to also
support the case when the allocated argument is being read from a variable.
The tool attempts to detect when such variable is protected, as to reduce
false alarms. The tool uses a heuristics based on dominator trees and
capture analysis (both provided by LLVM).

```
hasUnprotectedObject var

  if var may be capture before the call
    return false [LLVM capture analysis]

  find dominating allocation var = alloc()
    check all stores to var
      check if it takes value from an allocator
        check if it dominates the use of var of interest

  look for PROTECT(var) between the allocation and use
    check all loads of var
      check if it is passed to PROTECT or PROTECT_WITH_INDEX
        check if it is dominated by the allocation 
          check if it dominates the use

            if found, return false

  return true
```

There are many approximations.  There may not be a single dominating
PROTECT, but different PROTECTs on different paths.  There may not be a
single dominating allocation, but different allocations on different paths.
The pointer may also be passed to other protecting function than
`Rf_protect` or `Rf_ProtectWithIndex`. It is almost embarrassing that this
hack has been quite effective in finding errors in the core R.
