
## Detection of Error Paths

An *error basic block* is a basic block that never returns (its terminator
is not reachable).  The core of the error path detection is finding
*returning basic blocks*, that is basic blocks from which the return
statement is reachable.  All other basic blocks are *error basic blocks*. 
An *error function* is a function with *error basic block* as as the entry
basic block. This is a simple reachability problem.

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
the algorithm above applied only on the function of interest (as we already
know all error functions).

## Simple Allocator Detection

## Symbol Detection

## Context-Sensitive Allocator Detection

## Data-Flow and State Checking

### Integer Guards

### SEXP Guards

### Balance Checking

### Unallocated Pointers Checking

## Heuristical Checks

### Multiple-Allocating Arguments
