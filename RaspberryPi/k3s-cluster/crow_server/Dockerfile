# -------------------------------------------------------
# 1단계: 빌드 (Builder Stage)
# -------------------------------------------------------
FROM hjuohj/crow-base:latest AS builder

# 기존 설치된 asio 제거 시도 및 필수 라이브러리만 설치
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libboost-all-dev \
    libssl-dev \
    libmosquitto-dev \
    libmosquittopp-dev \
    libmysqlclient-dev \
    libcurl4-openssl-dev \
    pkg-config \
    && apt-get purge -y libasio-dev || true \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# 빌드 수행
RUN mkdir -p build && cd build && \
    cmake .. && \
    make

# -------------------------------------------------------
# 2단계: 실행 (Runtime Stage)
# -------------------------------------------------------
FROM ubuntu:22.04

# 실행할 때 필요한 라이브러리 설치 (asio 관련 패키지 제외)
RUN apt-get update && apt-get install -y \
    libboost-system-dev \
    libboost-thread-dev \
    libmosquitto1 \
    libmosquittopp1 \
    libmariadb3 \
    libmysqlclient-dev \
    libcurl4 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/build/crow_server ./crow_server
# Swagger 파일 복사
COPY swagger ./swagger

# 포트 개방
EXPOSE 8080

# 실행
CMD ["./crow_server"]
