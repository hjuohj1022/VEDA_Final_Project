# -------------------------------------------------------
# 1단계: 빌드 (Builder Stage)
# -------------------------------------------------------
FROM hjuohj/crow-base:latest AS builder

# 필요한 추가 빌드 의존성 설치
RUN apt-get update && apt-get install -y \
    pkg-config \
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

# 의존성은 이미 베이스 이미지에 있으므로 소스 복사 후 바로 빌드
WORKDIR /app
COPY . .

# 빌드 수행 (-j$(nproc)를 사용하여 병렬 빌드 속도 향상)
RUN mkdir -p build && cd build && \
    cmake .. && \
    make -j$(nproc)

# -------------------------------------------------------
# 2단계: 실행 (Runtime Stage)
# -------------------------------------------------------
FROM ubuntu:22.04

# 실행할 때 필요한 런타임 라이브러리만 설치
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
