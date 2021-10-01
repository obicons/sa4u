FROM ubuntu:20.04

RUN apt-get update          \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y   \
        bear			      	\
        build-essential     	      	\
        cmake				\
        git				\
        python3				\
        re2c				\
	software-properties-common	\
	sudo				\
	wget				\
    && wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -					\
    && add-apt-repository "deb http://apt.llvm.org/focal/     llvm-toolchain-focal$LLVM_VERSION_STRING    main" \
    && apt-get update                                                                                           \
    && apt-get install -y libclang-14-dev llvm-14-dev								\
    && rm -rf /var/lib/apt/lists/*

RUN adduser rewriter              \
    && usermod -aG sudo rewriter  \
    && echo "rewriter ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

WORKDIR /home/rewriter/
USER rewriter
ENV USER rewriter
