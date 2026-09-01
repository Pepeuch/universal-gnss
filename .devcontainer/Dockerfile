# syntax=docker/dockerfile:1

ARG ROS_DISTRO=kilted
FROM ros:${ROS_DISTRO}-ros-base

ARG ROS_DISTRO

ENV DEBIAN_FRONTEND=noninteractive
ENV ROS_DISTRO=${ROS_DISTRO}

SHELL ["/bin/bash", "-lc"]

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    bash-completion \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    git \
    gpg \
    python3-colcon-common-extensions \
    python3-pip \
    python3-rosdep \
    python3-vcstool \
 && rm -rf /var/lib/apt/lists/*

# Keep formatting stable across host distributions and future ROS base images.
RUN . /etc/os-release \
 && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
    | gpg --dearmor -o /usr/share/keyrings/apt.llvm.org.gpg \
 && echo "deb [signed-by=/usr/share/keyrings/apt.llvm.org.gpg] http://apt.llvm.org/${VERSION_CODENAME}/ llvm-toolchain-${VERSION_CODENAME}-21 main" \
    > /etc/apt/sources.list.d/llvm-21.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends clang-format-21 \
 && rm -rf /var/lib/apt/lists/*

RUN echo "source /opt/ros/${ROS_DISTRO}/setup.bash" >> /etc/bash.bashrc

WORKDIR /workspaces/universal-gnss
