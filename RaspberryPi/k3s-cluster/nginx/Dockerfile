FROM nginx:latest

# 기본 설정 파일 삭제 후 우리가 만든 설정 파일 복사
RUN rm /etc/nginx/conf.d/default.conf
COPY nginx.conf /etc/nginx/nginx.conf

# 인증서 디렉터리 생성 및 파일 복사
# Jenkins 빌드 과정에서 'certs' 폴더가 빌드 컨텍스트에 생성됨
RUN mkdir -p /etc/nginx/certs
COPY certs/server.crt /etc/nginx/certs/
COPY certs/server.key /etc/nginx/certs/
COPY certs/rootCA.crt /etc/nginx/certs/

EXPOSE 80 443 8554 8883
