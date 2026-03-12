#include "internal/sunapi/BackendSunapiExportDownloadService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>

void BackendSunapiExportDownloadService::playbackExportStartDownload(Backend *backend,
                                                                     BackendPrivate *state,
                                                                     const QUrl &downloadUrl,
                                                                     const QString &outPath)
{
    emit backend->playbackExportProgress(95, "내보내기 파일 다운로드 시작");
    QNetworkRequest req(downloadUrl);
    backend->applySslIfNeeded(req);
    QNetworkReply *dl = state->m_manager->get(req);
    state->m_playbackExportDownloadReply = dl;
    backend->attachIgnoreSslErrors(dl, "SUNAPI_EXPORT_DOWNLOAD");

    QObject::connect(dl, &QNetworkReply::downloadProgress, backend, [backend](qint64 rec, qint64 total) {
        if (total <= 0) {
            return;
        }
        const int p = qMax(0, qMin(100, static_cast<int>((rec * 100) / total)));
        emit backend->playbackExportProgress(p, QString("다운로드 진행 %1%").arg(p));
    });

    QObject::connect(dl, &QNetworkReply::finished, backend, [backend, state, dl, outPath]() {
        state->m_playbackExportDownloadReply = nullptr;
        if (state->m_playbackExportCancelRequested) {
            dl->deleteLater();
            return;
        }
        if (dl->error() != QNetworkReply::NoError) {
            state->m_playbackExportInProgress = false;
            emit backend->playbackExportFailed(QString("다운로드 실패: %1").arg(dl->errorString()));
            dl->deleteLater();
            return;
        }
        const QByteArray bytes = dl->readAll();
        QFile out(outPath);
        const QFileInfo fi(outPath);
        QDir().mkpath(fi.absolutePath());
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            state->m_playbackExportInProgress = false;
            emit backend->playbackExportFailed(QString("파일 저장 실패: %1").arg(out.errorString()));
            dl->deleteLater();
            return;
        }
        out.write(bytes);
        out.close();
        state->m_playbackExportInProgress = false;
        state->m_playbackExportOutPath.clear();
        state->m_playbackExportFinalPath.clear();
        emit backend->playbackExportFinished(outPath);
        dl->deleteLater();
    });
}

