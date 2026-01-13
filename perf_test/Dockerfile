FROM ros:humble

# Install build tools and network utilities
RUN apt-get update && apt-get install -y \
    python3-colcon-common-extensions \
    iproute2 \
    && rm -rf /var/lib/apt/lists/*

# Create workspace
WORKDIR /ws
COPY . /ws/src/reliable_comm

# Build
RUN . /opt/ros/humble/setup.sh && \
    colcon build --packages-select reliable_comm

# Entrypoint
COPY perf_test/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
