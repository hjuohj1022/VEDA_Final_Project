#ifndef BACKEND_CORE_API_SERVICE_H
#define BACKEND_CORE_API_SERVICE_H

template <typename Key, typename T>
class QMap;
class QString;
class QUrl;
class QNetworkRequest;
class Backend;
struct BackendPrivate;

class BackendCoreApiService
{
public:
    static void applyAuthIfNeeded(Backend *backend,
                                  BackendPrivate *state,
                                  QNetworkRequest &request);
    static QUrl buildApiUrl(Backend *backend,
                            BackendPrivate *state,
                            const QString &path,
                            const QMap<QString, QString> &query);
    static QNetworkRequest makeApiJsonRequest(Backend *backend,
                                              BackendPrivate *state,
                                              const QString &path,
                                              const QMap<QString, QString> &query);
    static bool isSunapiBodyError(Backend *backend,
                                  BackendPrivate *state,
                                  const QString &body,
                                  QString *reason);
};

#endif // BACKEND_CORE_API_SERVICE_H
