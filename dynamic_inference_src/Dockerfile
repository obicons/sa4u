FROM ubuntu:20.04

RUN apt-get update && \
    apt-get install -y keyboard-configuration

ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update                                                                                              \
    && apt-get install -y                                                                                       \
           build-essential                                                                                      \
           cmake                                                                                                \
           git                                                                                                  \
           lsb-release                                                                                          \
           wget                                                                                                 \
           software-properties-common                                                                           \
           sudo

RUN adduser ardupilot             \
    && usermod -aG sudo ardupilot \
    && echo "ardupilot ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers
WORKDIR /home/ardupilot/
USER ardupilot
ENV USER ardupilot

RUN wget https://raw.githubusercontent.com/ArduPilot/ardupilot/master/Tools/environment_install/install-prereqs-ubuntu.sh \
    && chmod +x install-prereqs-ubuntu.sh                                                                                 \
    && sed -i 's/$APT_GET install/$APT_GET install -y/g' install-prereqs-ubuntu.sh                                        \
    && sed -i 's/$APT_GET remove/$APT_GET remove -y/g' install-prereqs-ubuntu.sh                                          \
    && ./install-prereqs-ubuntu.sh                                                                                        \
    && rm install-prereqs-ubuntu.sh

RUN git clone https://github.com/ArduPilot/ardupilot \
    && cd ardupilot/                                 \
    && git submodule update --init --recursive       \
    && ./waf configure                               \
    && make copter

VOLUME [ "/home/ardupilot/valgrind_src" ]
