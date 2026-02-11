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

        // 🖥️ 5. Qt Client (폴더명: Qt_Client)
        stage('Qt Client 빌드') {
            when { changeset 'Qt_Client/**' }
            steps {
                script {
                    dir('Qt_Client') {
                        echo "🖥️ Qt Client 빌드 시작..."
                        // 1. Docker Build Image 생성 (캐시 활용)
                        sh "docker build -t qt-client-builder ."
                        
                        // 2. Docker Container 내부에서 빌드 실행
                        // -v $(pwd):/app : 현재 소스 코드를 컨테이너에 마운트
                        // cmake . && make : 빌드 수행
                        try {
                            sh "docker run --rm -v \$(pwd):/app qt-client-builder /bin/bash -c 'cmake . && make -j\$(nproc)'"
                        } catch (Exception e) {
                            error "Qt Client Build Failed"
                        }
                    }
                    // 3. 빌드 결과물 저장 (Jenkins Artifact)
                    archiveArtifacts artifacts: 'Qt_Client/Team3VideoReceiver', allowEmptyArchive: true
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
