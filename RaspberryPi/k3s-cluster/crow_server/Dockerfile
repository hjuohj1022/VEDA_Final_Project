# -------------------------------------------------------
# 1단계: 빌드 (Builder Stage)
# -------------------------------------------------------
FROM hjuohj/crow-base:latest

WORKDIR /app
COPY . .

# 빌드 수행
RUN mkdir build && cd build && \
    cmake .. && \
    make

# -------------------------------------------------------
# 2단계: 실행 (Runtime Stage)
# -------------------------------------------------------
FROM ubuntu:22.04

# 편의상 dev 패키지를 설치하거나 런타임 라이브러리를 설치합니다.
RUN apt-get update && apt-get install -y \
    libboost-system-dev \
    libboost-thread-dev \
    libmosquitto1 \
    libmosquittopp1 \
    && rm -rf /var/lib/apt/lists/*

# 실행 파일 복사
COPY --from=builder /app/build/crow_server /usr/local/bin/crow_server

# 포트 개방
EXPOSE 8080

# 실행
CMD ["/usr/local/bin/crow_server"]

