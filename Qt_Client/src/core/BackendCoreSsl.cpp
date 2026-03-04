#include "Backend.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>

// HTTPS용 SSL 구성 초기화
void Backend::setupSslConfiguration() {
    auto resolvePath = [](const QString &rawPath) {
        QFileInfo info(rawPath);
        if (info.isAbsolute()) return rawPath;

        const QString appSide = QCoreApplication::applicationDirPath() + "/" + rawPath;
        if (QFileInfo::exists(appSide)) {
            return appSide;
        }
        return rawPath;
    };

    const QString caPathRaw = m_env.value("SSL_CA_CERT",
                            m_env.value("MQTT_CA_CERT", "certs/rootCA.crt")).trimmed();
    const QString certPathRaw = m_env.value("SSL_CLIENT_CERT",
                              m_env.value("MQTT_CLIENT_CERT", "certs/client-qt.crt")).trimmed();
    const QString keyPathRaw = m_env.value("SSL_CLIENT_KEY",
                             m_env.value("MQTT_CLIENT_KEY", "certs/client-qt.key")).trimmed();

    const QString verifyPeerRaw = m_env.value("SSL_VERIFY_PEER", "1").trimmed().toLower();
    const QString ignoreErrorsRaw = m_env.value("SSL_IGNORE_ERRORS", "0").trimmed().toLower();
    const bool verifyPeer = !(verifyPeerRaw == "0" || verifyPeerRaw == "false" || verifyPeerRaw == "off");
    m_sslIgnoreErrors = (ignoreErrorsRaw == "1" || ignoreErrorsRaw == "true" || ignoreErrorsRaw == "on");

    m_sslConfig = QSslConfiguration::defaultConfiguration();
    m_sslConfigReady = false;

    bool hasAnyConfig = false;

    const QString caPath = resolvePath(caPathRaw);
    QFile caFile(caPath);
    if (caFile.open(QIODevice::ReadOnly)) {
        const QList<QSslCertificate> certs = QSslCertificate::fromData(caFile.readAll(), QSsl::Pem);
        if (!certs.isEmpty()) {
            m_sslConfig.setCaCertificates(certs);
            hasAnyConfig = true;
            qInfo() << "[SSL] CA loaded:" << caPath;
        } else {
            qWarning() << "[SSL] invalid CA cert:" << caPath;
        }
    } else {
        qWarning() << "[SSL] CA cert not found:" << caPath;
    }

    const QString certPath = resolvePath(certPathRaw);
    QFile certFile(certPath);
    if (certFile.open(QIODevice::ReadOnly)) {
        const QList<QSslCertificate> certs = QSslCertificate::fromData(certFile.readAll(), QSsl::Pem);
        if (!certs.isEmpty()) {
            m_sslConfig.setLocalCertificate(certs.first());
            hasAnyConfig = true;
            qInfo() << "[SSL] client cert loaded:" << certPath;
        } else {
            qWarning() << "[SSL] invalid client cert:" << certPath;
        }
    }

    const QString keyPath = resolvePath(keyPathRaw);
    QFile keyFile(keyPath);
    if (keyFile.open(QIODevice::ReadOnly)) {
        QSslKey key(&keyFile, QSsl::Rsa, QSsl::Pem);
        if (!key.isNull()) {
            m_sslConfig.setPrivateKey(key);
            hasAnyConfig = true;
            qInfo() << "[SSL] client key loaded:" << keyPath;
        } else {
            qWarning() << "[SSL] invalid client key:" << keyPath;
        }
    }

    m_sslConfig.setPeerVerifyMode(verifyPeer ? QSslSocket::VerifyPeer : QSslSocket::VerifyNone);
    m_sslConfigReady = hasAnyConfig;
    qInfo() << "[SSL] ready=" << m_sslConfigReady
            << "verifyPeer=" << verifyPeer
            << "ignoreErrors=" << m_sslIgnoreErrors;
}

// HTTPS 요청에 SSL 설정 적용
void Backend::applySslIfNeeded(QNetworkRequest &request) const {
    const QUrl url = request.url();
    if (url.scheme().compare("https", Qt::CaseInsensitive) != 0) {
        return;
    }
    if (m_sslConfigReady) {
        request.setSslConfiguration(m_sslConfig);
    }
}

// SSL 오류 무시 핸들러 연결
void Backend::attachIgnoreSslErrors(QNetworkReply *reply, const QString &tag) const {
    if (!reply) return;
    connect(reply, &QNetworkReply::sslErrors, reply, [reply, tag, this](const QList<QSslError> &errors) {
        for (const auto &err : errors) {
            qWarning() << "[" << tag << "][SSL]" << err.errorString();
        }
        if (m_sslIgnoreErrors) {
            reply->ignoreSslErrors();
        }
    });
}

