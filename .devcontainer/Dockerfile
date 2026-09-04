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
    docker.io \
    git \
    gpg \
    python3-colcon-common-extensions \
    python3-pip \
    python3-rosdep \
    python3-vcstool \
    sudo \
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

# Development commands and their generated artifacts must use this identity.
RUN set -eux; \
 if getent group ubuntu > /dev/null; then \
   ubuntu_gid="$(getent group ubuntu | cut -d: -f3)"; \
   [ "${ubuntu_gid}" = "1000" ] || { \
     echo "group 'ubuntu' exists with unexpected gid ${ubuntu_gid}" >&2; \
     exit 1; \
   }; \
 else \
   getent group 1000 > /dev/null && { \
     echo "gid 1000 is already assigned to another group" >&2; \
     exit 1; \
   }; \
   groupadd --gid 1000 ubuntu; \
 fi; \
 if id -u ubuntu > /dev/null 2>&1; then \
   ubuntu_uid="$(id -u ubuntu)"; \
   ubuntu_primary_gid="$(id -g ubuntu)"; \
   [ "${ubuntu_uid}" = "1000" ] || { \
     echo "user 'ubuntu' exists with unexpected uid ${ubuntu_uid}" >&2; \
     exit 1; \
   }; \
   [ "${ubuntu_primary_gid}" = "1000" ] || { \
     echo "user 'ubuntu' exists with unexpected primary gid ${ubuntu_primary_gid}" >&2; \
     exit 1; \
   }; \
 else \
   getent passwd 1000 > /dev/null && { \
     echo "uid 1000 is already assigned to another user" >&2; \
     exit 1; \
   }; \
   useradd --uid 1000 --gid ubuntu --create-home --shell /bin/bash ubuntu; \
 fi

RUN echo "ubuntu ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/ubuntu \
 && chmod 0440 /etc/sudoers.d/ubuntu

# Allow the non-root development user to access the host Docker socket.
RUN set -eux; \
    if getent group 126 >/dev/null; then \
        docker_group="$(getent group 126 | cut -d: -f1)"; \
    else \
        groupadd --gid 126 docker-host; \
        docker_group=docker-host; \
    fi; \
    usermod -aG "${docker_group}" ubuntu

RUN echo "source /opt/ros/${ROS_DISTRO}/setup.bash" >> /etc/bash.bashrc

WORKDIR /workspaces/universal-gnss
