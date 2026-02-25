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

        // 🔐 0. 인증서 및 공통 설정 준비 (하나의 압축 번들로 효율적 관리)
        stage('인증서 준비') {
            when {
                anyOf {
                    changeset 'RaspberryPi/k3s-cluster/security/**'
                    changeset 'RaspberryPi/k3s-cluster/nginx/**'
                    changeset 'RaspberryPi/k3s-cluster/mosquitto/**'
                    changeset 'Qt_Client/**'
                    triggeredBy 'UserIdCause'
                }
            }
            steps {
                script {
                    // 디렉터리 생성 및 클린업
                    sh "mkdir -p RaspberryPi/k3s-cluster/security/certs"
                    
                    dir('RaspberryPi/k3s-cluster/security/certs') {
                        echo "🔐 Jenkins Credentials에서 인증서 번들을 가져와 압축을 푸는 중..."
                        withCredentials([file(credentialsId: 'all-certs-bundle', variable: 'CERTS_BUNDLE')]) {
                            // tar.gz 압축 해제 (파일들이 바로 certs 폴더 안에 풀리도록 함)
                            sh "tar -xzvf '$CERTS_BUNDLE'"
                        }
                    }
                    
                    // 다른 스테이지(Windows 등)에서 사용할 수 있도록 인증서 보관
                    stash name: 'certs-stash', includes: 'RaspberryPi/k3s-cluster/security/certs/**'
                    
                    // 클라이언트 배포용으로 산출물 보관
                    archiveArtifacts artifacts: 'RaspberryPi/k3s-cluster/security/certs/**', fingerprint: true
                }
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
                    changeset 'RaspberryPi/k3s-cluster/security/**'
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

                    // 1. K8s Secret 업데이트 (mqtt-certs) - 통합 인증서(security/certs) 사용
                    withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
                        echo "🔑 MQTT K8s Secret 업데이트 중 (통합 인증서 사용)..."
                        sh """
                            kubectl --kubeconfig=$KUBECONFIG create secret generic mqtt-certs \
                                --from-file=ca.crt=RaspberryPi/k3s-cluster/security/certs/rootCA.crt \
                                --from-file=server.crt=RaspberryPi/k3s-cluster/security/certs/server.crt \
                                --from-file=server.key=RaspberryPi/k3s-cluster/security/certs/server.key \
                                --dry-run=client -o yaml | kubectl --kubeconfig=$KUBECONFIG apply -f -
                        """
                        
                        // 3. 배포 적용
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/mosquitto/mqtt.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/mqtt-broker"
                    }

                    // 4. 클라이언트용 인증서 보관 (통합된 위치에서 가져옴)
                    archiveArtifacts artifacts: 'RaspberryPi/k3s-cluster/security/certs/client-qt.*, RaspberryPi/k3s-cluster/security/certs/cctv.*, RaspberryPi/k3s-cluster/security/certs/rootCA.crt', fingerprint: true
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
                    // RTSP Read 인증 (Username with password) Credentials ID
                    def MTX_CRED = 'mediamtx-rtsp-read'  // <- 너가 만든 Jenkins credential ID로 변경

                    // 1) 카메라 접속 정보 치환에 필요한 값 가져오기
                    withCredentials([
                        string(credentialsId: 'cctv-camera-ip', variable: 'REAL_IP'),
                        string(credentialsId: 'cctv-camera-user', variable: 'REAL_USER'),
                        string(credentialsId: 'cctv-camera-pw', variable: 'REAL_PW'),
                        usernamePassword(credentialsId: MTX_CRED, usernameVariable: 'MTX_READ_USER', passwordVariable: 'MTX_READ_PASS')
                    ]) {
                        // 2) 원본 mediamtx.yaml을 직접 sed -i로 바꾸지 말고, 임시 파일로 만들어 치환
                        sh '''
                            set -e

                            SRC=RaspberryPi/k3s-cluster/mediamtx/mediamtx.yaml
                            OUT=/tmp/mediamtx.rendered.yaml

                            # 원본 -> 임시 파일 복사
                            cp "$SRC" "$OUT"

                            # 카메라 정보 치환 (템플릿에 __CAMERA_*__ 가 존재해야 함)
                            sed -i "s|__CAMERA_IP__|${REAL_IP}|g" "$OUT"
                            sed -i "s|__CAMERA_USER__|${REAL_USER}|g" "$OUT"
                            sed -i "s|__CAMERA_PASSWORD__|${REAL_PW}|g" "$OUT"

                            # RTSP Read 인증 치환 (템플릿에 ${MTX_READ_USER}, ${MTX_READ_PASS}가 있어야 함)
                            sed -i "s|\\${MTX_READ_USER}|${MTX_READ_USER}|g" "$OUT"
                            sed -i "s|\\${MTX_READ_PASS}|${MTX_READ_PASS}|g" "$OUT"

                            echo "[Rendered mediamtx.yaml head]"
                            sed -n '1,40p' "$OUT"
                        '''

                        // 3) 이미지 빌드/푸시 (기존 유지)
                        dir('RaspberryPi/k3s-cluster/mediamtx') {
                            echo "🎥 MediaMTX 빌드 시작..."
                            withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                                sh "echo $PASS | docker login -u $USER --password-stdin"
                                sh "docker buildx build --platform linux/arm64 -t hjuohj/mediamtx-server:latest --push ."
                            }
                        }

                        // 4) 렌더링된 yaml로 apply + rollout restart
                        withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
                            sh '''
                                set -e
                                kubectl --kubeconfig=$KUBECONFIG apply -f /tmp/mediamtx.rendered.yaml
                                kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/mediamtx-server
                            '''
                        }
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
                    unstash 'certs-stash' // 보관된 인증서 가져오기

                    // 2. K8s Secret 업데이트 (이미지에 넣지 않고 클러스터에 직접 등록)
                    withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
                        echo "🔑 K8s Secret 업데이트 중..."
                        sh """
                            kubectl --kubeconfig=$KUBECONFIG create secret generic nginx-certs \
                                --from-file=server.crt=RaspberryPi/k3s-cluster/security/certs/server.crt \
                                --from-file=server.key=RaspberryPi/k3s-cluster/security/certs/server.key \
                                --dry-run=client -o yaml | kubectl --kubeconfig=$KUBECONFIG apply -f -
                            
                            kubectl --kubeconfig=$KUBECONFIG create secret generic mtls-ca \
                                --from-file=rootCA.crt=RaspberryPi/k3s-cluster/security/certs/rootCA.crt \
                                --dry-run=client -o yaml | kubectl --kubeconfig=$KUBECONFIG apply -f -
                        """
                    }

                    // 3. Nginx 이미지 빌드 및 푸시 (인증서 없이 설정파일만 포함)
                    dir('RaspberryPi/k3s-cluster/nginx') {
                        echo "🛡️ Nginx Gateway 빌드 시작..."
                        withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                            sh "echo $PASS | docker login -u $USER --password-stdin"
                            sh "docker buildx build --platform linux/arm64 -t hjuohj/nginx-gateway:latest --push ."
                        }
                    }

                    // 4. K8s 배포 적용
                    withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/nginx/nginx-deployment.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/nginx-gateway"
                    }
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

                        // 2. 필수 의존성 파일(VLC, .env, 인증서)을 '미리' 배포 폴더로 복사
                        echo "🚚 라이브러리 및 설정 파일 복사..."
                        
                        unstash 'certs-stash' // 보관된 인증서 가져오기
                        bat "copy RaspberryPi\\k3s-cluster\\security\\certs\\rootCA.crt ${OUTPUT_DIR}\\"
                        
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
