#
# This container checks R packages for PROTECT bugs. The simplest use is:
#   docker run XXX jpeg
# to check package jpeg, where XXX is the container id.
#
# See https://github.com/kalibera/rchk/blob/master/doc/DOCKER.md for more.
#

FROM ubuntu:22.04
LABEL maintainer tomas.kalibera@gmail.com

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
  apt-get install -yq --no-install-recommends apt-utils && \
  rm -rf /var/lib/apt/lists/* && \
  apt autoremove -y

ENV TZ=Europe/Prague

# Install LLVM, R dependencies and common package dependencies
RUN apt-get update && apt-get install -yq \
      clang llvm-dev '^clang++$' llvm libllvm14 libc++-dev libc++abi-dev make && \
  apt-get install -yq zip unzip libpaper-utils xdg-utils \
      libbz2-dev libcairo2-dev libcurl4-gnutls-dev libgomp1 libicu-dev libjpeg-dev liblzma-dev \
      libpango1.0-dev libpangocairo-1.0-0 libpcre3-dev libpng-dev libreadline-dev tcl8.6-dev \
      libtiff-dev tk8.6-dev libx11-dev libxt-dev zlib1g-dev ca-certificates \
      libgsl-dev gfortran git file && \
  rm -rf /var/lib/apt/lists/* && \
  apt autoremove -y

# Install wllvm
RUN apt-get update && \
  dpkg --get-selections > /pkgs.sel && \
  apt-get install -yq python3-pip && \
  pip3 install wllvm && \
  dpkg --clear-selections && \
  dpkg --set-selections < /pkgs.sel && \
  apt-get dselect-upgrade -yq && \
  rm /pkgs.sel && \
  apt-get purge -yq $(dpkg -l | grep '^rc' | awk '{print $2}') && \
  rm -rf /var/lib/apt/lists/*  && \
  apt autoremove -y

# config.inc
ENV WLLVM=/usr/local/bin LLVM=/usr RCHK=/rchk

# Install rchk
RUN apt-get update && \
  dpkg --get-selections > /pkgs.sel && \
  git clone -b llvm14 https://github.com/kalibera/rchk.git && \
  echo "# settings moved to container configuration" > rchk/scripts/config.inc && \
  echo "# settings moved to container configuration" > rchk/scripts/cmpconfig.inc  && \
  cd rchk && \
  git rev-parse HEAD > git_version && \
  cd src && \
  env CXX=clang++ make -j 4 bcheck maacheck fficheck && \
  rm -f *.o *.dwo *.cpp *.h *d Makefile && \
  cd .. && \
  dpkg --clear-selections && \
  dpkg --set-selections < /pkgs.sel && \
  apt-get -yq dselect-upgrade && \
  rm /pkgs.sel && \
  apt-get purge -yq $(dpkg -l | grep '^rc' | awk '{print $2}') && \
  rm -rf /var/lib/apt/lists/*  && \
  apt autoremove -y

# cmpconfig.inc (part 1)
ENV CPICFLAGS="-fPIC" \
  CFLAGS="-Wall -g -O0 -fPIC" \
  CXXFLAGS="-Wall -g -O0 -fPIC -I/usr/include/libcxxabi" \
  CC=wllvm \
  CXX="wllvm++ -stdlib=libc++" \
  LLVM_COMPILER=clang \
  LD="clang++ -stdlib=libc++" \
  R_LIBS=$RCHK/packages/libs

ARG CRAN
ENV CRAN=${CRAN:-https://cran.R-project.org}

# Build R using WLLVM and install core dependencies to build R packages
RUN export WLLVM_BC_STORE="$RCHK/bcstore" && \
  sed -i 's/^# deb-src/deb-src/g' /etc/apt/sources.list && \
  apt-get update && \
  dpkg --get-selections > /pkgs.sel && \
  apt-get build-dep -yq r-base && \
  apt-get install -yq subversion && \
  cd rchk && \
  svn checkout https://svn.r-project.org/R/trunk && \
  mkdir -p $R_LIBS && \
  mkdir -p $WLLVM_BC_STORE && \
  cd trunk && \
  ./configure --without-recommended-packages --prefix=$RCHK/build \
              --with-blas=no --with-lapack=no --enable-R-static-lib && \
  make -j 32 && \
  make install && \
  cd .. && \
  extract-bc ./build/lib/R/bin/exec/R && \
  mv ./build/lib/R/bin/exec/R.bc ./build/R.bc && \
  rm -rf trunk && \
  echo 'options(repos = c(CRAN="'$CRAN'"))' > build/lib/R/etc/Rprofile.site && \
  dpkg --clear-selections && \
  dpkg --set-selections < /pkgs.sel && \
  apt-get -yq dselect-upgrade && \
  rm /pkgs.sel && \
  apt-get purge -yq $(dpkg -l | grep '^rc' | awk '{print $2}') && \
  sed -i 's/^deb-src/# deb-src/g' /etc/apt/sources.list && \
  rm -rf /var/lib/apt/lists/* && \
  apt autoremove -y

# cmpconfig.inc (part 2)
ENV R_LIBSONLY=$RCHK/packages/libsonly \
  PKG_BUILD_DIR=$RCHK/packages/build

# setup for container.sh
# sudo is needed to become regular user to match the user running / mounting
# rchk/packages
RUN mkdir -p $R_LIBS ${R_LIBSONLY} $PKG_BUILD && \
  apt-get update && \
  apt-get install -yq sudo && \
  sed -i 's/Defaults[\t]*env_reset/Defaults !env_reset/g' /etc/sudoers && \
  rm -rf /var/lib/apt/lists/*
  
ENV PATH=$RCHK/scripts:$RCHK/build/bin:$PATH

WORKDIR /rchk

ADD container.sh /container.sh

ENTRYPOINT ["/bin/bash", "/container.sh"]
