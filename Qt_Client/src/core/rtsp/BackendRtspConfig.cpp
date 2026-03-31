#include "Backend.h"
#include "internal/rtsp/BackendRtspConfigService.h"

// RTSP Ip 설정 함수
void Backend::setRtspIp(const QString &ip)
{
    BackendRtspConfigService::setRtspIp(this, d_ptr.get(), ip);
}

// RTSP 포트 설정 함수
void Backend::setRtspPort(const QString &port)
{
    BackendRtspConfigService::setRtspPort(this, d_ptr.get(), port);
}

// RTSP URL 생성 함수
QString Backend::buildRtspUrl(int cameraIndex, bool useSubStream) const
{
    return BackendRtspConfigService::buildRtspUrl(this, d_ptr.get(), cameraIndex, useSubStream);
}

// RTSP Ip 갱신 함수
bool Backend::updateRtspIp(QString ip)
{
    return BackendRtspConfigService::updateRtspIp(this, d_ptr.get(), ip);
}

// RTSP 설정 갱신 함수
bool Backend::updateRtspConfig(QString ip, QString port)
{
    return BackendRtspConfigService::updateRtspConfig(this, d_ptr.get(), ip, port);
}

// RTSP 설정 To 환경 초기화 함수
bool Backend::resetRtspConfigToEnv()
{
    return BackendRtspConfigService::resetRtspConfigToEnv(this, d_ptr.get());
}

// RTSP Credentials 갱신 함수
bool Backend::updateRtspCredentials(QString username, QString password)
{
    return BackendRtspConfigService::updateRtspCredentials(this, d_ptr.get(), username, password);
}

// use 환경 RTSP Credentials 처리 함수
void Backend::useEnvRtspCredentials()
{
    BackendRtspConfigService::useEnvRtspCredentials(this, d_ptr.get());
}

