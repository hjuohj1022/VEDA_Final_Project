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

// first Non Empty Line 처리 함수
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

// Power Shell One Line 실행 함수
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

// Gi B 포맷 함수
QString formatGiB(qulonglong bytes)
{
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    return QString::number(gib, 'f', 1) + " GB";
}

#ifdef Q_OS_WIN
// sample Cpusage Percent 처리 함수
int sampleCpuUsagePercent()
{
    static ULONGLONG prevIdle = 0;
    static ULONGLONG prevKernel = 0;
    static ULONGLONG prevUser = 0;
    static bool initialized = false;

    FILETIME idleTime {};
    FILETIME kernelTime {};
    FILETIME userTime {};
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return -1;
    }

    ULARGE_INTEGER idle {};
    ULARGE_INTEGER kernel {};
    ULARGE_INTEGER user {};
    idle.LowPart = idleTime.dwLowDateTime;
    idle.HighPart = idleTime.dwHighDateTime;
    kernel.LowPart = kernelTime.dwLowDateTime;
    kernel.HighPart = kernelTime.dwHighDateTime;
    user.LowPart = userTime.dwLowDateTime;
    user.HighPart = userTime.dwHighDateTime;

    const ULONGLONG curIdle = idle.QuadPart;
    const ULONGLONG curKernel = kernel.QuadPart;
    const ULONGLONG curUser = user.QuadPart;

    if (!initialized) {
        prevIdle = curIdle;
        prevKernel = curKernel;
        prevUser = curUser;
        initialized = true;
        return -1;
    }

    const ULONGLONG idleDelta = curIdle - prevIdle;
    const ULONGLONG kernelDelta = curKernel - prevKernel;
    const ULONGLONG userDelta = curUser - prevUser;
    const ULONGLONG totalDelta = kernelDelta + userDelta;

    prevIdle = curIdle;
    prevKernel = curKernel;
    prevUser = curUser;

    if (totalDelta == 0 || totalDelta < idleDelta) {
        return -1;
    }

    const double usage = (static_cast<double>(totalDelta - idleDelta) * 100.0)
                         / static_cast<double>(totalDelta);
    int value = static_cast<int>(usage + 0.5);
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    return value;
}
#endif

} // namespace

// 클라이언트 시스템 정보 조회 함수
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

    // 무거운 시스템 조회 결과 1회 캐시 사용.
    static bool modelInfoCached = false;
    static QString cachedCpuModel = "Unknown";
    static QString cachedGpuModel = "Unknown";
    static QString cachedDirectX = "Unknown";

    if (!modelInfoCached) {
        QString cpuModel = qEnvironmentVariable("PROCESSOR_IDENTIFIER").trimmed();
#ifdef Q_OS_WIN
        if (cpuModel.isEmpty()) {
            cpuModel = runPowerShellOneLine("(Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)");
        }
#endif
        cachedCpuModel = cpuModel.isEmpty() ? QString("Unknown") : cpuModel;

#ifdef Q_OS_WIN
        const QString gpuModel = runPowerShellOneLine("(Get-CimInstance Win32_VideoController | Select-Object -First 1 -ExpandProperty Name)");
        cachedGpuModel = gpuModel.isEmpty() ? QString("Unknown") : gpuModel;

        const QString directxVersion = runPowerShellOneLine("(Get-ItemProperty 'HKLM:\\SOFTWARE\\Microsoft\\DirectX').Version");
        cachedDirectX = directxVersion.isEmpty() ? QString("Unknown") : directxVersion;
#endif
        modelInfoCached = true;
    }
    out.insert("cpuModel", cachedCpuModel);
    out.insert("gpuModel", cachedGpuModel);
    out.insert("directxVersion", cachedDirectX);

    QString ramTotalText = "Unknown";
    QString ramAvailText = "Unknown";
    int cpuUsagePercent = -1;
#ifdef Q_OS_WIN
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        ramTotalText = formatGiB(mem.ullTotalPhys);
        ramAvailText = formatGiB(mem.ullAvailPhys);
    }
    cpuUsagePercent = sampleCpuUsagePercent();
#endif
    out.insert("ramTotal", ramTotalText);
    out.insert("ramAvailable", ramAvailText);
    out.insert("cpuUsagePercent", cpuUsagePercent);

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

// Caps 잠금 이벤트 확인 함수
bool BackendCoreSystemInfoService::isCapsLockOn()
{
#ifdef Q_OS_WIN
    return (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
#else
    return false;
#endif
}
