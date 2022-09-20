FROM ubuntu:20.04

RUN apt-get update && \
    apt-get install -y keyboard-configuration

ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update                                                                              \
    && apt-get install -y                                                                       \
    build-essential                                                                             \
    clang                                                                                       \
    cmake                                                                                       \
    git                                                                                         \
    lsb-release                                                                                 \
    wget                                                                                        \
    bear                                                                                        \
    python3                                                                                     \
    software-properties-common                                                                  \
    sudo                                                                                        \
    && wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -                \
    && add-apt-repository "deb http://apt.llvm.org/focal/     llvm-toolchain-focal-14 main"     \
    && apt-get update                                                                           \
    && apt-get install -y libclang-14-dev                                                       \
    && rm -rf /var/lib/apt/lists/*

RUN adduser ardupilot             \
    && usermod -aG sudo ardupilot \
    && echo "ardupilot ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers
WORKDIR /home/ardupilot/
USER ardupilot
ENV USER ardupilot

# ADD OpenUxAS /home/ardupilot/OpenUxAS
# ARG UXAS_SHA="a41e69c9d923ae4795a99fd8201a32f6bc81f4e1"
# RUN git clone https://github.com/afrl-rq/OpenUxAS /home/ardupilot/OpenUxAS
# WORKDIR /home/ardupilot/OpenUxAS
# RUN git checkout ${UXAS_SHA}

# RUN sudo chown -R ardupilot:ardupilot /home/ardupilot/OpenUxAS
# RUN bash -c 'yes | bear ./anod build uxas || true'

RUN sudo apt-get update && sudo apt-get install -y python3.9 python3.9-distutils pkg-config vim
RUN wget -O- 'https://bootstrap.pypa.io/get-pip.py' | python3.9

ARG SA4U_SHA="4b3031ff27a5b671679f8d791a216b14ec273247"
RUN git clone https://github.com/obicons/sa4u /home/ardupilot/sa4u
WORKDIR /home/ardupilot/sa4u/sa4u_z3
RUN git checkout ${SA4U_SHA}
RUN python3.9 -m pip install -r requirements.txt

ADD CMASI.xml /home/ardupilot/CMASI.xml
ADD types.json /home/ardupilot/types.json
ADD ignore_list /home/ardupilot/ignore_list

# && mv compile_commands.json compile_commands_uxas.json
# && bear ./anod build amase                              \
# && mv compile_commands.json compile_commands_amase.json \