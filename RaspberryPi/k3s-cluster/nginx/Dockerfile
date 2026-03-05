FROM nginx:latest

# 기본 설정 파일을 제거하고 우리가 만든 설정 템플릿을 복사
RUN rm /etc/nginx/conf.d/default.conf
COPY nginx.conf /etc/nginx/nginx.conf.template

# 인증서가 마운트될 디렉터리 생성
RUN mkdir -p /etc/nginx/certs

EXPOSE 80 443 8554 8883

# 추가: 컨테이너 시작 시 템플릿의 ${VAR}를 실제 환경변수로 치환
CMD ["/bin/sh", "-c", "envsubst < /etc/nginx/nginx.conf.template > /etc/nginx/nginx.conf && exec nginx -g 'daemon off;'"]
