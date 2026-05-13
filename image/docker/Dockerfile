#
# This container checks R packages for PROTECT bugs. The simplest use is:
#   docker run XXX jpeg
# to check package jpeg, where XXX is the container id.
#
# See https://github.com/kalibera/rchk/blob/master/doc/DOCKER.md for more.
#

FROM ubuntu:22.04
LABEL maintainer="tomas.kalibera@gmail.com"

ARG DEBIAN_FRONTEND=noninteractive
ARG LLVM_VERSION=22
ARG RCHK_REPO=https://github.com/kvapca/rchk
ARG RCHK_REF=llvm22-cache
ARG R_SVN_REVISION=HEAD

RUN apt-get update && \
  apt-get install -yq --no-install-recommends apt-utils && \
  rm -rf /var/lib/apt/lists/* && \
  apt autoremove -y

ENV TZ=Europe/Prague

# Add LLVM package repository
RUN apt-get update && apt-get install -yq ca-certificates wget && \
  wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc >/dev/null && \
  . /etc/os-release && \
  echo "deb http://apt.llvm.org/${VERSION_CODENAME}/ llvm-toolchain-${VERSION_CODENAME}-${LLVM_VERSION} main" > /etc/apt/sources.list.d/llvm.list && \
  echo "deb-src http://apt.llvm.org/${VERSION_CODENAME}/ llvm-toolchain-${VERSION_CODENAME}-${LLVM_VERSION} main" >> /etc/apt/sources.list.d/llvm.list && \
  rm -rf /var/lib/apt/lists/* && \
  apt autoremove -y

# Install LLVM
RUN apt-get update && apt-get install -yq \
      clang-${LLVM_VERSION} llvm-${LLVM_VERSION} llvm-${LLVM_VERSION}-dev \
      libllvm${LLVM_VERSION} libc++-${LLVM_VERSION}-dev libc++abi-${LLVM_VERSION}-dev make && \
  rm -rf /var/lib/apt/lists/* && \
  apt autoremove -y

# Install R dependencies and common package dependencies
RUN apt-get update && apt-get install -yq zip unzip libpaper-utils xdg-utils \
      libbz2-dev libcairo2-dev libcurl4-gnutls-dev libgomp1 libicu70 libjpeg-dev liblzma-dev \
      libpango1.0-dev libpangocairo-1.0-0 libpcre3-dev libpng-dev libreadline-dev tcl8.6-dev \
      libtiff-dev tk8.6-dev libx11-dev libxt-dev zlib1g-dev file && \
  apt-get install -yq libgsl-dev gfortran && \
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
ENV LLVM_VERSION=${LLVM_VERSION} \
  WLLVM=/usr/local/bin \
  LLVM=/usr/lib/llvm-${LLVM_VERSION} \
  RCHK=/rchk

# Install rchk
RUN apt-get update && \
  dpkg --get-selections > /pkgs.sel && \
  apt-get install -yq git && \
  git clone -b ${RCHK_REF} ${RCHK_REPO} rchk && \
  echo "# settings moved to container configuration" > rchk/scripts/config.inc && \
  echo "# settings moved to container configuration" > rchk/scripts/cmpconfig.inc  && \
  cd rchk && \
  git rev-parse HEAD > git_version && \
  cd src && \
  env CXX=clang++-${LLVM_VERSION} LLVM=${LLVM} make -j 4 bcheck maacheck fficheck cachegen && \
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
  LD="clang++-${LLVM_VERSION} -stdlib=libc++" \
  R_LIBS=$RCHK/packages/libs

# set LLVM tool names for WLLVM
ENV LLVM_CC_NAME=clang-${LLVM_VERSION} \
  LLVM_CXX_NAME=clang++-${LLVM_VERSION} \
  LLVM_LINK_NAME=llvm-link-${LLVM_VERSION} \
  LLVM_AR_NAME=llvm-ar-${LLVM_VERSION}

# Build R using WLLVM and install core dependencies to build R packages
RUN export WLLVM_BC_STORE="$RCHK/bcstore" && \
  sed -i 's/^# deb-src/deb-src/g' /etc/apt/sources.list && \
  apt-get update && \
  dpkg --get-selections > /pkgs.sel && \
  apt-get build-dep -yq r-base && \
  apt-get install -yq subversion && \
  cd rchk && \
  svn checkout --revision "${R_SVN_REVISION}" https://svn.r-project.org/R/trunk trunk && \
  mkdir -p $R_LIBS && \
  mkdir -p $WLLVM_BC_STORE && \
  cd trunk && \
  ./configure --without-recommended-packages --prefix=$RCHK/build \
              --with-blas=no --with-lapack=no --enable-R-static-lib && \
  make -j 4 && \
  make install && \
  cd .. && \
  extract-bc ./build/lib/R/bin/exec/R && \
  mv ./build/lib/R/bin/exec/R.bc ./build/R.bc && \
  bash $RCHK/scripts/ensure_cache.sh ./build/R.bc ./build/R.bc.cache && \
  rm -rf trunk && \
  echo 'options(repos = c(CRAN="https://cran.r-project.org"))' > build/lib/R/etc/Rprofile.site && \
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
RUN mkdir -p $R_LIBS ${R_LIBSONLY} $PKG_BUILD_DIR && \
  apt-get update && \
  apt-get install -yq sudo && \
  sed -i 's/Defaults[\t]*env_reset/Defaults !env_reset/g' /etc/sudoers && \
  rm -rf /var/lib/apt/lists/*
  
ENV PATH=$RCHK/scripts:$RCHK/build/bin:$LLVM/bin:$PATH

WORKDIR /rchk

ADD container.sh /container.sh

ENTRYPOINT ["/bin/bash", "/container.sh"]
