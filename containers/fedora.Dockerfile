FROM fedora:42

RUN dnf install -y \
        cairo-devel \
        cpio \
        clang \
        clang-tools-extra \
        cmake \
        gcc \
        gcc-c++ \
        glib2-devel \
        libxcb-devel \
        libxkbcommon-x11-devel \
        ninja-build \
        pango-devel \
        pkgconf-pkg-config \
        rpm-build \
        shared-mime-info \
        xorg-x11-server-Xvfb \
        xorg-x11-xauth \
        xrandr \
    && dnf clean all

ENV HOME=/tmp
WORKDIR /workspace
CMD ["./scripts/container-check.sh"]
