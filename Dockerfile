FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    PIP_NO_CACHE_DIR=1

# System dependencies needed to build the C++ extension and run Python.
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        libssl-dev \
        ninja-build \
        python3 \
        python3-dev \
        python3-pip \
        python3-venv && \
    rm -rf /var/lib/apt/lists/*

# repo2docker will create and use a notebook user; just install into /srv.
WORKDIR /srv/repo

# Copy the repo contents into the image.
COPY . /srv/repo

# Install the Neutral Atom VM Python package (builds the C++ extension).
RUN pip install --upgrade pip setuptools wheel && \
    pip install --no-cache-dir ./python

# Do not set CMD/ENTRYPOINT: repo2docker/Binder will launch Jupyter itself.

