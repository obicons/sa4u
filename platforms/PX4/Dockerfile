FROM ubuntu:20.04

RUN apt-get update && \
    apt-get install -y keyboard-configuration

ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update												\
    && apt-get install -y											\
           build-essential											\
           cmake												\
           git													\
           lsb-release												\
           wget													\
	   software-properties-common										\
           sudo													\
    && wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -                                \
    && add-apt-repository "deb http://apt.llvm.org/focal/     llvm-toolchain-focal$LLVM_VERSION_STRING    main" \
    && apt-get update                                                                                           \
    && apt-get install -y libclang-14-dev									\
    && rm -rf /var/lib/apt/lists/*

RUN adduser ardupilot             \
    && usermod -aG sudo ardupilot \
    && echo "ardupilot ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers
WORKDIR /home/ardupilot/
USER ardupilot
ENV USER ardupilot

RUN git clone https://github.com/PX4/PX4-Autopilot.git \
    && cd PX4-Autopilot/			       \
    && git submodule update --init --recursive         \
    && cd ./Tools/setup/    	   		       \
    && chmod +x ./ubuntu.sh			       \
    && ./ubuntu.sh				       \
    && cd ../../ 				       \
    && make px4_sitl
