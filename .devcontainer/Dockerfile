# syntax=docker/dockerfile:1

ARG ROS_DISTRO=lyrical
FROM ros:${ROS_DISTRO}-ros-base

ARG ROS_DISTRO

ARG MAVROS_VERSION=2.16.0
ARG MAVROS_COMMIT=5c68b905ab30de6ce630822dc46c33467e8f23ea
ARG MAVLINK_GBP_BRANCH=release/rolling/mavlink


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
    software-properties-common \
    sudo \
 && add-apt-repository -y universe \
 && apt-get update \
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

# ROS 2 Lyrical does not provide the MAVLink package required by this
# pinned MAVROS baseline in every environment. Build MAVLink and MAVROS
# from source so the development image is deterministic.
RUN set -eux; \
    apt-get update; \
    rosdep init 2>/dev/null || true; \
    rosdep update --rosdistro "${ROS_DISTRO}"; \
    mkdir -p /opt/mavros_ws/src; \
    git clone \
      --depth 1 \
      --branch "${MAVLINK_GBP_BRANCH}" \
      https://github.com/ros2-gbp/mavlink-gbp-release.git \
      /opt/mavros_ws/src/mavlink; \
    git clone \
      https://github.com/mavlink/mavros.git \
      /opt/mavros_ws/src/mavros; \
    cd /opt/mavros_ws/src/mavros; \
    git fetch --depth 1 origin "${MAVROS_COMMIT}"; \
    git checkout --detach "${MAVROS_COMMIT}"; \
    test "$(sed -n 's:.*<version>\(.*\)</version>.*:\1:p' mavros/package.xml | head -n1)" = \
      "${MAVROS_VERSION}"; \
    rosdep install \
      --from-paths /opt/mavros_ws/src \
      --ignore-src \
      --rosdistro "${ROS_DISTRO}" \
      --skip-keys "ament_cmake_google_benchmark google-mock" \
      -r -y; \
    set +u; source "/opt/ros/${ROS_DISTRO}/setup.bash"; set -u; \
    cd /opt/mavros_ws; \
    colcon build \
      --merge-install \
      --install-base /opt/mavros \
      --packages-up-to mavros mavros_extras \
      --cmake-args -DBUILD_TESTING=OFF; \
    /opt/mavros/lib/mavros/install_geographiclib_datasets.sh; \
    rm -rf /opt/mavros_ws/build /opt/mavros_ws/log /var/lib/apt/lists/*

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
    apt-get update; \
    if getent group 126 >/dev/null; then \
        docker_group="$(getent group 126 | cut -d: -f1)"; \
    else \
        groupadd --gid 126 docker-host; \
        docker_group=docker-host; \
    fi; \
    usermod -aG "${docker_group}" ubuntu

RUN printf '%s\n' \
    "source /opt/ros/${ROS_DISTRO}/setup.bash" \
    "source /opt/mavros/setup.bash" \
    >> /etc/bash.bashrc

WORKDIR /workspaces/universal-gnss
