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

        stage('Qt Client 배포(Windows MinGW)') {
            agent { label 'windows-qt' } 
            
            // Qt_Client 폴더 내 변경이 있거나 수동 실행 시 작동
            when { changeset 'Qt_Client/**' }

            environment {
                // 1. 본인의 실제 설치 경로에 맞게 절대 경로 확인 필수!
                QT_ROOT = "C:\\Qt\\6.10.0\\mingw_64"
                // MinGW 컴파일러 도구 경로
                MINGW_BIN = "C:\\Qt\\Tools\\mingw1310_64\\bin" 
                
                // qmake, mingw32-make, windeployqt를 모두 사용하기 위한 PATH
                PATH = "${QT_ROOT}\\bin;${MINGW_BIN};C:\\Windows\\System32;${PATH}"
                
                BUILD_DIR = "build_mingw"
                OUTPUT_DIR = "deploy_output"
            }

            steps {
                // 윈도우 에이전트에서 코드 체크아웃
                git branch: 'develop', url: GIT_URL

                dir('Qt_Client') {
                    echo "🔨 MinGW 빌드 및 패키징 시작..."
                    
                    // 1. 기존 빌드 정리
                    bat """
                        if exist ${BUILD_DIR} rmdir /s /q ${BUILD_DIR}
                        if exist ${OUTPUT_DIR} rmdir /s /q ${OUTPUT_DIR}
                        mkdir ${BUILD_DIR}
                        mkdir ${OUTPUT_DIR}
                    """

                    // 2. QMake & 빌드 (nmake 대신 mingw32-make 사용)
                    dir(BUILD_DIR) {
                        // 만약 .pro 파일이 아니라 CMake를 쓴다면 "cmake .."으로 바꿔야 합니다.
                        // 여기서는 .pro 파일 기준입니다. 파일명을 실제 이름으로 수정하세요.
                        bat "qmake ..\\Team3VideoReceiver.pro -config release" 
                        bat "mingw32-make -j4"
                    }

                    // 3. 배포 (windeployqt)
                    script {
                        // MinGW는 보통 빌드 폴더 바로 아래에 exe가 생성됩니다. (release 폴더 안이 아님)
                        // 파일명을 실제 생성된 이름으로 수정하세요.
                        bat "copy ${BUILD_DIR}\\Team3VideoReceiver.exe ${OUTPUT_DIR}\\"
                        
                        dir(OUTPUT_DIR) {
                            echo "📦 DLL 의존성 수집 중..."
                            bat "windeployqt --release Team3VideoReceiver.exe"
                        }
                    }

                    // 4. 압축 (Powershell)
                    dir(OUTPUT_DIR) {
                        powershell "Compress-Archive -Path * -DestinationPath ..\\QtClient_Windows_MinGW.zip -Force"
                    }
                }
                
                // 5. 결과물 저장
                archiveArtifacts artifacts: 'Qt_Client/QtClient_Windows_MinGW.zip', fingerprint: true
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
