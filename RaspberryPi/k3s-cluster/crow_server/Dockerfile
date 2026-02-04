# [Stage 1: Build]
FROM ubuntu:22.04 AS builder

# 모스키토 라이브러리 설치 (libmosquitto-dev)
RUN apt-get update && apt-get install -y \
    g++ cmake make libboost-all-dev \
    libmosquitto-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 소스 전체 복사
COPY . .

# CMake로 빌드
RUN mkdir build && cd build && cmake .. && make

# ----------------------------------------

# [Stage 2: Run]
FROM ubuntu:22.04

# 실행 시 필요한 런타임 라이브러리 설치 (dev 아님)
RUN apt-get update && apt-get install -y \
    libmosquitto1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 빌드된 실행 파일만 가져오기
COPY --from=builder /app/build/crow_server .

EXPOSE 8080

CMD ["./crow_server"]
