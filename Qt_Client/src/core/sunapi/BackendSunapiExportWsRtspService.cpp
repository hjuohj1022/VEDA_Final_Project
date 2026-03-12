#include "internal/sunapi/BackendSunapiExportWsRtspService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QRegularExpression>

void BackendSunapiExportWsRtspService::playbackExportHandleRtspResponse(
    Backend *backend,
    BackendPrivate *state,
    const QString &text,
    const QHash<int, QString> &setupCseqTrack,
    int &setupDoneCount,
    QHash<QString, QByteArray> &trackInterleaved,
    int &h264RtpChannel,
    int setupExpected,
    bool &playSent,
    bool &playAck,
    QString &session,
    int &nextCseq,
    const QString &authHeader,
    const QString &uri,
    const std::function<void(const QByteArray &)> &sendRtsp,
    const std::function<void(const QString &)> &failWith)
{
    Q_UNUSED(state);

    const QRegularExpression statusRe("^RTSP/1\\.0\\s+(\\d{3})", QRegularExpression::MultilineOption);
    const QRegularExpression cseqRe("CSeq:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression sessRe("Session:\\s*([^;\\r\\n]+)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression trRe("Transport:\\s*([^\\r\\n]+)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression ilRe("interleaved\\s*=\\s*(\\d+)\\s*-\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);

    const int status = statusRe.match(text).captured(1).toInt();
    const int cseq = cseqRe.match(text).captured(1).toInt();
    const QString sess = sessRe.match(text).captured(1).trimmed();
    const QString transport = trRe.match(text).captured(1).trimmed();
    if (!sess.isEmpty()) {
        session = sess;
    }

    if (status == 401) {
        if (cseq >= 1 && cseq <= 2 && !playSent && setupDoneCount == 0) {
            return;
        }
        failWith(QString("내보내기 실패: RTSP 인증 오류 (401, CSeq %1)").arg(cseq));
        return;
    }

    if (status >= 400) {
        failWith(QString("내보내기 실패: RTSP 응답 오류 (%1, CSeq %2)").arg(status).arg(cseq));
        return;
    }

    if (setupCseqTrack.contains(cseq)) {
        setupDoneCount++;
        const QString track = setupCseqTrack.value(cseq);
        const QRegularExpressionMatch m = ilRe.match(transport);
        if (m.hasMatch()) {
            trackInterleaved.insert(track, QString("%1-%2").arg(m.captured(1), m.captured(2)).toUtf8());
            if (track.compare("H264", Qt::CaseInsensitive) == 0) {
                h264RtpChannel = m.captured(1).toInt();
            }
        }
        if (setupDoneCount >= setupExpected && !playSent && !session.isEmpty()) {
            QByteArray playReq = backend->buildPlaybackExportRtspRequest(nextCseq,
                                                                         authHeader,
                                                                         session,
                                                                         "PLAY",
                                                                         uri.toUtf8(),
                                                                         true);
            playReq += "Require: samsung-replay-timezone\r\n";
            playReq += "Rate-Control: no\r\n";
            playReq += "\r\n";
            sendRtsp(playReq);
            playSent = true;
        }
        return;
    }

    if (playSent && !playAck && text.contains("RTP-Info:", Qt::CaseInsensitive)) {
        playAck = true;
        emit backend->playbackExportProgress(10, "내보내기 데이터 수신 시작");
        return;
    }
}

