pipeline {
    agent any
    
    environment {
        // === 1. 버전 관리 설정 (여기서 메이저/마이너 관리) ===
        MAJOR_VER = '1'
        MINOR_VER = '0'
        
        GIT_URL = 'https://github.com/hjuohj1022/VEDA_Final_Project.git' 
        DOCKER_CRED = 'docker-hub-login'
        KUBE_CONFIG = 'k3s-kubeconfig'
        GIT_CREDENTIAL_ID = 'github-access-token'
    }

    stages {
        // 🏁 0. 초기화 및 버전 계산
        stage('초기화 및 버전 설정') {
            steps {
                script {
                    // Git Short Hash 계산
                    def gitHash = sh(script: "git rev-parse --short HEAD", returnStdout: true).trim()
                    
                    // Docker용 풀 버전: 1.0.50-a1b2c3d
                    env.DOCKER_VER = "${MAJOR_VER}.${MINOR_VER}.${env.BUILD_NUMBER}-${gitHash}"
                    
                    // Git 태그용 버전: v1.0.50
                    env.GIT_TAG_VER = "v${MAJOR_VER}.${MINOR_VER}.${env.BUILD_NUMBER}"
                    
                    echo "ℹ️ 이번 빌드 버전: ${env.DOCKER_VER}"
                }
                
                slackSend (
                    channel: 'C0ADS8RQAL9', 
                    color: '#439FE0',
                    botUser: true, 
                    message: "🚀 배포 시작: [${env.JOB_NAME}] Ver ${env.DOCKER_VER} (<${env.BUILD_URL}|상세보기>)"
                )
                
                git branch: 'develop', url: GIT_URL
            }
        }

        // 🔐 1. 인증서 준비
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
                    sh "mkdir -p RaspberryPi/k3s-cluster/security/certs"
                    dir('RaspberryPi/k3s-cluster/security/certs') {
                        withCredentials([file(credentialsId: 'all-certs-bundle', variable: 'CERTS_BUNDLE')]) {
                            sh "tar -xzvf '$CERTS_BUNDLE'"
                        }
                    }
                    stash name: 'certs-stash', includes: 'RaspberryPi/k3s-cluster/security/certs/**'
                    archiveArtifacts artifacts: 'RaspberryPi/k3s-cluster/security/certs/**', fingerprint: true
                }
            }
        }
        
        // 🦅 2. Crow Server
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
                        echo "🦅 Crow Server 빌드: ${env.DOCKER_VER}"
                        withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                            sh "echo $PASS | docker login -u $USER --password-stdin"
                            // ★ 수정됨: 버전 태그와 latest 태그 동시 푸시
                            sh "docker buildx build --platform linux/arm64 -t hjuohj/crow-server:${env.DOCKER_VER} -t hjuohj/crow-server:latest --push ."
                        }
                    }
                    withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/crow_server/crow-server.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/crow-server"
                    }
                }
            }
        }

        // 📡 3. MQTT
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
                        echo "📡 MQTT 빌드: ${env.DOCKER_VER}"
                        withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                            sh "echo $PASS | docker login -u $USER --password-stdin"
                            sh "docker buildx build --platform linux/arm64 -t hjuohj/mqtt-broker:${env.DOCKER_VER} -t hjuohj/mqtt-broker:latest --push ."
                        }
                    }
                    
                    // Secret 및 배포 적용
                    withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
                        sh """
                            kubectl --kubeconfig=$KUBECONFIG create secret generic mqtt-certs \
                                --from-file=ca.crt=RaspberryPi/k3s-cluster/security/certs/rootCA.crt \
                                --from-file=server.crt=RaspberryPi/k3s-cluster/security/certs/server.crt \
                                --from-file=server.key=RaspberryPi/k3s-cluster/security/certs/server.key \
                                --dry-run=client -o yaml | kubectl --kubeconfig=$KUBECONFIG apply -f -
                        """
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/mosquitto/mqtt.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/mqtt-broker"
                    }
                    archiveArtifacts artifacts: 'RaspberryPi/k3s-cluster/security/certs/client-qt.*, RaspberryPi/k3s-cluster/security/certs/cctv.*, RaspberryPi/k3s-cluster/security/certs/rootCA.crt', fingerprint: true
                }
            }
        }

        // 🎥 4. MediaMTX
        stage('MediaMTX 배포') {
            when {
                anyOf {
                    changeset 'RaspberryPi/k3s-cluster/mediamtx/**'
                    triggeredBy 'UserIdCause'
                }
            }
            steps {
                script {
                    def MTX_CRED = 'mediamtx-rtsp-read'
                    withCredentials([
                        string(credentialsId: 'cctv-camera-ip', variable: 'REAL_IP'),
                        string(credentialsId: 'cctv-camera-user', variable: 'REAL_USER'),
                        string(credentialsId: 'cctv-camera-pw', variable: 'REAL_PW'),
                        usernamePassword(credentialsId: MTX_CRED, usernameVariable: 'MTX_READ_USER', passwordVariable: 'MTX_READ_PASS')
                    ]) {
                        sh '''
                            set -e
                            SRC=RaspberryPi/k3s-cluster/mediamtx/mediamtx.yaml
                            OUT=/tmp/mediamtx.rendered.yaml
                            cp "$SRC" "$OUT"
                            sed -i "s|__CAMERA_IP__|${REAL_IP}|g" "$OUT"
                            sed -i "s|__CAMERA_USER__|${REAL_USER}|g" "$OUT"
                            sed -i "s|__CAMERA_PASSWORD__|${REAL_PW}|g" "$OUT"
                            sed -i "s|\\${MTX_READ_USER}|${MTX_READ_USER}|g" "$OUT"
                            sed -i "s|\\${MTX_READ_PASS}|${MTX_READ_PASS}|g" "$OUT"
                        '''

                        dir('RaspberryPi/k3s-cluster/mediamtx') {
                            echo "🎥 MediaMTX 빌드: ${env.DOCKER_VER}"
                            withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                                sh "echo $PASS | docker login -u $USER --password-stdin"
                                sh "docker buildx build --platform linux/arm64 -t hjuohj/mediamtx-server:${env.DOCKER_VER} -t hjuohj/mediamtx-server:latest --push ."
                            }
                        }

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

        // 🐬 5. MariaDB
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
                        echo "🐬 MariaDB 빌드: ${env.DOCKER_VER}"
                        withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                            sh "echo $PASS | docker login -u $USER --password-stdin"
                            sh "docker buildx build --platform linux/arm64 -t hjuohj/mariadb-server:${env.DOCKER_VER} -t hjuohj/mariadb-server:latest --push ."
                        }
                    }
                    withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-init.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-deploy.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/mariadb"
                    }
                }
            }
        }

        // 🛡️ 6. Nginx Gateway
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
                    unstash 'certs-stash'
                    withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
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

                    dir('RaspberryPi/k3s-cluster/nginx') {
                        echo "🛡️ Nginx Gateway 빌드: ${env.DOCKER_VER}"
                        withCredentials([usernamePassword(credentialsId: DOCKER_CRED, usernameVariable: 'USER', passwordVariable: 'PASS')]) {
                            sh "echo $PASS | docker login -u $USER --password-stdin"
                            sh "docker buildx build --platform linux/arm64 -t hjuohj/nginx-gateway:${env.DOCKER_VER} -t hjuohj/nginx-gateway:latest --push ."
                        }
                    }

                    withCredentials([file(credentialsId: KUBE_CONFIG, variable: 'KUBECONFIG')]) {
                        sh "kubectl --kubeconfig=$KUBECONFIG apply -f RaspberryPi/k3s-cluster/nginx/nginx-deployment.yaml"
                        sh "kubectl --kubeconfig=$KUBECONFIG rollout restart deployment/nginx-gateway"
                    }
                }
            }
        }
        
         // // 🖥️ 6. Qt Client (Windows)
        // stage('Qt Client (Windows CMake)') {
        //     agent { label 'windows-qt' } 
            
        //     // Qt 폴더 내 변경사항이 있을 때
        //     when { 
        //         anyOf {
        //             changeset 'Qt_Client/**'
        //             triggeredBy 'UserIdCause'
        //         }
        //     }

        //     environment {
        //         QT_ROOT = "C:\\Qt\\6.10.2\\mingw_64"
        //         MINGW_BIN = "C:\\Qt\\Tools\\mingw1310_64\\bin" 
        //         CMAKE_BIN = "C:\\Qt\\Tools\\CMake_64\\bin"
        //         PATH = "${QT_ROOT}\\bin;${MINGW_BIN};${CMAKE_BIN};C:\\Windows\\System32;${PATH}"
                
        //         BUILD_DIR = "build_cmake"
        //         OUTPUT_DIR = "deploy_output_new"
        //     }

        //     steps {
        //         echo "🔨 CMake 기반 MinGW 빌드 시작..."
        //         git branch: 'develop', url: GIT_URL

        //         dir('Qt_Client') {
        //             script {
        //                 bat "taskkill /F /IM Team3VideoReceiver.exe /T || exit 0"
        //                 bat "taskkill /F /IM ld.exe /T || exit 0"
        //                 bat "taskkill /F /IM g++.exe /T || exit 0"
        //                 bat "taskkill /F /IM cmake.exe /T || exit 0"
                        
        //                 sleep 3
                        
        //                 // 1. 배포 폴더(OUTPUT_DIR) 초기화 및 생성
        //                 bat "if exist ${OUTPUT_DIR} rmdir /s /q ${OUTPUT_DIR}"
        //                 bat "mkdir ${OUTPUT_DIR}"
        //                 bat "if not exist ${BUILD_DIR} mkdir ${BUILD_DIR}"

        //                 // 2. 필수 의존성 파일(VLC, .env, 인증서)을 '미리' 배포 폴더로 복사
        //                 echo "🚚 라이브러리 및 설정 파일 복사..."
                        
        //                 unstash 'certs-stash' // 보관된 인증서 가져오기
        //                 bat "copy RaspberryPi\\k3s-cluster\\security\\certs\\rootCA.crt ${OUTPUT_DIR}\\"
                        
        //                 bat "if exist libvlc.dll copy /Y libvlc.dll ${OUTPUT_DIR}\\"
        //                 bat "if exist libvlccore.dll copy /Y libvlccore.dll ${OUTPUT_DIR}\\"
        //                 bat "if exist plugins xcopy /E /I /Y plugins ${OUTPUT_DIR}\\plugins"

        //                 // Jenkins Secret File(.env) 처리
        //                 withCredentials([file(credentialsId: 'qt-client-env', variable: 'SECRET_ENV')]) {
        //                      // 실행 시 필요하므로 배포 폴더에 바로 복사
        //                     bat "copy /Y \"%SECRET_ENV%\" ${OUTPUT_DIR}\\.env"
        //                 }

        //                 // 3. CMake 설정 및 빌드 (결과물이 deploy_output_new로 직행)
        //                 dir(BUILD_DIR) {
        //                     bat """
        //                         cmake -G "MinGW Makefiles" ^
        //                         -DCMAKE_BUILD_TYPE=Release ^
        //                         -DCMAKE_PREFIX_PATH="${QT_ROOT}" ^
        //                         -DCMAKE_MAKE_PROGRAM="${MINGW_BIN}\\mingw32-make.exe" ^
        //                         -DCMAKE_C_COMPILER="${MINGW_BIN}\\gcc.exe" ^
        //                         -DCMAKE_CXX_COMPILER="${MINGW_BIN}\\g++.exe" ^
        //                         -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="../${OUTPUT_DIR}" ^
        //                         ..
        //                     """
                            
        //                     // 병렬 빌드
        //                     bat "cmake --build . --parallel 4"
        //                 }
                        
        //                 // 4. 패키징 (windeployqt)
        //                 // 이미 EXE와 DLL이 OUTPUT_DIR에 다 모여 있으므로 바로 실행
        //                 dir(OUTPUT_DIR) {
        //                     echo "📦 Qt 의존성 주입 (windeployqt)..."
        //                     bat "windeployqt --release --no-translations --verbose 2 --qmldir .. Team3VideoReceiver.exe"
        //                 }

        //                 // 5. 압축 (deploy_output_new 전체를 압축)
        //                 echo "🗜️ 압축 중..."
        //                 powershell "Compress-Archive -Path ${OUTPUT_DIR}\\* -DestinationPath QtClient_Windows_VMS.zip -Force"
        //             }
        //         }
                
        //         // 6. Jenkins에 산출물 보관
        //         archiveArtifacts artifacts: 'Qt_Client/QtClient_Windows_VMS.zip', fingerprint: true
        //     }
        // }
    }

    // === 🏁 빌드 후 처리 (성공 시에만 버전 태그 생성) ===
    post {
        success {
            script {
                echo "✅ 빌드 및 배포 성공! Git Tag 생성 중... (${env.GIT_TAG_VER})"
                
                // Git Tag 생성 및 푸시
                withCredentials([usernamePassword(credentialsId: GIT_CREDENTIAL_ID, usernameVariable: 'GIT_USER', passwordVariable: 'GIT_PASS')]) {
                    sh """
                        git config user.email "jenkins@your-server.com"
                        git config user.name "Jenkins Bot"
                        
                        # 이미 태그가 있으면 덮어쓰지 않도록 체크하거나 force 사용
                        # 여기선 중복 방지를 위해 단순 생성 시도
                        git tag -a ${env.GIT_TAG_VER} -m "Release ${env.DOCKER_VER} (Jenkins #${env.BUILD_NUMBER})"
                        git push https://${GIT_USER}:${GIT_PASS}@github.com/hjuohj1022/VEDA_Final_Project.git ${env.GIT_TAG_VER}
                    """
                }
            }
            
            slackSend (
                channel: 'C0ADS8RQAL9', 
                color: 'good',
                botUser: true, 
                message: "✅ 배포 완료: Ver ${env.GIT_TAG_VER} (${env.JOB_NAME} #${env.BUILD_NUMBER})\nDocker Tag: ${env.DOCKER_VER}"
            )
        }
        failure {
            slackSend (
                channel: 'C0ADS8RQAL9', 
                color: 'danger', 
                botUser: true,
                message: "❌ 배포 실패: ${env.JOB_NAME} #${env.BUILD_NUMBER} (<${env.BUILD_URL}|로그 확인>)"
            )
        }
    }
}

