#include "Backend.h"

#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

// 서버 API 요청에 로그인 토큰(Bearer)을 조건부로 주입
void Backend::applyAuthIfNeeded(QNetworkRequest &request) const {
    if (m_authToken.trimmed().isEmpty()) {
        return;
    }

    const QUrl reqUrl = request.url();
    const QUrl apiBase(serverUrl());
    if (!reqUrl.isValid() || !apiBase.isValid()) {
        return;
    }

    const auto defaultPortFor = [](const QString &scheme) {
        return (scheme.compare("https", Qt::CaseInsensitive) == 0) ? 443 : 80;
    };

    const bool sameScheme = (reqUrl.scheme().compare(apiBase.scheme(), Qt::CaseInsensitive) == 0);
    const bool sameHost = (reqUrl.host().compare(apiBase.host(), Qt::CaseInsensitive) == 0);
    const bool samePort = (reqUrl.port(defaultPortFor(reqUrl.scheme())) == apiBase.port(defaultPortFor(apiBase.scheme())));
    if (!sameScheme || !sameHost || !samePort) {
        return;
    }

    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_authToken.toUtf8());
}

// API URL 생성
QUrl Backend::buildApiUrl(const QString &path, const QMap<QString, QString> &query) const {
    QUrl url(serverUrl());
    QString cleanPath = path.trimmed();
    if (!cleanPath.startsWith('/')) {
        cleanPath.prepend('/');
    }
    url.setPath(cleanPath);

    QUrlQuery q(url);
    for (auto it = query.constBegin(); it != query.constEnd(); ++it) {
        q.addQueryItem(it.key(), it.value());
    }
    url.setQuery(q);
    return url;
}

QNetworkRequest Backend::makeApiJsonRequest(const QString &path, const QMap<QString, QString> &query) const {
    QNetworkRequest req(buildApiUrl(path, query));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // API_URL이 HTTPS인 경우 인증서 설정 적용
    applySslIfNeeded(req);
    return req;
}

bool Backend::isSunapiBodyError(const QString &body, QString *reason) const {
    const QString trimmed = body.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    const QString bodyLower = trimmed.toLower();
    // SUNAPI Key=Value 형식에서 Error 필드 우선 검사
    const QRegularExpression errPattern("^\\s*error\\s*=\\s*([^\\r\\n]+)",
                                        QRegularExpression::CaseInsensitiveOption
                                        | QRegularExpression::MultilineOption);
    const QRegularExpressionMatch errMatch = errPattern.match(trimmed);
    if (errMatch.hasMatch()) {
        const QString errValue = errMatch.captured(1).trimmed();
        const QString errValueLower = errValue.toLower();
        if (errValueLower != "0"
            && errValueLower != "ok"
            && errValueLower != "none"
            && errValueLower != "success") {
            if (reason) {
                *reason = QString("Error=%1").arg(errValue);
            }
            return true;
        }
    }

    // 공통 실패 키워드 기반으로 비정상 응답 판별
    if (bodyLower.contains("fail")
        || bodyLower.contains("unsupported")
        || bodyLower.contains("not support")
        || bodyLower.contains("invalid")
        || bodyLower.startsWith("ng")) {
        if (reason) {
            *reason = trimmed.left(160);
        }
        return true;
    }

    return false;
}
