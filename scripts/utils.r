
# Installs libraries (--libs-only) of a given package.  Complete packages
# needed as dependencies are installed as normally in R (e.g.  to R_LIBS). 
# The partial package, shared libraries only, is installed into
# <target>.
#
# This function tries to avoid installing too many packages (as that may
# require additional system libraries, possibly not available on the system,
# and takes space and time).  The function tries to in this order
#
#   1. install only LinkingTo dependencies and their default (Imports,
#      Depends, LinkingTo) dependencies, and install libraries of the
#      package
#
#   2. install all default dependencies, and install libraries of the
#      package
#
# Note: the first attemp above may result in warnings, even when the second
# attempt succeeds/
#

install_package_libs <- function(package,
                          target = Sys.getenv("R_LIBSONLY", file.path(tempdir(), "libsonly")),
                          contriburl,
                          CRAN_mirror = "https://cran.r-project.org/src",
                          BIOC_mirror = "https://master.bioconductor.org/packages/3.11",
                          Ncpus = 4) {

  check_rchk_variables()
  dir.create(target, recursive = TRUE, showWarnings = FALSE)

  if (missing(contriburl)) {  
    contriburl <- c(paste0(CRAN_mirror, "/contrib"),
                    paste0(BIOC_mirror, "/bioc/src/contrib"),
                    paste0(BIOC_mirror, "/data/annotation/src/contrib"),
                    paste0(BIOC_mirror, "/data/experiment/src/contrib"),
                    paste0(BIOC_mirror, "/workflows/src/contrib"))
  }

  ap <- available.packages(contriburl)
  
  # extract names of direct dependencies (linking to, all default)
  if (grepl(".tar.gz$", package)) {
    tdir <- tempfile(pattern = "dir", tmpdir = tempdir(), fileext = "")
    utils::untar(package, exdir=tdir)
    pkgname <- list.files(tdir)
    if (length(pkgname) != 1)
      stop("Bad package archive")

    # FIXME: unexported functions from tools
    pkgInfo <- tools:::.split_description(
                 tools:::.read_description(
                   file.path(tdir, pkgname, "DESCRIPTION")))

    direct_linking_to <- unique(c(names(pkgInfo$LinkingTo)))
    default <- unique(c(names(pkgInfo$Depends), names(pkgInfo$Imports),
                        names(pkgInfo$LinkingTo)))
    
  } else {
    pkgname <- package    
    direct_linking_to <- unique(unlist(
        tools::package_dependencies(package, db = ap, which = "LinkingTo")
    ))
    default <- unique(unlist(
        tools::package_dependencies(package, db = ap, recursive = TRUE)
    ))
  }

  sodir <- file.path(target, pkgname)
  unlink(sodir, recursive = TRUE)
  
  Sys.setenv("_R_INSTALL_LIBSONLY_FORCE_DEPENDS_IMPORTS_" = "FALSE")
  
  ip <- installed.packages()[,"Package"]
  deps <- direct_linking_to[!(direct_linking_to %in% ip)]
  
  # install direct linking to dependencies and their default dependencies
  install.packages(deps, contriburl = contriburl, 
                   available = ap,  Ncpus = Ncpus,
                   INSTALL_opts = c("--no-byte-compile"))
                   
  install.packages(pkgname, contriburl = contriburl, lib = target,
                   available = ap, libs_only = TRUE, dependencies = FALSE,
                   INSTALL_opts = c("--no-test-load"))
                   
  soname <- file.path(target, pkgname, "libs", paste0(pkgname, ".so"))
  if (!file.exists(soname)) {
  
    # install the package libraries with all default dependencies
    
    ip <- installed.packages()[,"Package"]
    deps <- default[!(default %in% ip)]
    
    install.packages(deps, contriburl = contriburl, 
                     available = ap,  Ncpus = Ncpus,
                     INSTALL_opts = c("--no-byte-compile"))
    
    install.packages(pkgname, contriburl = contriburl, lib = target,
                     available = ap, libs_only = TRUE, dependencies = FALSE,
                     Ncpus = Ncpus, INSTALL_opts = c("--no-test-load"))    
  }
  
  if (file.exists(soname))
    soname
  else
    character(0)
}

check_rchk_variables <- function() {
  RCHK <- Sys.getenv("RCHK", "/dev/null")
  if (!file.exists(file.path(RCHK, "scripts/config.inc")))
    stop("Please set RCHK variables (scripts/config.inc)")
}
