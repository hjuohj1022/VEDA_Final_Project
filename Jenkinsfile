pipeline {
    agent any
    environment {
        // 본인 설정에 맞게 수정
        GIT_URL = 'https://github.com/hjuohj1022/VEDA_Final_Project.git' 
        DOCKER_CRED = 'docker-hub-login'
        KUBE_CONFIG = 'k3s-kubeconfig'
    }
    stages {
        stage('소스 가져오기') {
            steps {
                slackSend (
                    channel: 'C0ADS8RQAL9', 
                    color: '#439FE0', // 파란색 (시작 알림용)
                    botUser: true, 
                    message: "🚀 배포 시작: ${env.JOB_NAME} #${env.BUILD_NUMBER} (<${env.BUILD_URL}|상세보기>)"
                )
                git branch: 'develop', url: GIT_URL
            }
        }
        
        // 🦅 1. Crow Server (폴더명: crow_server)
        stage('Crow Server 배포') {
            when { changeset 'RaspberryPi/k3s-cluster/crow_server/**' } // 폴더명 주의!
            steps {
                script {
                    dir('RaspberryPi/k3s-cluster/crow_server') { // 이 폴더로 들어가서 빌드해라
                        echo "🦅 Crow Server 빌드 시작..."
                        withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                            sh "echo $PASS | docker login -u $USER --password-stdin"
                            sh "docker buildx build --platform linux/arm64 -t hjuohj/crow-server:latest --push ."
                        }
                    }
                    // 배포 명령 (yaml 파일도 각 폴더 안에 있다고 가정하면 경로 수정 필요)
                    withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
                        // yaml 파일이 crow_server 폴더 안에 있다면 경로를 명시
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/crow_server/crow-server.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/crow-server"
                    }
                }
            }
        }

        // 📡 2. Mosquitto (폴더명: mosquitto)
        stage('MQTT 배포') {
            when { changeset 'RaspberryPi/k3s-cluster/mosquitto/**' }
            steps {
                script {
                    dir('RaspberryPi/k3s-cluster/mosquitto') {
                        echo "📡 MQTT 빌드 시작..."
                        withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                            sh "echo $PASS | docker login -u $USER --password-stdin"
                            sh "docker buildx build --platform linux/arm64 -t hjuohj/mqtt-broker:latest --push ."
                        }
                    }
                    withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
                        // yaml 파일 경로 주의: mosquitto/mqtt.yaml
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/mosquitto/mqtt.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/mqtt-broker"
                    }
                }
            }
        }

        // 🎥 3. MediaMTX (폴더명: mediamtx)
        stage('MediaMTX 배포') {
            when { changeset 'RaspberryPi/k3s-cluster/mediamtx/**' }
            steps {
                script {
                    // 1. YAML 파일 복사 (원본 보존용)
                    withCredentials([
                    string(credentialsId: 'cctv-camera-ip', variable: 'REAL_IP'),
                    string(credentialsId: 'cctv-camera-user', variable: 'REAL_USER'),
                    string(credentialsId: 'cctv-camera-pw', variable: 'REAL_PW')
                ]) {
                    script {
                        // 2. sed 명령어로 YAML 파일 내용 바꿔치기 (덮어쓰기)
                        // 주의: 구분자로 / 대신 | 를 사용 (URL이나 특수문자 충돌 방지)
                        sh "sed -i 's|__CAMERA_IP__|${REAL_IP}|g' RaspberryPi/k3s-cluster/mediamtx/mediamtx.yaml"
                        sh "sed -i 's|__CAMERA_USER__|${REAL_USER}|g' RaspberryPi/k3s-cluster/mediamtx/mediamtx.yaml"
                        sh "sed -i 's|__CAMERA_PASSWORD__|${REAL_PW}|g' RaspberryPi/k3s-cluster/mediamtx/mediamtx.yaml"
                    }
                }
                    dir('RaspberryPi/k3s-cluster/mediamtx') {
                        echo "🎥 MediaMTX 빌드 시작..."
                        withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                            sh "echo $PASS | docker login -u $USER --password-stdin"
                            sh "docker buildx build --platform linux/arm64 -t hjuohj/mediamtx-server:latest --push ."
                        }
                    }
                    withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
                        // yaml 파일 경로 주의: mediamtx/mediamtx.yaml
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/mediamtx/mediamtx.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/mediamtx-server"
                    }
                }
            }
        }

        // 🐬 4. MariaDB (폴더명: mariadb)
        stage('MariaDB 배포') {
            when { changeset 'RaspberryPi/k3s-cluster/mariadb/**' }
            steps {
                script {
                    dir('RaspberryPi/k3s-cluster/mariadb') {
                        echo "🐬 MariaDB 빌드 시작..."
                        withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                            sh "echo $PASS | docker login -u $USER --password-stdin"
                            sh "docker buildx build --platform linux/arm64 -t hjuohj/mariadb-server:latest --push ."
                        }
                    }
                    withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-deploy.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/mariadb"
                    }
                }
            }
        }

        // 🖥️ 4.9. Qt Base Image (환경 설정 변경 시에만 실행)
        stage('Qt Base Build') {
            when { changeset 'Qt_Client/Dockerfile.base' }
            steps {
                script {
                    dir('Qt_Client') {
                        echo "🛠️ Qt 빌드 환경(Base Image) 업데이트 중..."
                        withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                            sh "echo $PASS | docker login -u $USER --password-stdin"
                            // Dockerfile.base를 사용하여 빌드
                            sh "docker build -f Dockerfile.base -t hjuohj/qt-client-base:latest --push ."
                        }
                    }
                }
            }
        }

        // 🖥️ 5. Qt Client (폴더명: Qt_Client)
        stage('Qt Client 빌드') {
            // 소스 코드 변경 OR Base Image 변경 시 실행
            when { anyOf { changeset 'Qt_Client/**'; changeset 'Qt_Client/Dockerfile.base' } }
            steps {
                script {
                    dir('Qt_Client') {
                        echo "🖥️ Qt Client 빌드 시작..."
                        
                        withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                            // Base Image가 없을 수 있으므로 로그인 (private repo일 경우 대비)
                            sh "echo $PASS | docker login -u $USER --password-stdin"
                            
                            // 1. Docker Build (Base Image 활용)
                            // Dockerfile의 FROM hjuohj/qt-client-base:latest 가 사용됨
                            // --pull 옵션으로 최신 Base Image 확인
                            sh "docker build --pull -t qt-client-builder ."
                        }

                        // 2. 컨테이너 생성 및 빌드 실행 (볼륨 마운트 제거)
                        sh "docker rm -f qt-build-temp || true"
                        
                        try {
                            // 컨테이너 실행 (빌드 및 압축)
                            sh "docker run --name qt-build-temp qt-client-builder /bin/bash -c 'cmake . && make -j\$(nproc) && zip QtClient_Build.zip Team3VideoReceiver .env'"
                            
                            // 3. 결과물(zip)을 컨테이너에서 호스트로 복사
                            sh "docker cp qt-build-temp:/app/QtClient_Build.zip ."
                            
                        } catch (Exception e) {
                            error "Qt Client Build Failed"
                        } finally {
                            // 4. 컨테이너 정리
                            sh "docker rm -f qt-build-temp || true"
                        }
                    }
                    // 5. 빌드 결과물 저장 (Jenkins Artifact)
                    archiveArtifacts artifacts: 'Qt_Client/QtClient_Build.zip', allowEmptyArchive: true
                }
            }
        }
    }
    post {
        success {
            slackSend (
                channel: 'C0ADS8RQAL9', 
                color: 'good',
                botUser: true, 
                message: "✅ 배포 성공: ${env.JOB_NAME} #${env.BUILD_NUMBER} (<${env.BUILD_URL}|상세보기>)"
            )
        }
        failure {
            slackSend (
                channel: 'C0ADS8RQAL9', 
                color: 'danger', 
                botUser: true,
                message: "❌ 배포 실패: ${env.JOB_NAME} #${env.BUILD_NUMBER} (<${env.BUILD_URL}|상세보기>)"
            )
        }
    }
}
