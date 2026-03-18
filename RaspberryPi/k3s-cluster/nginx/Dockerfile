FROM nginx:latest

# 기본 설정 파일을 제거하고 우리가 만든 설정 템플릿을 복사
RUN rm /etc/nginx/conf.d/default.conf
COPY nginx.conf /etc/nginx/nginx.conf.template

# 인증서가 마운트될 디렉터리 생성
RUN mkdir -p /etc/nginx/certs

EXPOSE 80 443 8554 8883

# nginx 내장 변수($host, $http_upgrade 등)를 보존하기 위해 템플릿을 그대로 사용
CMD ["/bin/sh", "-c", "cp /etc/nginx/nginx.conf.template /etc/nginx/nginx.conf && exec nginx -g 'daemon off;'"]
