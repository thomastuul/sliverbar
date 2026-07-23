FROM archlinux:base

RUN pacman -Syu --noconfirm \
        base-devel \
        cairo \
        clang \
        cmake \
        evolution-data-server \
        glib2 \
        libxcb \
        libxkbcommon-x11 \
        ninja \
        pango \
        pkgconf \
        xorg-server-xvfb \
        xorg-xauth \
        xorg-xrandr \
    && pacman -Scc --noconfirm

ENV HOME=/tmp
WORKDIR /workspace
CMD ["./scripts/container-check.sh"]
