FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    PIP_NO_CACHE_DIR=1

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

RUN groupadd -g 1001 quera && \
    useradd -m -u 1001 -g 1001 -s /bin/bash quera

WORKDIR /workspace
COPY . /workspace
RUN chown -R quera:quera /workspace

RUN python3 -m venv /opt/venv
RUN chown -R quera:quera /opt/venv
ENV PATH=/opt/venv/bin:$PATH

USER quera
ENV HOME=/home/quera
ENV PATH=$HOME/.local/bin:$PATH

WORKDIR /workspace
RUN pip install --upgrade pip setuptools wheel
RUN pip install --no-cache-dir ./python

EXPOSE 8080
CMD ["python3", "/workspace/python/scripts/vm_service.py", "--host", "0.0.0.0", "--port", "8080"]
