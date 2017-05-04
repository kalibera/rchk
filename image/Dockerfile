FROM ubuntu:16.04
ENV DEBIAN_FRONTEND noninteractive

# ssh server and other tools
RUN apt-get update && apt-get install -y apt-utils
RUN DEBIAN_FRONTEND=noninteractive apt-get update && apt-get install -y \
  openssh-server \
  openssh-client \
  lsb-release \
  sudo \
  git \
  language-pack-en-base \
  && rm -rf /var/lib/apt/lists/*

RUN locale-gen en_US.UTF-8
ENV LANG='en_US.UTF-8' LANGUAGE='en_US:en' LC_ALL='en_US.UTF-8'

# root user
RUN echo 'root:vagrant' | chpasswd

# vagrant user and password-less sudo for vagrant user
RUN useradd -m -s /bin/bash vagrant && \
    echo 'vagrant:vagrant' | chpasswd && \
    echo 'vagrant ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/99_vagrant && \
    chmod 440 /etc/sudoers.d/99_vagrant

# install public ssh key of vagrant
#   downloaded from https://raw.githubusercontent.com/mitchellh/vagrant/master/keys/vagrant.pub
# vagrant automatically replaces this key on installation

RUN mkdir /home/vagrant/.ssh
ADD vagrant.pub /home/vagrant/.ssh/authorized_keys
RUN chown -R vagrant /home/vagrant/.ssh && \
  chmod -R go-rwsx /home/vagrant/.ssh && \
  mkdir /var/run/sshd && chmod 0755 /var/run/sshd

ENV DEBIAN_FRONTEND teletype
EXPOSE 22
CMD ["/usr/sbin/sshd", "-D"]
