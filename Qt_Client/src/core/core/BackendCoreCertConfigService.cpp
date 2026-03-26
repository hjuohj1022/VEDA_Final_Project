#include "internal/core/BackendCoreCertConfigService.h"

#include "Backend.h"
#include "internal/core/BackendCoreMqttService.h"
#include "internal/core/Backend_p.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace {

QString defaultCertDirectoryPath()
{
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("certs")));
}

QString normalizeDirectoryPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(QDir(trimmed).absolutePath());
}

void reloadSecurityConfiguration(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state) {
        return;
    }

    // 변경된 인증서 폴더를 즉시 다시 읽는다.
    backend->setupSslConfiguration();
    BackendCoreMqttService::reloadMqtt(backend, state);
}

} // namespace

void BackendCoreCertConfigService::loadCertDirectoryOverride(Backend *backend, BackendPrivate *state)
{
    Q_UNUSED(backend);

    if (!state) {
        return;
    }

    QSettings settings;
    // 사용자가 마지막으로 고른 인증서 폴더를 앱 시작 시 복원한다.
    state->m_certDirectoryOverride =
        normalizeDirectoryPath(settings.value(QStringLiteral("security/cert_directory_path")).toString());
}

QString BackendCoreCertConfigService::certDirectoryPath(const BackendPrivate *state)
{
    if (!state) {
        return defaultCertDirectoryPath();
    }

    if (!state->m_certDirectoryOverride.trimmed().isEmpty()) {
        return normalizeDirectoryPath(state->m_certDirectoryOverride);
    }
    return defaultCertDirectoryPath();
}

QString BackendCoreCertConfigService::resolveCertificatePath(const BackendPrivate *state, const QString &rawPath)
{
    const QString overrideDir = state ? normalizeDirectoryPath(state->m_certDirectoryOverride) : QString();
    const QString fileName = QFileInfo(rawPath).fileName();
    if (!overrideDir.isEmpty() && !fileName.isEmpty()) {
        // 사용자 지정 폴더가 있으면 같은 파일명으로 먼저 찾는다.
        return QDir(overrideDir).filePath(fileName);
    }

    QFileInfo info(rawPath);
    if (info.isAbsolute()) {
        return QDir::cleanPath(info.absoluteFilePath());
    }

    const QString appSide = QDir(QCoreApplication::applicationDirPath()).filePath(rawPath);
    if (QFileInfo::exists(appSide)) {
        return QDir::cleanPath(appSide);
    }
    return QDir::cleanPath(rawPath);
}

bool BackendCoreCertConfigService::updateCertDirectoryPath(Backend *backend,
                                                           BackendPrivate *state,
                                                           const QString &path)
{
    if (!backend || !state) {
        return false;
    }

    const QString normalizedPath = normalizeDirectoryPath(path);
    if (normalizedPath.isEmpty() || !QFileInfo(normalizedPath).isDir()) {
        return false;
    }

    if (state->m_certDirectoryOverride == normalizedPath) {
        reloadSecurityConfiguration(backend, state);
        return true;
    }

    // 사용자가 고른 폴더를 다음 실행에도 유지한다.
    state->m_certDirectoryOverride = normalizedPath;
    QSettings settings;
    settings.setValue(QStringLiteral("security/cert_directory_path"), normalizedPath);
    emit backend->certDirectoryPathChanged();
    reloadSecurityConfiguration(backend, state);
    qInfo() << "[SSL] cert directory override set:" << normalizedPath;
    return true;
}

bool BackendCoreCertConfigService::resetCertDirectoryPath(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state) {
        return false;
    }

    const bool hadOverride = !state->m_certDirectoryOverride.trimmed().isEmpty();
    state->m_certDirectoryOverride.clear();

    QSettings settings;
    settings.remove(QStringLiteral("security/cert_directory_path"));

    if (hadOverride) {
        emit backend->certDirectoryPathChanged();
    }

    reloadSecurityConfiguration(backend, state);
    qInfo() << "[SSL] cert directory override reset to default:" << defaultCertDirectoryPath();
    return true;
}
