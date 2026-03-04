#include "Backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>

void Backend::playbackExportStartDownload(const QUrl &downloadUrl, const QString &outPath) {
    emit playbackExportProgress(95, "내보내기 파일 다운로드 시작");
    QNetworkRequest req(downloadUrl);
    applySslIfNeeded(req);
    QNetworkReply *dl = m_manager->get(req);
    m_playbackExportDownloadReply = dl;
    attachIgnoreSslErrors(dl, "SUNAPI_EXPORT_DOWNLOAD");

    connect(dl, &QNetworkReply::downloadProgress, this, [this](qint64 rec, qint64 total) {
        if (total <= 0) return;
        const int p = qMax(0, qMin(100, static_cast<int>((rec * 100) / total)));
        emit playbackExportProgress(p, QString("다운로드 중 %1%").arg(p));
    });

    connect(dl, &QNetworkReply::finished, this, [this, dl, outPath]() {
        m_playbackExportDownloadReply = nullptr;
        if (m_playbackExportCancelRequested) {
            // 사용자 취소 시 실패 알림 없이 즉시 종료
            dl->deleteLater();
            return;
        }
        if (dl->error() != QNetworkReply::NoError) {
            m_playbackExportInProgress = false;
            emit playbackExportFailed(QString("다운로드 실패: %1").arg(dl->errorString()));
            dl->deleteLater();
            return;
        }
        const QByteArray bytes = dl->readAll();
        // 로컬 저장 경로가 없으면 생성 후 파일 저장
        QFile out(outPath);
        const QFileInfo fi(outPath);
        QDir().mkpath(fi.absolutePath());
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_playbackExportInProgress = false;
            emit playbackExportFailed(QString("파일 쓰기 실패: %1").arg(out.errorString()));
            dl->deleteLater();
            return;
        }
        out.write(bytes);
        out.close();
        m_playbackExportInProgress = false;
        m_playbackExportOutPath.clear();
        m_playbackExportFinalPath.clear();
        emit playbackExportFinished(outPath);
        dl->deleteLater();
    });
}


