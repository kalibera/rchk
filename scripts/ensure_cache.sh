#! /bin/bash

# ensure_cache.sh ensures the cache is available and fresh
# It generates the cache by invoking cachegen if needed.
# Usage: ensure_cache.sh [--fresh] <path-to-RBC-file> <path-to-cache-file>

if [ ! -r $RCHK/scripts/config.inc ] ; then
  echo "Please set RCHK variables (scripts/config.inc)" >&2
  exit 2
fi

if [ ! -x $RCHK/src/cachegen ] ; then
    echo "Please set RCHK variables (scripts/config.inc) and RCHK installation - cannot find tool cachegen." >&2
  exit 2
fi

. $RCHK/scripts/common.inc

if ! check_config ; then
  exit 2
fi

# Parse command-line options.
FRESH=0
if [ X"$1" == X"--fresh" ] ; then
  FRESH=1
  shift
fi

# RBC and cache file have to be passed explicitly
RBC="$1"
CACHE_FILE="$2"
if [ X"$RBC" == X ] || [ X"$CACHE_FILE" == X ] ; then
  echo "Usage: $0 [--fresh] <path-to-RBC-file> <path-to-cache-file>" >&2
  exit 2
fi

if [ "$FRESH" -eq 1 ] || [ ! -r "$CACHE_FILE" ] || [ "$RBC" -nt "$CACHE_FILE" ] || [ "$RCHK/src/cachegen" -nt "$CACHE_FILE" ] ; then
  echo "Regenerating R base cache..."
  $RCHK/src/cachegen "$RBC" "$CACHE_FILE"
fi
