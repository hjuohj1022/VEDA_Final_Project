#ifndef BACKEND_CORE_CERT_CONFIG_SERVICE_H
#define BACKEND_CORE_CERT_CONFIG_SERVICE_H

#include <QString>

class Backend;
struct BackendPrivate;

class BackendCoreCertConfigService
{
public:
    static void loadCertDirectoryOverride(Backend *backend, BackendPrivate *state);
    static QString certDirectoryPath(const BackendPrivate *state);
    static QString resolveCertificatePath(const BackendPrivate *state, const QString &rawPath);
    static bool updateCertDirectoryPath(Backend *backend, BackendPrivate *state, const QString &path);
    static bool resetCertDirectoryPath(Backend *backend, BackendPrivate *state);
};

#endif // BACKEND_CORE_CERT_CONFIG_SERVICE_H
