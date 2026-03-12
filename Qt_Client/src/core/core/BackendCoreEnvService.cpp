#include "internal/core/BackendCoreEnvService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

void BackendCoreEnvService::loadEnv(Backend *backend, BackendPrivate *state)
{
    Q_UNUSED(backend);

    state->m_env.clear();

    QStringList candidates;
    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << (appDir + "/.env")
               << (appDir + "/../.env")
               << (appDir + "/../../.env")
               << (appDir + "/../../../.env")
               << (QDir::currentPath() + "/.env")
               << ".env";

    QString loadedPath;
    for (const QString &path : candidates) {
        QFileInfo fi(path);
        if (!fi.exists() || !fi.isFile()) {
            continue;
        }

        QFile file(fi.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.isEmpty() || line.startsWith("#")) {
                continue;
            }
            const int eq = line.indexOf('=');
            if (eq <= 0) {
                continue;
            }
            const QString key = line.left(eq).trimmed();
            const QString val = line.mid(eq + 1).trimmed();
            if (!key.isEmpty()) {
                state->m_env.insert(key, val);
            }
        }
        file.close();
        loadedPath = fi.absoluteFilePath();
        break;
    }

    if (loadedPath.isEmpty()) {
        qWarning() << "[ENV] .env file not found. using defaults.";
    } else {
        qInfo() << "[ENV] loaded from:" << loadedPath
                << "API_URL=" << state->m_env.value("API_URL", "https://localhost:8080");
    }
}

