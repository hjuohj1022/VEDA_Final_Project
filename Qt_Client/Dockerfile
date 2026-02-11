FROM ubuntu:22.04

# Avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    qt6-base-dev \
    qt6-multimedia-dev \
    libvlc-dev \
    libvlccore-dev \
    pkg-config \
    libgl1-mesa-dev \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# (Optional) Pre-configure environment variables if needed
ENV QT_SELECT=qt6
