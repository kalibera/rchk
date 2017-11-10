require 'yaml'

# read configuration

configfile = "/vagrant/config.yml"
unless File.exists? configfile
    raise "config.yml does not exist!"
end

yamlconfig = YAML.load_file configfile

def read_fixnum_var(config, varname)
  if config.has_key? varname
    res = config[varname]
    unless res.class == Fixnum
      raise "config error (#{configgile})): ${varname} must be a number"
    end
    return res
  else
    return 0
  end
end

bcheck_max_states = read_fixnum_var(yamlconfig, "bcheck_max_states")
callocators_max_states = read_fixnum_var(yamlconfig, "callocators_max_states")

# install some packages

username = nil
["vagrant", "ubuntu"].each do |user|
  res = `grep "^#{user}" /etc/passwd`
  username = user unless res.empty?
end

execute "initial apt-get update" do
  command "apt-get update"
  user "root"
  action :nothing
    # do not run unless notified
end

execute "enable source repositories" do
  command "sed -i 's/^# deb-src/deb-src/g' /etc/apt/sources.list"
  user "root"
  action :run
  not_if 'grep "^deb-src" /etc/apt/sources.list'
end

# disable autoremove because it fails as it cannot remove
#   linux-image-extra-4.4.0-71-generic
file '/etc/apt/apt.conf' do
  content 'APT::Get::AutomaticRemove "0"; APT::Get::HideAutoRemove "1";'
  mode '0755'
  owner 'root'
  group 'root'
  notifies :run, 'execute[initial apt-get update]',:immediately
  not_if {File.exists?("/etc/apt/apt.conf")}
end

execute "periodic apt-get update" do
  command "apt-get update"
  user "root"
  action :run
  not_if 'find /var/lib/apt/periodic/update-success-stamp -mmin -180 | grep update-success-stamp'
    # do not run if updated less than 3 hours ago
end

# This is to work around a no longer needed workaround which accidentally prevents
# updating packages in the image (see https://github.com/chef/bento/issues/592)
bash "fix broken persistent names hack" do
  code <<-EOH
    rm -rf /etc/udev/rules.d/70-persistent-net.rules
    touch /etc/udev/rules.d/70-persistent-net.rules
    apt-get -y -f install
  EOH
  user "root"
  action :run
  not_if 'test -f /etc/udev/rules.d/70-persistent-net.rules'
end

execute "install R (dev) build deps" do
  command "apt-get build-dep -y r-base-dev"
  user "root"
  action :run
  not_if 'dpkg --get-selections | grep -q "^xvfb\s"'
  timeout 3600
end

  # now also needed to build R
["libcurl4-openssl-dev"].each do |pkg|
  package pkg do
    action :install
    not_if 'dpkg --get-selections | grep -q "^#{pkg}\s"'
  end
end

directory "/opt" do
  owner "root"
  group "root"
  mode "0755"
  action :create
end

#
# # install LLVM
#
# llvmtarbase = "clang+llvm-3.6.1-x86_64-linux-gnu-ubuntu-15.04.tar.xz"
# llvmtarfile = "/opt/#{llvmtarbase}"
# llvmdir = "/opt/clang+llvm-3.6.1-x86_64-linux-gnu"
#
# remote_file llvmtarfile do
#   source "http://llvm.org/releases/3.6.1/#{llvmtarbase}"
#   not_if {File.exists?("#{llvmdir}/bin/clang")}
# end
#
# execute "unpack LLVM" do
#   command "tar xf #{llvmtarfile}"
#   cwd "/opt"
#   user "root"
#   action :run
#   not_if {File.exists?("#{llvmdir}/bin/clang")}
# end
#
# file llvmtarfile do
#   action :delete
# end

# install LLVM

["clang-3.8 llvm-3.8-dev clang\+\+-3.8 clang llvm-dev libc++-dev libc++abi-dev"].each do |pkg|
  package pkg do
    action :install
    not_if 'dpkg --get-selections | grep -q "^#{pkg}\s"'
    timeout 3600
  end
end

llvmdir = "/usr"

# Fix a bug in C++ string header file (libc++-dev)
# (see https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=808086)
bash "fix broken string header" do
  code <<-EOH
    cp -p /usr/include/c++/v1/string /root/string.orig
    cd /usr/include/c++/v1/
    patch -p1 < /vagrant/string.diff
  EOH
  user "root"
  action :run
  only_if 'md5sum /usr/include/c++/v1/string | grep bc206c6dee334d9d8d1cdcf32df6a92b'
end


# install whole-program-llvm

wllvmsrcdir = "/opt/whole-program-llvm"
git wllvmsrcdir do
  repository "git://www.github.com/travitch/whole-program-llvm"
  revision "16e4fa62dc8f91ca9a3d5416424a6583248d6cce"
  action :export
  user "root"
  not_if {File.exists?("#{wllvmsrcdir}/wllvm")}
end

 # pip needed to install wllvm

["python-pip"].each do |pkg|
  package pkg do
    action :install
    not_if 'dpkg --get-selections | grep -q "^#{pkg}\s"'
  end
end

 # upgrade needed to install wllvm

execute "pip upgrade" do
  command "sudo -u #{username} -H pip install --user pip==9.0.1"
  user "#{username}"
  action :run
  not_if 'pip --version | grep 9.0.1'
end

wllvmdir = "/home/#{username}/.local/bin"

execute "wllvm install" do
  command "sudo -u #{username} -H pip install --user #{wllvmsrcdir}"
  user "#{username}"
  action :run
  not_if {File.exists?("#{wllvmdir}/wllvm")}
end

# install rchk

rchkdir = "/opt/rchk"
bcheck = "#{rchkdir}/src/bcheck"

git rchkdir do
  repository "git://www.github.com/kalibera/rchk"
  revision "97ceff3509ba01fd98333a063217cf304ce9009c"
  action :export
  user "root"
  not_if {File.exists?("#{rchkdir}/src")}
end

makeargs = ""
if bcheck_max_states > 0
  makeargs.concat("BCHECK_MAX_STATES=#{bcheck_max_states}")
end
if callocators_max_states > 0
  makeargs.concat(" CALLOCATORS_MAX_STATES=#{callocators_max_states}")
end

execute "make rchk" do
  command "make LLVM=#{llvmdir} CXX=g++ #{makeargs}"
  cwd "#{rchkdir}/src"
  user "root"
  action :run
  not_if {File.exists?(bcheck)}
end

# now based on hostname in config.inc
#
# execute "configure rchk - llvm dir" do
#   command "sed -i 's|\\( LLVM=\\).*|\\1#{llvmdir}|g' #{rchkdir}/scripts/config.inc"
#   user "root"
#   action :run
# end
#
# execute "configure rchk - wllvm dir" do
#   command "sed -i 's|\\( WLLVM=\\).*|\\1#{wllvmdir}|g' #{rchkdir}/scripts/config.inc"
#   user "root"
#   action :run
# end
#
# execute "configure rchk - rchk dir" do
#   command "sed -i 's|\\( RCHK=\\).*|\\1#{rchkdir}|g' #{rchkdir}/scripts/config.inc"
#   user "root"
#   action :run
# end


# install more packages

["subversion","git"].each do |pkg|
  package pkg do
    action :install
    not_if 'dpkg --get-selections | grep -q "^#{pkg}\s"'
  end
end
