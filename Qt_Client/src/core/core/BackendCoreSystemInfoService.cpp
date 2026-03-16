#include "internal/core/BackendCoreSystemInfoService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>
#include <QVariantMap>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

QString firstNonEmptyLine(const QByteArray &data)
{
    const QString text = QString::fromLocal8Bit(data).trimmed();
    if (text.isEmpty()) {
        return {};
    }
    const QStringList lines = text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            return trimmed;
        }
    }
    return {};
}

QString runPowerShellOneLine(const QString &script, int timeoutMs = 1800)
{
#ifdef Q_OS_WIN
    QProcess p;
    p.start("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script});
    if (!p.waitForStarted(500)) {
        return {};
    }
    p.waitForFinished(timeoutMs);

    const QString out = firstNonEmptyLine(p.readAllStandardOutput());
    if (!out.isEmpty()) {
        return out;
    }
    return firstNonEmptyLine(p.readAllStandardError());
#else
    Q_UNUSED(script);
    Q_UNUSED(timeoutMs);
    return {};
#endif
}

QString formatGiB(qulonglong bytes)
{
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    return QString::number(gib, 'f', 1) + " GB";
}

} // namespace

QVariantMap BackendCoreSystemInfoService::getClientSystemInfo(Backend *backend, BackendPrivate *state)
{
    Q_UNUSED(backend);
    Q_UNUSED(state);

    QVariantMap out;

    out.insert("generatedAt", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    out.insert("hostName", QSysInfo::machineHostName());
    out.insert("osName", QSysInfo::prettyProductName());
    out.insert("kernelType", QSysInfo::kernelType());
    out.insert("kernelVersion", QSysInfo::kernelVersion());
    out.insert("cpuArch", QSysInfo::currentCpuArchitecture());
    out.insert("buildAbi", QSysInfo::buildAbi());
    out.insert("qtVersion", QString::fromLatin1(qVersion()));
    out.insert("logicalCores", QThread::idealThreadCount());

    QString cpuModel = qEnvironmentVariable("PROCESSOR_IDENTIFIER").trimmed();
#ifdef Q_OS_WIN
    if (cpuModel.isEmpty()) {
        cpuModel = runPowerShellOneLine("(Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)");
    }
#endif
    out.insert("cpuModel", cpuModel.isEmpty() ? QString("Unknown") : cpuModel);

    QString gpuModel;
#ifdef Q_OS_WIN
    gpuModel = runPowerShellOneLine("(Get-CimInstance Win32_VideoController | Select-Object -First 1 -ExpandProperty Name)");
#endif
    out.insert("gpuModel", gpuModel.isEmpty() ? QString("Unknown") : gpuModel);

    QString directxVersion;
#ifdef Q_OS_WIN
    directxVersion = runPowerShellOneLine("(Get-ItemProperty 'HKLM:\\SOFTWARE\\Microsoft\\DirectX').Version");
#endif
    out.insert("directxVersion", directxVersion.isEmpty() ? QString("Unknown") : directxVersion);

    QString ramTotalText = "Unknown";
    QString ramAvailText = "Unknown";
#ifdef Q_OS_WIN
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        ramTotalText = formatGiB(mem.ullTotalPhys);
        ramAvailText = formatGiB(mem.ullAvailPhys);
    }
#endif
    out.insert("ramTotal", ramTotalText);
    out.insert("ramAvailable", ramAvailText);

    const QStorageInfo root(QDir::rootPath());
    out.insert("systemDrivePath", root.rootPath());
    if (root.isValid() && root.isReady()) {
        out.insert("systemDriveTotal", formatGiB(root.bytesTotal()));
        out.insert("systemDriveFree", formatGiB(root.bytesAvailable()));
    } else {
        out.insert("systemDriveTotal", QString("Unknown"));
        out.insert("systemDriveFree", QString("Unknown"));
    }

    return out;
}
