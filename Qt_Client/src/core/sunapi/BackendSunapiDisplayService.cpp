#include "internal/sunapi/BackendSunapiDisplayService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace {
int clampInt(int value, int minValue, int maxValue)
{
    return qMax(minValue, qMin(maxValue, value));
}

QString extractValue(const QString &body, const QStringList &keys)
{
    for (const QString &k : keys) {
        const QString token = QString("(?:[A-Za-z0-9_\\-]+\\.)*%1").arg(QRegularExpression::escape(k));
        const QRegularExpression jsonPattern(
                QString("\"%1\"\\s*:\\s*(\"([^\"]*)\"|[-]?[0-9]+|true|false)").arg(token),
                QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch jm = jsonPattern.match(body);
        if (jm.hasMatch()) {
            QString raw = jm.captured(1).trimmed();
            if (raw.startsWith('"') && raw.endsWith('"') && raw.size() >= 2) {
                raw = raw.mid(1, raw.size() - 2);
            }
            return raw;
        }

        const QRegularExpression kvPattern(
                QString("(^|\\n|\\r)\\s*%1\\s*=\\s*([^\\r\\n]+)").arg(token),
                QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch km = kvPattern.match(body);
        if (km.hasMatch()) {
            return km.captured(2).trimmed();
        }

        const QRegularExpression kvColonPattern(
                QString("(^|\\n|\\r)\\s*%1\\s*:\\s*([^\\r\\n]+)").arg(token),
                QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch kcm = kvColonPattern.match(body);
        if (kcm.hasMatch()) {
            return kcm.captured(2).trimmed();
        }

        const QRegularExpression xmlPattern(
                QString("<\\s*%1\\s*>\\s*([^<]+)\\s*<\\s*/\\s*%1\\s*>").arg(token),
                QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch xm = xmlPattern.match(body);
        if (xm.hasMatch()) {
            return xm.captured(1).trimmed();
        }
    }
    return {};
}

bool parseBoolString(const QString &text, bool fallback)
{
    const QString v = text.trimmed().toLower();
    if (v == "1" || v == "true" || v == "yes" || v == "on")
        return true;
    if (v == "0" || v == "false" || v == "no" || v == "off")
        return false;
    return fallback;
}
} // namespace

void BackendSunapiDisplayService::sunapiLoadDisplaySettings(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    if (cameraIndex < 0) {
        emit backend->cameraControlMessage("Display settings load failed: invalid camera index", true);
        return;
    }
    if (state->m_authToken.trimmed().isEmpty()) {
        emit backend->cameraControlMessage("Display settings load failed: login required", true);
        return;
    }

    QNetworkRequest request = backend->makeApiJsonRequest("/api/sunapi/display/settings", {
        { "channel", QString::number(cameraIndex) }
    });
    backend->applyAuthIfNeeded(request);
    qInfo() << "[SUNAPI] request: Display settings view url=" << request.url().toString();

    QNetworkReply *reply = state->m_manager->get(request);
    backend->attachIgnoreSslErrors(reply, "SUNAPI_DISPLAY_VIEW");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        qInfo() << "[SUNAPI][DISPLAY] view response status=" << statusCode
                << "url=" << reply->request().url()
                << "body(sample)=" << body.left(260);

        if (reply->error() != QNetworkReply::NoError) {
            const QString err = QString("Display settings load failed (HTTP %1): %2")
                                        .arg(statusCode)
                                        .arg(reply->errorString());
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(160);
            emit backend->cameraControlMessage(err, true);
            reply->deleteLater();
            return;
        }

        QString sunapiErrMsg;
        if (backend->isSunapiBodyError(body, &sunapiErrMsg)) {
            const QString err = QString("Display settings load failed: device response error (%1)").arg(sunapiErrMsg);
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(200);
            emit backend->cameraControlMessage(err, true);
            reply->deleteLater();
            return;
        }

        const QString contrastText = extractValue(body, { "Contrast", "contrast" });
        const QString brightnessText = extractValue(body, { "Brightness", "brightness" });
        const QString sharpnessLevelText = extractValue(body, { "SharpnessLevel", "sharpnessLevel", "sharpness_level" });
        const QString sharpnessEnableText = extractValue(body, { "SharpnessEnable", "sharpnessEnable", "sharpness_enable" });
        const QString colorLevelText = extractValue(body, { "Saturation", "ColorLevel", "colorLevel", "color_level" });

        bool changed = false;
        if (!contrastText.isEmpty()) {
            const int next = clampInt(contrastText.toInt(), 1, 100);
            if (state->m_displayContrast != next) {
                state->m_displayContrast = next;
                changed = true;
            }
        }
        if (!brightnessText.isEmpty()) {
            const int next = clampInt(brightnessText.toInt(), 1, 100);
            if (state->m_displayBrightness != next) {
                state->m_displayBrightness = next;
                changed = true;
            }
        }
        if (!sharpnessLevelText.isEmpty()) {
            const int next = clampInt(sharpnessLevelText.toInt(), 1, 32);
            if (state->m_displaySharpnessLevel != next) {
                state->m_displaySharpnessLevel = next;
                changed = true;
            }
        }
        if (!sharpnessEnableText.isEmpty()) {
            const bool next = parseBoolString(sharpnessEnableText, state->m_displaySharpnessEnabled);
            if (state->m_displaySharpnessEnabled != next) {
                state->m_displaySharpnessEnabled = next;
                changed = true;
            }
        }
        if (!colorLevelText.isEmpty()) {
            const int next = clampInt(colorLevelText.toInt(), 1, 100);
            if (state->m_displayColorLevel != next) {
                state->m_displayColorLevel = next;
                changed = true;
            }
        }

        if (changed) {
            emit backend->displaySettingsChanged();
        }
        reply->deleteLater();
    });
}

bool BackendSunapiDisplayService::sunapiSetDisplaySettings(Backend *backend,
                                                           BackendPrivate *state,
                                                           int cameraIndex,
                                                           int contrast,
                                                           int brightness,
                                                           int sharpnessLevel,
                                                           int colorLevel,
                                                           bool sharpnessEnabled)
{
    if (cameraIndex < 0) {
        emit backend->cameraControlMessage("Display settings update failed: invalid camera index", true);
        return false;
    }
    if (state->m_authToken.trimmed().isEmpty()) {
        emit backend->cameraControlMessage("Display settings update failed: login required", true);
        return false;
    }

    const int c = clampInt(contrast, 1, 100);
    const int b = clampInt(brightness, 1, 100);
    const int s = clampInt(sharpnessLevel, 1, 32);
    const int sat = clampInt(colorLevel, 1, 100);
    const QString sharpEnabled = sharpnessEnabled ? "true" : "false";

    QNetworkRequest request = backend->makeApiJsonRequest("/api/sunapi/display/settings", {
        { "channel", QString::number(cameraIndex) },
        { "contrast", QString::number(c) },
        { "brightness", QString::number(b) },
        { "sharpness_level", QString::number(s) },
        { "color_level", QString::number(sat) },
        { "sharpness_enable", sharpEnabled }
    });
    backend->applyAuthIfNeeded(request);
    qInfo() << "[SUNAPI] request: Display settings set url=" << request.url().toString();

    QNetworkReply *reply = state->m_manager->post(request, QByteArray());
    backend->attachIgnoreSslErrors(reply, "SUNAPI_DISPLAY_SET");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply, c, b, s, sat, sharpnessEnabled]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();

        if (reply->error() == QNetworkReply::NoError) {
            QString sunapiErrMsg;
            if (backend->isSunapiBodyError(body, &sunapiErrMsg)) {
                const QString err = QString("Display settings update failed: device response error (%1)").arg(sunapiErrMsg);
                qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(200);
                emit backend->cameraControlMessage(err, true);
            } else {
                bool changed = false;
                if (state->m_displayContrast != c) {
                    state->m_displayContrast = c;
                    changed = true;
                }
                if (state->m_displayBrightness != b) {
                    state->m_displayBrightness = b;
                    changed = true;
                }
                if (state->m_displaySharpnessLevel != s) {
                    state->m_displaySharpnessLevel = s;
                    changed = true;
                }
                if (state->m_displayColorLevel != sat) {
                    state->m_displayColorLevel = sat;
                    changed = true;
                }
                if (state->m_displaySharpnessEnabled != sharpnessEnabled) {
                    state->m_displaySharpnessEnabled = sharpnessEnabled;
                    changed = true;
                }
                if (changed)
                    emit backend->displaySettingsChanged();
                emit backend->cameraControlMessage("Display settings updated", false);
            }
        } else {
            const QString err = QString("Display settings update failed (HTTP %1): %2")
                                        .arg(statusCode)
                                        .arg(reply->errorString());
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(160);
            emit backend->cameraControlMessage(err, true);
        }
        reply->deleteLater();
    });

    return true;
}

bool BackendSunapiDisplayService::sunapiResetDisplaySettings(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    if (cameraIndex < 0) {
        emit backend->cameraControlMessage("Display reset failed: invalid camera index", true);
        return false;
    }
    if (state->m_authToken.trimmed().isEmpty()) {
        emit backend->cameraControlMessage("Display reset failed: login required", true);
        return false;
    }

    QNetworkRequest request = backend->makeApiJsonRequest("/api/sunapi/display/reset", {
        { "channel", QString::number(cameraIndex) }
    });
    backend->applyAuthIfNeeded(request);
    qInfo() << "[SUNAPI] request: Display reset url=" << request.url().toString();

    QNetworkReply *reply = state->m_manager->post(request, QByteArray());
    backend->attachIgnoreSslErrors(reply, "SUNAPI_DISPLAY_RESET");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();

        if (reply->error() == QNetworkReply::NoError) {
            QString sunapiErrMsg;
            if (backend->isSunapiBodyError(body, &sunapiErrMsg)) {
                const QString err = QString("Display reset failed: device response error (%1)").arg(sunapiErrMsg);
                qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(200);
                emit backend->cameraControlMessage(err, true);
            } else {
                bool changed = false;
                if (state->m_displayContrast != 50) {
                    state->m_displayContrast = 50;
                    changed = true;
                }
                if (state->m_displayBrightness != 50) {
                    state->m_displayBrightness = 50;
                    changed = true;
                }
                if (state->m_displaySharpnessLevel != 12) {
                    state->m_displaySharpnessLevel = 12;
                    changed = true;
                }
                if (state->m_displayColorLevel != 50) {
                    state->m_displayColorLevel = 50;
                    changed = true;
                }
                if (!state->m_displaySharpnessEnabled) {
                    state->m_displaySharpnessEnabled = true;
                    changed = true;
                }
                if (changed)
                    emit backend->displaySettingsChanged();
                emit backend->cameraControlMessage("Display settings reset", false);
            }
        } else {
            const QString err = QString("Display reset failed (HTTP %1): %2")
                                        .arg(statusCode)
                                        .arg(reply->errorString());
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(160);
            emit backend->cameraControlMessage(err, true);
        }
        reply->deleteLater();
    });

    return true;
}

