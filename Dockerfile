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
        libxkbcommon-x11-dev \
        libxcb1-dev \
        libxcb-randr0-dev \
        ninja-build \
        pkg-config \
        xauth \
        x11-xserver-utils \
        xvfb \
    && rm -rf /var/lib/apt/lists/*

ENV HOME=/tmp
WORKDIR /workspace

CMD ["./scripts/container-check.sh"]
