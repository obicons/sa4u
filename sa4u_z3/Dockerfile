FROM python:3.10

RUN apt-get update                      \
    && apt install -y                   \
           build-essential              \
           software-properties-common   \
    && wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -                                     \
    && add-apt-repository "deb http://apt.llvm.org/focal/     llvm-toolchain-focal-14    main"                  \
    && apt-get update                                                                                           \
    && apt install -y clang-14 libclang-14-dev                                                                  \
    && rm -rf /var/lib/apt/lists/*

RUN ln -s /usr/bin/clang-14 /usr/bin/clang

ADD requirements.txt /sa4u-src/requirements.txt
WORKDIR /sa4u-src/
RUN pip3 install -r requirements.txt

ADD . /sa4u-src/

RUN cp /sa4u-src/sa4u /bin/ \
    && chmod +x /bin/sa4u

ENTRYPOINT ["/bin/sa4u"]
