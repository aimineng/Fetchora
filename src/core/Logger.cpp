#include "Logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QOperatingSystemVersion>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <utility>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dbghelp.h>
#endif

namespace {

/// One day of logging is capped at this, then the file is rotated.
const qint64 kMaxFileBytes = 4 * 1024 * 1024;
/// The whole directory is capped at this; the oldest files go first.
const qint64 kMaxTotalBytes = 20 * 1024 * 1024;
/// Default retention window in days, overridable through the settings page.
const int kDefaultRetentionDays = 7;
/// Lines kept in memory, which is what a crash report can still reach.
const int kRecentLines = 300;

QMutex g_mutex;
QFile g_file;
QString g_directory;
QString g_currentFile;
QStringList g_recent;
QTimer *g_pruneTimer = nullptr;
bool g_installed = false;
int g_retentionDays = kDefaultRetentionDays;

QString stamp(const QDateTime &when)
{
    return when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}

/// Caller holds the mutex.
QString fileNameFor(const QDate &date)
{
    return QStringLiteral("fetchora-%1.log").arg(date.toString(QStringLiteral("yyyy-MM-dd")));
}

/// Caller holds the mutex.
void closeFile()
{
    if (g_file.isOpen()) {
        g_file.flush();
        g_file.close();
    }
}

/**
 * Open (or re-open) today's file, rotating it first when it has grown past the
 * budget. Without the rotation a single day of engine chatter could produce a
 * file nobody can open.
 */
void ensureOpenLocked()
{
    const QString wanted = QDir(g_directory).filePath(fileNameFor(QDate::currentDate()));
    if (g_file.isOpen() && g_currentFile == wanted) {
        if (g_file.size() < kMaxFileBytes)
            return;
        closeFile();
        const QString rotated = QStringLiteral("fetchora-%1-%2.log")
                                    .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")),
                                         QTime::currentTime().toString(QStringLiteral("HHmmss")));
        QFile::rename(wanted, QDir(g_directory).filePath(rotated));
    } else {
        closeFile();
    }

    g_file.setFileName(wanted);
    g_currentFile = wanted;
    if (!g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        g_currentFile.clear();
        std::fprintf(stderr, "Fetchora: cannot write the log file %s\n",
                     qPrintable(wanted));
    }
}

void appendLocked(const QString &line)
{
    g_recent.append(line);
    while (g_recent.size() > kRecentLines)
        g_recent.removeFirst();

    ensureOpenLocked();
    if (!g_file.isOpen())
        return;
    g_file.write(line.toUtf8());
    g_file.write("\n");
    // Flushed per line on purpose: a crash must not take the last line with it.
    g_file.flush();
}

void writeLine(const QString &level, const QString &channel, const QString &text)
{
    const QString line = QStringLiteral("[%1] [%2]%3 %4")
                             .arg(stamp(QDateTime::currentDateTime()), level,
                                  channel.isEmpty() ? QString()
                                                    : QStringLiteral(" [%1]").arg(channel),
                                  text);
    QMutexLocker locker(&g_mutex);
    if (g_directory.isEmpty())
        g_directory = Logger::directory();
    appendLocked(line);
}

QString levelTag(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("D");
    case QtInfoMsg:
        return QStringLiteral("I");
    case QtWarningMsg:
        return QStringLiteral("W");
    case QtCriticalMsg:
        return QStringLiteral("E");
    case QtFatalMsg:
        return QStringLiteral("F");
    }
    return QStringLiteral("?");
}

/// The Qt message handler. Writes to the log and keeps stderr working, so a
/// terminal run and the CI self-test still show what they always showed.
void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QString channel = context.category && *context.category
        ? QString::fromLatin1(context.category)
        : QString();
    writeLine(levelTag(type), channel, message);

    const QByteArray plain = message.toLocal8Bit();
    if (type == QtDebugMsg || type == QtInfoMsg)
        std::fprintf(stdout, "%s\n", plain.constData());
    else
        std::fprintf(stderr, "%s\n", plain.constData());

    if (type == QtFatalMsg) {
        Logger::line(QStringLiteral("crash"), QStringLiteral("qFatal: %1").arg(message), true);
        std::fflush(stderr);
        abort();
    }
}

/// Files in the log directory, newest first.
QList<QFileInfo> logFiles()
{
    QList<QFileInfo> files;
    QDir dir(g_directory);
    const QStringList names = dir.entryList({QStringLiteral("fetchora-*.log")}, QDir::Files, QDir::Time);
    for (const QString &name : names)
        files << QFileInfo(dir.filePath(name));
    return files;
}

#ifdef Q_OS_WIN
/**
 * Write a minidump for the crash. Returns its path, or an empty string.
 *
 * DbgHelp is loaded on demand: a machine without it still gets the log lines,
 * which are the part a user can read.
 */
QString writeMiniDump(EXCEPTION_POINTERS *info)
{
    const QString path = QDir(g_directory).filePath(
        QStringLiteral("crash-%1.dmp")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));

    HANDLE file = CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()), GENERIC_WRITE, 0,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return QString();

    MINIDUMP_EXCEPTION_INFORMATION details = {};
    details.ThreadId = GetCurrentThreadId();
    details.ExceptionPointers = info;
    details.ClientPointers = FALSE;

    const HMODULE dbgHelp = LoadLibraryW(L"dbghelp.dll");
    bool ok = false;
    if (dbgHelp) {
        using MiniDumpWriteDumpFn = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                                   PMINIDUMP_EXCEPTION_INFORMATION,
                                                   PMINIDUMP_USER_STREAM_INFORMATION,
                                                   PMINIDUMP_CALLBACK_INFORMATION);
        auto dump = reinterpret_cast<MiniDumpWriteDumpFn>(
            reinterpret_cast<void *>(GetProcAddress(dbgHelp, "MiniDumpWriteDump")));
        if (dump)
            ok = dump(GetCurrentProcess(), GetCurrentProcessId(), file,
                      static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithHandleData
                                                 | MiniDumpWithThreadInfo),
                      details.ExceptionPointers ? &details : nullptr, nullptr, nullptr)
                != FALSE;
        FreeLibrary(dbgHelp);
    }
    CloseHandle(file);
    if (!ok)
        QFile::remove(path);
    return ok ? path : QString();
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS *info)
{
    const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    const void *address = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress
                                                        : nullptr;

    // Where the exe was loaded: the difference between it and a return address is
    // the offset to look up in Fetchora.map.
    const quintptr base = reinterpret_cast<quintptr>(GetModuleHandleW(nullptr));

    QStringList frames;
    void *stack[32] = {};
    const USHORT count = CaptureStackBackTrace(0, 32, stack, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        const quintptr addressValue = reinterpret_cast<quintptr>(stack[i]);
        frames << QStringLiteral("#%1 0x%2 (+0x%3)")
                      .arg(i)
                      .arg(addressValue, 0, 16)
                      .arg(addressValue >= base ? addressValue - base : 0, 0, 16);
    }

    {
        QMutexLocker locker(&g_mutex);
        if (g_directory.isEmpty())
            g_directory = Logger::directory();
        appendLocked(QStringLiteral("[%1] [F] [crash] unhandled exception 0x%2 at 0x%3 "
                                    "(module base 0x%4)")
                         .arg(stamp(QDateTime::currentDateTime()),
                              QString::number(code, 16),
                              QString::number(reinterpret_cast<quintptr>(address), 16),
                              QString::number(base, 16)));
        for (const QString &frame : std::as_const(frames))
            appendLocked(QStringLiteral("[%1] [F] [crash] %2")
                             .arg(stamp(QDateTime::currentDateTime()), frame));
    }

    const QString dump = writeMiniDump(info);
    {
        QMutexLocker locker(&g_mutex);
        appendLocked(QStringLiteral("[%1] [F] [crash] %2")
                         .arg(stamp(QDateTime::currentDateTime()),
                              dump.isEmpty()
                                  ? QStringLiteral("no minidump was written")
                                  : QStringLiteral("minidump: %1").arg(dump)));
        appendLocked(QStringLiteral("[%1] [F] [crash] the last %2 log lines follow")
                         .arg(stamp(QDateTime::currentDateTime()))
                         .arg(g_recent.size()));
        for (const QString &recent : std::as_const(g_recent))
            appendLocked(QStringLiteral("[%1] [F] [crash] | %2")
                             .arg(stamp(QDateTime::currentDateTime()), recent));
        closeFile();
    }

    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif // Q_OS_WIN

void terminateHandler()
{
    writeLine(QStringLiteral("F"), QStringLiteral("crash"),
              QStringLiteral("std::terminate was called"));
#ifdef Q_OS_WIN
    // A terminate() without an active exception still deserves a dump.
    crashFilter(nullptr);
#endif
    std::fflush(stderr);
    abort();
}

} // namespace

namespace Logger {

QString directory()
{
    // Logs are data, not configuration: AppLocalData is the place the platform
    // expects them (and where a crash report can still write to on every OS).
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty())
        base = QDir::tempPath();
    const QString path = QDir(base).filePath(QStringLiteral("logs"));
    QDir().mkpath(path);
    return path;
}

QString currentFile()
{
    QMutexLocker locker(&g_mutex);
    if (g_currentFile.isEmpty())
        g_currentFile = QDir(directory()).filePath(fileNameFor(QDate::currentDate()));
    return g_currentFile;
}

void line(const QString &channel, const QString &text, bool isError)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &one : lines)
        writeLine(isError ? QStringLiteral("E") : QStringLiteral("I"), channel, one);
}

QStringList recent(int maxLines)
{
    QMutexLocker locker(&g_mutex);
    if (maxLines <= 0 || maxLines >= g_recent.size())
        return g_recent;
    return g_recent.mid(g_recent.size() - maxLines);
}

int retentionDays()
{
    return g_retentionDays;
}

void setRetentionDays(int days)
{
    g_retentionDays = qBound(1, days, 365);
}

int prune()
{
    int removed = 0;
    {
        QMutexLocker locker(&g_mutex);
        if (g_directory.isEmpty())
            g_directory = directory();

        const QDateTime cutoff = QDateTime::currentDateTime().addDays(-g_retentionDays);
        QList<QFileInfo> files = logFiles();   // newest first
        QList<QFileInfo> kept;

        for (const QFileInfo &info : std::as_const(files)) {
            // Never delete the file that is open right now.
            if (info.absoluteFilePath() == g_currentFile) {
                kept << info;
                continue;
            }
            if (info.lastModified() < cutoff) {
                if (QFile::remove(info.absoluteFilePath()))
                    ++removed;
                continue;
            }
            kept << info;
        }

        // Size budget, oldest first: a week of engine chatter still has a ceiling.
        qint64 total = 0;
        for (const QFileInfo &info : std::as_const(kept))
            total += info.size();
        std::reverse(kept.begin(), kept.end());
        for (const QFileInfo &info : std::as_const(kept)) {
            if (total <= kMaxTotalBytes)
                break;
            if (info.absoluteFilePath() == g_currentFile)
                continue;
            if (QFile::remove(info.absoluteFilePath())) {
                total -= info.size();
                ++removed;
            }
        }
        // The mutex is released before logging: writeLine() takes it again.
    }

    if (removed > 0)
        writeLine(QStringLiteral("I"), QStringLiteral("logger"),
                  QStringLiteral("pruned %1 old log file(s), keeping %2 day(s)")
                      .arg(removed)
                      .arg(g_retentionDays));
    return removed;
}

void install()
{
    if (g_installed)
        return;
    g_installed = true;

    {
        QMutexLocker locker(&g_mutex);
        g_directory = directory();
        ensureOpenLocked();
    }

    // The handler goes in first so everything below is in the file too.
    qInstallMessageHandler(messageHandler);
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(crashFilter);
#endif
    std::set_terminate(terminateHandler);

    const QString banner =
        QStringLiteral("===== %1 %2 started %3 =====")
            .arg(QCoreApplication::applicationName().isEmpty()
                     ? QStringLiteral("Fetchora")
                     : QCoreApplication::applicationName(),
                 QCoreApplication::applicationVersion(), stamp(QDateTime::currentDateTime()));
    line(QStringLiteral("app"), banner);
    line(QStringLiteral("app"),
         QStringLiteral("os: %1 %2 (%3), arch %4")
             .arg(QOperatingSystemVersion::current().name(),
                  QSysInfo::prettyProductName(), QSysInfo::kernelVersion(),
                  QSysInfo::currentCpuArchitecture()));
    line(QStringLiteral("app"),
         QStringLiteral("qt: %1 (built against %2)")
             .arg(QString::fromLatin1(qVersion()), QString::fromLatin1(QT_VERSION_STR)));
    line(QStringLiteral("app"), QStringLiteral("exe: %1").arg(QCoreApplication::applicationFilePath()));
    line(QStringLiteral("app"), QStringLiteral("log: %1").arg(currentFile()));

    prune();

    // Once a day is enough for both the prune and the rotation bookkeeping.
    if (!g_pruneTimer && QCoreApplication::instance()) {
        g_pruneTimer = new QTimer(QCoreApplication::instance());
        g_pruneTimer->setInterval(24 * 60 * 60 * 1000);
        QObject::connect(g_pruneTimer, &QTimer::timeout, QCoreApplication::instance(), []() {
            prune();
        });
        g_pruneTimer->start();
    }
}

} // namespace Logger
