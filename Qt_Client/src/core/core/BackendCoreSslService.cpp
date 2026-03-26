#include "internal/core/BackendCoreSslService.h"

#include "Backend.h"
#include "internal/core/BackendCoreCertConfigService.h"
#include "internal/core/Backend_p.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>

namespace {

QString buildCertConfigErrorMessage(const BackendPrivate *state)
{
    const QString certDir = BackendCoreCertConfigService::certDirectoryPath(state);
    return QStringLiteral(
               "인증서 폴더 설정이 올바르지 않습니다. %1 폴더의 rootCA.crt, client-qt.crt, client-qt.key 파일을 확인해 주세요.")
        .arg(certDir);
}

} // namespace

void BackendCoreSslService::setupSslConfiguration(Backend *backend, BackendPrivate *state)
{
    Q_UNUSED(backend);

    const QString caPathRaw = state->m_env.value("SSL_CA_CERT",
                            state->m_env.value("MQTT_CA_CERT", "certs/rootCA.crt")).trimmed();
    const QString certPathRaw = state->m_env.value("SSL_CLIENT_CERT",
                              state->m_env.value("MQTT_CLIENT_CERT", "certs/client-qt.crt")).trimmed();
    const QString keyPathRaw = state->m_env.value("SSL_CLIENT_KEY",
                             state->m_env.value("MQTT_CLIENT_KEY", "certs/client-qt.key")).trimmed();

    const QString verifyPeerRaw = state->m_env.value("SSL_VERIFY_PEER", "1").trimmed().toLower();
    const QString ignoreErrorsRaw = state->m_env.value("SSL_IGNORE_ERRORS", "0").trimmed().toLower();
    const bool verifyPeer = !(verifyPeerRaw == "0" || verifyPeerRaw == "false" || verifyPeerRaw == "off");
    state->m_sslIgnoreErrors = (ignoreErrorsRaw == "1" || ignoreErrorsRaw == "true" || ignoreErrorsRaw == "on");

    state->m_sslConfig = QSslConfiguration::defaultConfiguration();
    state->m_sslConfigReady = false;

    bool hasCaCertificate = false;
    bool hasClientCertificate = false;
    bool hasClientKey = false;

    const QString caPath = BackendCoreCertConfigService::resolveCertificatePath(state, caPathRaw);
    QFile caFile(caPath);
    if (caFile.open(QIODevice::ReadOnly)) {
        const QList<QSslCertificate> certs = QSslCertificate::fromData(caFile.readAll(), QSsl::Pem);
        if (!certs.isEmpty()) {
            state->m_sslConfig.setCaCertificates(certs);
            hasCaCertificate = true;
            qInfo() << "[SSL] CA loaded:" << caPath;
        } else {
            qWarning() << "[SSL] invalid CA cert:" << caPath;
        }
    } else {
        qWarning() << "[SSL] CA cert not found:" << caPath;
    }

    const QString certPath = BackendCoreCertConfigService::resolveCertificatePath(state, certPathRaw);
    QFile certFile(certPath);
    if (certFile.open(QIODevice::ReadOnly)) {
        const QList<QSslCertificate> certs = QSslCertificate::fromData(certFile.readAll(), QSsl::Pem);
        if (!certs.isEmpty()) {
            state->m_sslConfig.setLocalCertificate(certs.first());
            hasClientCertificate = true;
            qInfo() << "[SSL] client cert loaded:" << certPath;
        } else {
            qWarning() << "[SSL] invalid client cert:" << certPath;
        }
    } else {
        qWarning() << "[SSL] client cert not found:" << certPath;
    }

    const QString keyPath = BackendCoreCertConfigService::resolveCertificatePath(state, keyPathRaw);
    QFile keyFile(keyPath);
    if (keyFile.open(QIODevice::ReadOnly)) {
        QSslKey key(&keyFile, QSsl::Rsa, QSsl::Pem);
        if (!key.isNull()) {
            state->m_sslConfig.setPrivateKey(key);
            hasClientKey = true;
            qInfo() << "[SSL] client key loaded:" << keyPath;
        } else {
            qWarning() << "[SSL] invalid client key:" << keyPath;
        }
    } else {
        qWarning() << "[SSL] client key not found:" << keyPath;
    }

    state->m_sslConfig.setPeerVerifyMode(verifyPeer ? QSslSocket::VerifyPeer : QSslSocket::VerifyNone);
    // HTTPS/WSS 요청은 인증서 3종이 모두 준비된 경우에만 허용한다.
    state->m_sslConfigReady = hasCaCertificate && hasClientCertificate && hasClientKey;
    qInfo() << "[SSL] ready=" << state->m_sslConfigReady
            << "verifyPeer=" << verifyPeer
            << "ignoreErrors=" << state->m_sslIgnoreErrors;
}

bool BackendCoreSslService::isHttpsRequestReady(BackendPrivate *state,
                                                const QUrl &url,
                                                QString *errorMessage)
{
    if (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
        return true;
    }

    if (state && state->m_sslConfigReady) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = buildCertConfigErrorMessage(state);
    }
    return false;
}

void BackendCoreSslService::applySslIfNeeded(Backend *backend,
                                             BackendPrivate *state,
                                             QNetworkRequest &request)
{
    backend->applyAuthIfNeeded(request);

    const QUrl url = request.url();
    if (url.scheme().compare("https", Qt::CaseInsensitive) != 0) {
        return;
    }

    if (state->m_sslConfigReady) {
        request.setSslConfiguration(state->m_sslConfig);
    }
}

void BackendCoreSslService::attachIgnoreSslErrors(Backend *backend,
                                                  BackendPrivate *state,
                                                  QNetworkReply *reply,
                                                  const QString &tag)
{
    Q_UNUSED(backend);

    if (!reply) {
        return;
    }

    QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply, tag, state](const QList<QSslError> &errors) {
        for (const auto &err : errors) {
            qWarning() << "[" << tag << "][SSL]" << err.errorString();
        }
        if (state->m_sslIgnoreErrors) {
            reply->ignoreSslErrors();
        }
    });
}

