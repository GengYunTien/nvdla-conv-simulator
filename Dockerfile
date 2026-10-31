# Stage 1: Build Verilator v4.228
FROM ubuntu:22.04 AS build-verilator
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    git make autoconf g++ flex bison libfl2 libfl-dev zlib1g-dev python3 \
 && rm -rf /var/lib/apt/lists/*
ARG VERILATOR_VERSION=v4.228
RUN git clone https://github.com/verilator/verilator.git /tmp/verilator \
 && cd /tmp/verilator \
 && git checkout ${VERILATOR_VERSION} \
 && autoconf \
 && ./configure --prefix=/opt/verilator \
 && make -j$(nproc) \
 && make install

# Stage 2: Build SystemC 2.3.4
FROM ubuntu:22.04 AS build-systemc
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y git cmake g++ make \
 && rm -rf /var/lib/apt/lists/*
RUN git clone --branch 2.3.4_pub_rev_20190614 --depth 1 \
    https://github.com/accellera-official/systemc.git /tmp/systemc-src \
 && mkdir -p /tmp/systemc-src/docs/tlm \
 && touch /tmp/systemc-src/docs/tlm/ChangeLog \
 && cmake -S /tmp/systemc-src -B /tmp/systemc-src/build \
         -DCMAKE_INSTALL_PREFIX=/opt/systemc \
         -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_CXX_STANDARD=17 \
 && cmake --build /tmp/systemc-src/build -j$(nproc) \
 && cmake --install /tmp/systemc-src/build

# Stage 3: Final dev image
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    git make cmake g++ flex bison \
    clang \
    perl libyaml-perl libxml-simple-perl libio-tee-perl libcapture-tiny-perl \
    python2 python3 \
    default-jdk \
    libfl2 zlib1g wget curl vim \
 && rm -rf /var/lib/apt/lists/*

RUN ln -s /usr/bin/python3 /usr/local/bin/python

COPY --from=build-verilator /opt/verilator /opt/verilator
COPY --from=build-systemc   /opt/systemc   /opt/systemc

RUN ln -s /opt/verilator/share/verilator/include /opt/verilator/include

ENV PATH="/opt/verilator/bin:$PATH"
ENV VERILATOR_ROOT=/opt/verilator
ENV SYSTEMC_HOME=/opt/systemc
ENV LD_LIBRARY_PATH="/opt/systemc/lib:$LD_LIBRARY_PATH"
ENV CPLUS_INCLUDE_PATH="/opt/systemc/include"

ENV JAVA=java \
    PERL=/usr/bin/perl \
    CPP=cpp \
    VERILATOR=/opt/verilator/bin/verilator \
    CLANG=clang \
    VERILATOR_PARAMS="--compiler clang --output-split 250000000 -Wno-WIDTH -Wno-LITENDIAN"

WORKDIR /workspace
CMD ["/bin/bash"]