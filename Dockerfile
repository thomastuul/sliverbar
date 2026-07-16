FROM debian:13-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        clang \
        clang-format \
        clang-tidy \
        cmake \
        libcairo2-dev \
        libpango1.0-dev \
        libxcb1-dev \
        ninja-build \
        pkg-config \
        xauth \
        xvfb \
    && rm -rf /var/lib/apt/lists/*

ENV HOME=/tmp
WORKDIR /workspace

CMD ["./scripts/container-check.sh"]
