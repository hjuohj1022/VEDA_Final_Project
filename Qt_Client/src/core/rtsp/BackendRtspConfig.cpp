#include "Backend.h"
#include "internal/rtsp/BackendRtspConfigService.h"

void Backend::setRtspIp(const QString &ip)
{
    BackendRtspConfigService::setRtspIp(this, d_ptr.get(), ip);
}

void Backend::setRtspPort(const QString &port)
{
    BackendRtspConfigService::setRtspPort(this, d_ptr.get(), port);
}

QString Backend::buildRtspUrl(int cameraIndex, bool useSubStream) const
{
    return BackendRtspConfigService::buildRtspUrl(this, d_ptr.get(), cameraIndex, useSubStream);
}

bool Backend::updateRtspIp(QString ip)
{
    return BackendRtspConfigService::updateRtspIp(this, d_ptr.get(), ip);
}

bool Backend::updateRtspConfig(QString ip, QString port)
{
    return BackendRtspConfigService::updateRtspConfig(this, d_ptr.get(), ip, port);
}

bool Backend::resetRtspConfigToEnv()
{
    return BackendRtspConfigService::resetRtspConfigToEnv(this, d_ptr.get());
}

bool Backend::updateRtspCredentials(QString username, QString password)
{
    return BackendRtspConfigService::updateRtspCredentials(this, d_ptr.get(), username, password);
}

void Backend::useEnvRtspCredentials()
{
    BackendRtspConfigService::useEnvRtspCredentials(this, d_ptr.get());
}

