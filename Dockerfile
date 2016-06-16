FROM debian:latest
MAINTAINER Preetam D'Souza <preetamjdsouza@gmail.com>

RUN apt-get update && apt-get install -y \
    build-essential \
    debhelper \
    devscripts \
    dh-make \
    dh-systemd \
    nano \
&& apt-get clean \
&& rm -rf /var/lib/apt/lists/*
