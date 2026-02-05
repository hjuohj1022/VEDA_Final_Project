FROM mariadb:10.6

# 시간대 설정
ENV TZ=Asia/Seoul

# UTF-8MB4 설정 (한글/이모지 지원)
# Kubernetes args에서 설정할 수도 있지만, Dockerfile CMD로 기본값 지정
CMD ["mysqld", "--character-set-server=utf8mb4", "--collation-server=utf8mb4_unicode_ci"]
