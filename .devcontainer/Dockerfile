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
    git \
    python3-colcon-common-extensions \
    python3-pip \
    python3-rosdep \
    python3-vcstool \
 && rm -rf /var/lib/apt/lists/*

RUN echo "source /opt/ros/${ROS_DISTRO}/setup.bash" >> /etc/bash.bashrc

WORKDIR /workspaces/universal-gnss
