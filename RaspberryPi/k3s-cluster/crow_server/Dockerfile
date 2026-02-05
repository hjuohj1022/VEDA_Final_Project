# -------------------------------------------------------
# 1단계: 빌드 (Builder Stage)
# -------------------------------------------------------
FROM hjuohj/crow-base:latest AS builder

WORKDIR /app

# 필요한 개발 라이브러리 설치 (mosquitto headers 등)
RUN apt-get update && apt-get install -y \
    libmosquitto-dev \
    libmosquittopp-dev \
    && rm -rf /var/lib/apt/lists/*

COPY . .

# 빌드 수행
RUN mkdir build && cd build && \
    cmake .. && \
    make

# -------------------------------------------------------
# 2단계: 실행 (Runtime Stage)
# -------------------------------------------------------
FROM ubuntu:22.04

# [설명] 실행할 때 필요한 라이브러리만 설치합니다.
RUN apt-get update && apt-get install -y \
    libboost-system-dev \
    libboost-thread-dev \
    libmosquitto1 \
    libmosquittopp1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/crow_server /usr/local/bin/crow_server

# 포트 개방
EXPOSE 8080

# 실행
CMD ["/usr/local/bin/crow_server"]