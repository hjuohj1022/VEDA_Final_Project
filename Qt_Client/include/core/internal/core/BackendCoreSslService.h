#ifndef BACKEND_CORE_SSL_SERVICE_H
#define BACKEND_CORE_SSL_SERVICE_H

class Backend;
struct BackendPrivate;
class QNetworkReply;
class QNetworkRequest;
class QString;

class BackendCoreSslService
{
public:
    static void setupSslConfiguration(Backend *backend, BackendPrivate *state);
    static void applySslIfNeeded(Backend *backend,
                                 BackendPrivate *state,
                                 QNetworkRequest &request);
    static void attachIgnoreSslErrors(Backend *backend,
                                      BackendPrivate *state,
                                      QNetworkReply *reply,
                                      const QString &tag);
};

#endif // BACKEND_CORE_SSL_SERVICE_H
