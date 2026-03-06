#include "Backend.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

void Backend::loadEnv() {
    m_env.clear();

    // 실행 경로/상위 경로/현재 작업 경로 순서로 .env 탐색
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

        // .env 라인 파싱: 주석(#)과 비정상 라인은 건너뜀
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.isEmpty() || line.startsWith("#")) continue;
            const int eq = line.indexOf('=');
            if (eq <= 0) continue;
            const QString key = line.left(eq).trimmed();
            const QString val = line.mid(eq + 1).trimmed();
            if (!key.isEmpty()) m_env.insert(key, val);
        }
        file.close();
        loadedPath = fi.absoluteFilePath();
        break;
    }

    if (loadedPath.isEmpty()) {
        qWarning() << "[ENV] .env file not found. using defaults.";
    } else {
        qInfo() << "[ENV] loaded from:" << loadedPath
                << "API_URL=" << m_env.value("API_URL", "https://localhost:8080");
    }
}
