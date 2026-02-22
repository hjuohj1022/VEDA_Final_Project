FROM nginx:latest

# 기본 설정 파일 삭제 후 우리가 만든 설정 파일 복사
RUN rm /etc/nginx/conf.d/default.conf
COPY nginx.conf /etc/nginx/nginx.conf

# 인증서가 마운트될 디렉터리만 생성
RUN mkdir -p /etc/nginx/certs

EXPOSE 80 443 8554 8883
