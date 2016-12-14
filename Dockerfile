FROM debian:latest
MAINTAINER Preetam D'Souza <preetamjdsouza@gmail.com>

RUN apt-get update && apt-get install -y \
    build-essential \
    debhelper \
    devscripts \
    dh-make \
    dh-systemd \
    git-buildpackage \
    nano

# set up cross-toolchain archive (only needed for Jessie)
RUN echo 'deb http://emdebian.org/tools/debian/ jessie main' > /etc/apt/sources.list.d/crosstools.list \
    && curl http://emdebian.org/tools/debian/emdebian-toolchain-archive.key | apt-key add -

# install cross-toolchains
RUN dpkg --add-architecture armhf \
    && dpkg --add-architecture arm64 \
    && apt-get update \
    && apt-get install -y crossbuild-essential-armhf crossbuild-essential-arm64

# mclient build dependencies
# multiarch libs are done in a separate apt-get command to avoid weird conflicts
# NOTE: libxi-dev is broken and always removes the previous installed architecture...
RUN apt-get install -y \
    libx11-dev \
    libxfixes-dev \
    libxext-dev \
    libxdamage-dev \
    libxi-dev \
    libxrandr-dev \
&& apt-get install -y \
    libx11-dev:armhf \
    libxfixes-dev:armhf \
    libxext-dev:armhf \
    libxdamage-dev:armhf \
    libxi-dev:armhf \
    libxrandr-dev:armhf \
&& apt-get install -y \
    libx11-dev:arm64 \
    libxfixes-dev:arm64 \
    libxext-dev:arm64 \
    libxdamage-dev:arm64 \
    libxi-dev:arm64 \
    libxrandr-dev:arm64

RUN apt-get clean && rm -rf /var/lib/apt/lists/*

# set up workspace
ENV MARU_WORKSPACE /var/mworkspace
RUN mkdir -p ${MARU_WORKSPACE}
WORKDIR ${MARU_WORKSPACE}
COPY . ${MARU_WORKSPACE}

# create a non-root user
ARG user=dev
ARG group=dev
ARG uid=1000
ARG gid=1000
RUN groupadd -g ${gid} ${group} \
    && useradd -u ${uid} -g ${gid} -m -s /bin/bash ${user} \
    && echo "${user} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/90-passwordless-sudo

# drop root
USER ${user}
