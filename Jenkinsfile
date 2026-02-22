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
            when { 
                anyOf {
                    changeset 'RaspberryPi/k3s-cluster/crow_server/**'
                    triggeredBy 'UserIdCause'
                }
            }
            steps {
                script {
                    dir('RaspberryPi/k3s-cluster/crow_server') { 
                        echo "🦅 Crow Server 빌드 시작..."
                        withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                            sh "echo $PASS | docker login -u $USER --password-stdin"
                            sh "docker buildx build --platform linux/arm64 -t hjuohj/crow-server:latest --push ."
                        }
                    }
                    withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/crow_server/crow-server.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/crow-server"
                    }
                }
            }
        }

        // 📡 2. Mosquitto (폴더명: mosquitto)
        stage('MQTT 배포') {
            when { 
                anyOf {
                    changeset 'RaspberryPi/k3s-cluster/mosquitto/**'
                    triggeredBy 'UserIdCause'
                }
            }
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
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/mosquitto/mqtt.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/mqtt-broker"
                    }
                }
            }
        }

        // 🎥 3. MediaMTX (폴더명: mediamtx)
        stage('MediaMTX 배포') {
            when { 
                anyOf {
                    changeset 'RaspberryPi/k3s-cluster/mediamtx/**'
                    triggeredBy 'UserIdCause'
                }
            }
            steps {
                script {
                    withCredentials([
                        string(credentialsId: 'cctv-camera-ip', variable: 'REAL_IP'),
                        string(credentialsId: 'cctv-camera-user', variable: 'REAL_USER'),
                        string(credentialsId: 'cctv-camera-pw', variable: 'REAL_PW')
                    ]) {
                        script {
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
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/mediamtx/mediamtx.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/mediamtx-server"
                    }
                }
            }
        }

        // 🐬 4. MariaDB (폴더명: mariadb)
        stage('MariaDB 배포') {
            when { 
                anyOf {
                    changeset 'RaspberryPi/k3s-cluster/mariadb/**'
                    triggeredBy 'UserIdCause'
                }
            }
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

        // 🛡️ 5. Nginx Gateway (폴더명: nginx)
        stage('Nginx Gateway 배포') {
            when { 
                anyOf {
                    changeset 'RaspberryPi/k3s-cluster/nginx/**'
                    changeset 'RaspberryPi/k3s-cluster/security/**'
                    triggeredBy 'UserIdCause'
                }
            }
            steps {
                script {
                    // 1. 인증서 생성 실행
                    dir('RaspberryPi/k3s-cluster/security') {
                        echo "🔐 인증서 생성 중..."
                        sh "chmod +x generate_certs.sh"
                        sh "./generate_certs.sh"
                        
                        // 생성된 인증서를 nginx 빌드 폴더로 복사
                        sh "mkdir -p ../nginx/certs"
                        sh "cp certs/server.crt certs/server.key certs/rootCA.crt ../nginx/certs/"
                    }

                    // 2. Nginx 이미지 빌드 및 푸시
                    dir('RaspberryPi/k3s-cluster/nginx') {
                        echo "🛡️ Nginx Gateway 빌드 시작..."
                        withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                            sh "echo $PASS | docker login -u $USER --password-stdin"
                            sh "docker buildx build --platform linux/arm64 -t hjuohj/nginx-gateway:latest --push ."
                        }
                    }

                    // 3. K8s 배포 (Secret 마운트 없이 이미지 내부 파일 사용하도록 YAML 수정 필요 시 대응)
                    withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/nginx/nginx-deployment.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/nginx-gateway"
                    }

                    // 4. 클라이언트용 인증서 산출물 보관
                    archiveArtifacts artifacts: 'RaspberryPi/k3s-cluster/security/certs/cctv.*, RaspberryPi/k3s-cluster/security/certs/stm32.*, RaspberryPi/k3s-cluster/security/certs/rootCA.crt', fingerprint: true
                }
            }
        }

        // 🖥️ 6. Qt Client (Windows)
        stage('Qt Client (Windows CMake)') {
            agent { label 'windows-qt' } 
            
            // Qt 폴더 내 변경사항이 있을 때
            when { 
                anyOf {
                    changeset 'Qt_Client/**'
                    triggeredBy 'UserIdCause'
                }
            }

            environment {
                QT_ROOT = "C:\\Qt\\6.10.2\\mingw_64"
                MINGW_BIN = "C:\\Qt\\Tools\\mingw1310_64\\bin" 
                CMAKE_BIN = "C:\\Qt\\Tools\\CMake_64\\bin"
                PATH = "${QT_ROOT}\\bin;${MINGW_BIN};${CMAKE_BIN};C:\\Windows\\System32;${PATH}"
                
                BUILD_DIR = "build_cmake"
                OUTPUT_DIR = "deploy_output_new"
            }

            steps {
                echo "🔨 CMake 기반 MinGW 빌드 시작..."
                git branch: 'develop', url: GIT_URL

                dir('Qt_Client') {
                    script {
                        bat "taskkill /F /IM Team3VideoReceiver.exe /T || exit 0"
                        bat "taskkill /F /IM ld.exe /T || exit 0"
                        bat "taskkill /F /IM g++.exe /T || exit 0"
                        bat "taskkill /F /IM cmake.exe /T || exit 0"
                        
                        sleep 3
                        
                        // 1. 배포 폴더(OUTPUT_DIR) 초기화 및 생성
                        bat "if exist ${OUTPUT_DIR} rmdir /s /q ${OUTPUT_DIR}"
                        bat "mkdir ${OUTPUT_DIR}"
                        bat "if not exist ${BUILD_DIR} mkdir ${BUILD_DIR}"

                        // 2. 필수 의존성 파일(VLC, .env)을 '미리' 배포 폴더로 복사
                        echo "🚚 라이브러리 및 설정 파일 복사..."
                        
                        bat "if exist libvlc.dll copy /Y libvlc.dll ${OUTPUT_DIR}\\"
                        bat "if exist libvlccore.dll copy /Y libvlccore.dll ${OUTPUT_DIR}\\"
                        bat "if exist plugins xcopy /E /I /Y plugins ${OUTPUT_DIR}\\plugins"

                        // Jenkins Secret File(.env) 처리
                        withCredentials([file(credentialsId: 'qt-client-env', variable: 'SECRET_ENV')]) {
                             // 실행 시 필요하므로 배포 폴더에 바로 복사
                            bat "copy /Y \"%SECRET_ENV%\" ${OUTPUT_DIR}\\.env"
                        }

                        // 3. CMake 설정 및 빌드 (결과물이 deploy_output_new로 직행)
                        dir(BUILD_DIR) {
                            bat """
                                cmake -G "MinGW Makefiles" ^
                                -DCMAKE_BUILD_TYPE=Release ^
                                -DCMAKE_PREFIX_PATH="${QT_ROOT}" ^
                                -DCMAKE_MAKE_PROGRAM="${MINGW_BIN}\\mingw32-make.exe" ^
                                -DCMAKE_C_COMPILER="${MINGW_BIN}\\gcc.exe" ^
                                -DCMAKE_CXX_COMPILER="${MINGW_BIN}\\g++.exe" ^
                                -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="../${OUTPUT_DIR}" ^
                                ..
                            """
                            
                            // 병렬 빌드
                            bat "cmake --build . --parallel 4"
                        }
                        
                        // 4. 패키징 (windeployqt)
                        // 이미 EXE와 DLL이 OUTPUT_DIR에 다 모여 있으므로 바로 실행
                        dir(OUTPUT_DIR) {
                            echo "📦 Qt 의존성 주입 (windeployqt)..."
                            bat "windeployqt --release --no-translations --verbose 2 --qmldir .. Team3VideoReceiver.exe"
                        }

                        // 5. 압축 (deploy_output_new 전체를 압축)
                        echo "🗜️ 압축 중..."
                        powershell "Compress-Archive -Path ${OUTPUT_DIR}\\* -DestinationPath QtClient_Windows_VMS.zip -Force"
                    }
                }
                
                // 6. Jenkins에 산출물 보관
                archiveArtifacts artifacts: 'Qt_Client/QtClient_Windows_VMS.zip', fingerprint: true
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
