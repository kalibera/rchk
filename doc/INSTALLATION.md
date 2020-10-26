# Automated installation (Docker/Virtualbox)

One can use a pre-installed virtual machine with rchk (or, more precisely,
use an automated script that installs such machine without user
intervention, into virtualbox or docker).  Please note that this is
conceptually the same thing as downloading the binary image from docker
hub/virtualbox repository, it is just independent on such repositories and
takes slightly longer (within 15 minutes on my laptop) to automatically
install. The installation will create a virtual machine into which one can
log in and perform the checks, all state is kept inside the machine.

Run `vagrant up --provider virtualbox` in `image` directory to use VirtualBox
or `vagrant up --provider docker` to use Docker.

One needs to install [Vagrant](https://www.vagrantup.com/) (`apt-get install
vagrant`) and [VirtualBox](https://www.virtualbox.org/wiki/Downloads)
(`apt-get install virtualbox`) or
[Docker](https://www.docker.com/get-started) (`apt-get install docker
docker.io`, add current user to the `docker` group).

If `vagrant up --provider virtualbox` fails or times out due to network
connectivity issues, `vagrant provision` restarts it.  To log into the
installed machined, use `vagrant ssh` and check the first R package using
instructions provided [here](../README.md).  One can access the host file
system in directory `/vagrant` inside the virtual installation, e.g.  to
copy package tarballs in and out.

# Automated installation (Singularity)

On Linux, one can install also into a singularity container
([instructions](SINGULARITY.md) and configuration contributed by B. 
W.  Lewis, with some maintenance/updates from me).  This setup is aimed
directly at checking of packages, one normally would not log into the
virtual machine, but the machine keeps some state on the host system
(package library, temporary build files, and rchk reports).  For just
checking of packages on Linux, this is easier to use than the Docker/Virtualbox
one. Even the text may seem long, the installation can be as simple as
(running on Ubuntu 18.04 as host system):

```
apt-get install singularity-container debootstrap
singularity build rchk.img singularity.def
```

and the checking just (package memisc from CRAN).

```
/usr/bin/singularity run rchk.img memisc
```

and the results appear under subdirectory "lib" of the current directory. 
Full path to the package tarball can be given instead as argument to check a
testing version of the package.  On Ubuntu 20.04, install
`singularity-container` from Neuro Debian (see
[instructions](SINGULARITY.md)).

# Alternative automated installations

An alternative docker image is also available from third parties on R-hub
(`rhub/ubuntu-rchk`,
[source](https://github.com/r-hub/rhub-linux-builders/tree/master/ubuntu-rchk)).

# Native installation on Linux

Once the installation is finished, check the first R package using
instructions provided [here](../README.md).  The description below assumes
that wllvm is installed via running pip as root; when run as a regular user,
the installation directory will be different (e.g.  `~/.local/bin`).

## Ubuntu 20.04 (Focal Fossa)

These instructions are for LLVM 10. Tested October 21, 2020.

0. Install build dependencies for R:
	* enable source repositories in `/etc/apt/sources.list`, e.g. using
	  `sed -i 's/^# deb-src/deb-src/g' /etc/apt/sources.list`
	* `apt-get update`
	* `apt-get build-dep -y r-base`
1. Install clang and llvm:
	* `apt-get install clang llvm-dev '^clang++$' llvm libllvm10 libc++-dev libc++abi-dev
2. Install [WLLVM scripts](https://github.com/travitch/whole-program-llvm):
	* `apt-get install python3-pip`
	* `pip3 install wllvm` (as root)
3. Install [rchk](https://github.com/kalibera/rchk.git):
	* `apt-get install git`
	* `git clone https://github.com/kalibera/rchk.git`
	* `cd rchk/src ; make ; cd ..`
	* customize `scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM
	is `/usr`, WLLVM is `/usr/local/bin`, RCHK is the
	path to rchk directory created by git.

## Debian (Bullseye)

These instructions are for LLVM 9. Tested October 21, 2020 on a clean
install of Debian testing (bullseye/sid).

0. Install build dependencies for R:
	* enable source repositories in `/etc/apt/sources.list`
	* `apt-get update`
	* `apt-get build-dep -y r-base`
1. Install clang and llvm:
	* `apt-get install llvm clang clang-9 llvm-9-dev llvm-9 libllvm9 libc\+\+-dev libc\+\+abi-dev`
2. Install [WLLVM scripts](https://github.com/travitch/whole-program-llvm):
	* `apt-get install python3-pip`
	* `pip3 install wllvm` (as root)
3. Install [rchk](https://github.com/kalibera/rchk.git):
	* `apt-get install git`
	* `git clone https://github.com/kalibera/rchk.git`
	* `cd rchk/src ; make ; cd ..`
	* customize `scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM
	is `/usr`, WLLVM is `/usr/local/bin`, RCHK is the
	path to rchk directory created by git.

## Fedora 32

These instructions are for LLVM 10. Tested October 21, 2020 on a clean install
of Fedora 32.

0. Install development tools and build dependencies for R:
	* `dnf install dnf-plugins-core diffutils which`
	* `dnf install redhat-rpm-config hostname java-1.8.0-openjdk-devel`
        * `dnf install 'dnf-command(builddep)'`
	* `dnf builddep R`
	* `dnf groupinstall "Development Tools"`
	* `dnf groupinstall "C Development Tools and Libraries"`
1. Install clang and llvm:
	* `dnf install llvm llvm-devel clang`
2. Install [WLLVM scripts](https://github.com/travitch/whole-program-llvm):
	* `dnf install python-pip`
	* `pip install wllvm` (as root)
3. Install [rchk](https://github.com/kalibera/rchk.git):
	* `dnf install git`
	* `git clone https://github.com/kalibera/rchk.git`
	* `cd rchk/src ;  make ; cd ..`
	* customize `scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM
	is `/usr`, WLLVM is `/usr/local/bin`, RCHK is the
	path to rchk directory created by git.

# Installing on older systems

There instructions will probably still work on older systems, but are not
re-tested as those systems evolve.

## Debian (Buster)

These instructions are for LLVM 7. Tested March 5, 2019 on a clean install
of Debian testing (buster/sid).

0. Install build dependencies for R:
	* enable source repositories in `/etc/apt/sources.list`
	* `apt-get update`
	* `apt-get build-dep -y r-base-dev`
1. Install clang and llvm:
	* `apt-get install llvm clang clang-7 llvm-7-dev llvm-7 libllvm7 libc\+\+-dev libc\+\+abi-dev`
2. Install [WLLVM scripts](https://github.com/travitch/whole-program-llvm):
	* `apt-get install python-pip`
	* `pip install wllvm` (as root)
3. Install [rchk](https://github.com/kalibera/rchk.git):
	* `apt-get install git`
	* `git clone https://github.com/kalibera/rchk.git`
	* `cd rchk/src ; env LLVM=/usr/lib/llvm-7 make ; cd ..`
	* customize `scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM
	is `/usr/lib/llvm-7`, WLLVM is `/usr/local/bin`, RCHK is the
	path to rchk directory created by git.

## Fedora 29 and 30

These instructions are for LLVM 7. Tested March 6, 2019 on a clean install
of Fedora 29 and March 7 on Fedora 30. Note, however, there may be error
messages like `objcopy: xxx: failed to find link section for section` (the
problem has been discussed at WLLVM website a bug filed against binutils).
It seems these messages can be ignored.

0. Install development tools and build dependencies for R:
	* `dnf install dnf-plugins-core hostname`
	* `dnf install redhat-rpm-config hostname java-1.8.0-openjdk-devel`
	* `dnf builddep R`
	* `dnf groupinstall "Development Tools"`
	* `dnf groupinstall "C Development Tools and Libraries"`
1. Install clang and llvm:
	* `dnf install llvm llvm-devel clang`
2. Install [WLLVM scripts](https://github.com/travitch/whole-program-llvm):
	* `dnf install python-pip`
	* `pip install wllvm` (as root)
3. Install [rchk](https://github.com/kalibera/rchk.git):
	* `dnf install git`
	* `git clone https://github.com/kalibera/rchk.git`
	* `cd rchk/src ;  make ; cd ..`
	* customize `scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM
	is `/usr`, WLLVM is `/usr/bin`, RCHK is the
	path to rchk directory created by git.

## Ubuntu 18.04 (Bionic Beaver)

These instructions are for LLVM 4.

0. Install build dependencies for R:
	* enable source repositories in `/etc/apt/sources.list`
	* `apt-get update`
	* `apt-get build-dep -y r-base-dev`
1. Install clang and llvm:
	* `apt-get install clang-4.0 llvm-4.0-dev clang\+\+-4.0 llvm-4.0 libllvm4.0 libc\+\+-dev libc\+\+abi-dev`
2. Install [WLLVM scripts](https://github.com/travitch/whole-program-llvm):
	* `apt-get install python-pip`
	* `pip install wllvm` (as root)
3. Install [rchk](https://github.com/kalibera/rchk.git):
	* `apt-get install git`
	* `git clone https://github.com/kalibera/rchk.git`
	* `cd rchk/src ; env LLVM=/usr/lib/llvm-4.0 make ; cd ..`
	* customize `scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM
	is `/usr/lib/llvm-4.0`, WLLVM is `/usr/local/bin`, RCHK is the
	path to rchk directory created by git.

To use rchk with LLVM 6, modify the steps above as follows:

* Install clang and llvm:
	* `apt-get install llvm clang clang-6.0 llvm-6.0-dev llvm-6.0 libllvm6.0 libc\+\+-dev libc\+\+abi-dev`
* Install rchk:
	* `cd rchk/src ; make ; cd ..`
	* customize `scripts/config.inc` to include export `LLVM=/usr`

## Ubuntu 19.04 (Disco Dingo)

These instructions are for LLVM 8.

0. Install build dependencies for R:
	* enable source repositories in `/etc/apt/sources.list`
	* `apt-get update`
	* `apt-get build-dep -y r-base-dev`
1. Install clang and llvm:
	* `apt-get install clang-8 llvm-8-dev clang\+\+-8 llvm-8 libllvm8 libc\+\+-dev libc\+\+abi-dev`
2. Install [WLLVM scripts](https://github.com/travitch/whole-program-llvm):
	* `apt-get install python-pip`
	* `pip install wllvm` (as root)
3. Install [rchk](https://github.com/kalibera/rchk.git):
	* `apt-get install git`
	* `git clone https://github.com/kalibera/rchk.git`
	* `cd rchk/src ; env LLVM=/usr/lib/llvm-8 make ; cd ..`
	* customize `scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM
	is `/usr/lib/llvm-8`, WLLVM is `/usr/local/bin`, RCHK is the
	path to rchk directory created by git.

## Debian 9 (Stretch)

These instructions are for LLVM 3.8.

0. Install build dependencies for R:
	* enable source repositories in `/etc/apt/sources.list`
	* `apt-get update`
	* `apt-get build-dep -y r-base-dev`
1. Install clang and llvm:
	* `apt-get install clang-3.8 llvm-3.8-dev clang\+\+-3.8 llvm-3.8 libllvm3.8 libc\+\+-dev libc\+\+abi-dev`
2. Install [WLLVM scripts](https://github.com/travitch/whole-program-llvm):
	* `apt-get install python-pip`
	* `pip install wllvm` (as root)
3. Install [rchk](https://github.com/kalibera/rchk.git):
	* `apt-get install git`
	* `git clone https://github.com/kalibera/rchk.git`
	* `cd rchk/src ; env LLVM=/usr/lib/llvm-3.8 make ; cd ..`
	* customize `scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM
	is `/usr/lib/llvm-3.8`, WLLVM is `/usr/local/bin`, RCHK is the
	path to rchk directory created by git.

## Fedora 28

These instructions are for LLVM 6.

0. Install development tools and build dependencies for R:
	* `yum install yum-utils`
	* `yum-builddep R`
	* `yum install redhat-rpm-config hostname java-1.8.0-openjdk-devel`
	* `yum groupinstall "Development Tools"`
	* `yum groupinstall "C Development Tools and Libraries"`
1. Install clang and llvm:
	* `yum install llvm llvm-devel clang`
2. Install [WLLVM scripts](https://github.com/travitch/whole-program-llvm):
	* `pip install wllvm` (as root)
3. Install [rchk](https://github.com/kalibera/rchk.git):
	* `yum install git`
	* `git clone https://github.com/kalibera/rchk.git`
	* `cd rchk/src ;  make ; cd ..`
	* customize `scripts/config.inc` (set root of LLVM, WLLVM, and rchk), LLVM
	is `/usr`, WLLVM is `/usr/bin`, RCHK is the
	path to rchk directory created by git.

# Installing on even older systems

While rchk used to work on older systems, it is not being backported to them
and cannot be compiled with older versions of LLVM due to incompatibility of
the API.  One can get an old version of rchk to work with LLVM 3.6
(`llvm-36` branch) and with LLVM 3.8 (`llvm-38` branch), but these older
versions of rchk do not include the latest fixes to work well with the
latest (development) version of R, and hence they are not recommended for
use.  It should, however, be possible to install rchk on an older system
with a working installation of LLVM 4, 5 or 6.  I've been using rchk on many
earlier versions of Ubuntu, so Ubuntu and Debian are likely to be easiest to
use.  Also I've used rchk regularly on several versions of Fedora. 
Particularly on older systems or with older versions of rchk, it is
recommended to use GCC/g++ instead of LLVM/clang++ to compile rchk as
crashes have been observed when compiled with clang++.  Earlier version of
rchk had an access-out-of-bounds bug that incidentally has been causing
crashes on Gentoo but not other systems.
