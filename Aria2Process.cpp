#include "Aria2Process.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QDebug>

Aria2Process::Aria2Process(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setProcessEnvironment(QProcessEnvironment::systemEnvironment());

    connect(m_process, &QProcess::started, this, &Aria2Process::onStarted);
    connect(m_process, &QProcess::errorOccurred, this, &Aria2Process::onErrorOccurred);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &Aria2Process::onFinished);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Aria2Process::drainStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &Aria2Process::drainStderr);

    m_executable = locateAria2();
}

Aria2Process::~Aria2Process()
{
    stop();
}

QString Aria2Process::locateAria2(const QString &hint)
{
    // The engine is an external program on every platform; only the file name
    // differs. Windows ships aria2c.exe, Unix (Linux/macOS) installs "aria2c".
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("aria2c.exe");
#else
    const QString name = QStringLiteral("aria2c");
#endif

    // Search order, first hit wins:
    //   1. the path configured in Settings -> RPC/引擎 (explicit user choice),
    //   2. next to the executable (the portable layout, and where build.ps1 /
    //      build.sh / CMake's POST_BUILD step drop the engine),
    //   3. the macOS bundle's Resources directory, then Contents/MacOS,
    //   4. the usual Unix install prefixes (Apple silicon Homebrew first),
    //   5. PATH.
    QStringList candidates;
    QSet<QString> seen;
    auto add = [&candidates, &seen](const QString &path) {
        if (path.isEmpty())
            return;
        const QString cleaned = QDir::cleanPath(path);
        if (seen.contains(cleaned))
            return;
        seen.insert(cleaned);
        candidates << cleaned;
    };

    add(hint);

    const QString appDir = QCoreApplication::applicationDirPath();
    add(appDir + QLatin1Char('/') + name);
    add(appDir + QStringLiteral("/bin/") + name);
    add(appDir + QStringLiteral("/../") + name);
    add(appDir + QStringLiteral("/../../") + name);
#ifdef Q_OS_MACOS
    // Inside Fetchora.app the binary is Contents/MacOS/Fetchora, so
    // "../Resources" is Contents/Resources - the documented place for helper
    // executables and data files in a bundle. "../MacOS" covers a build that put
    // the engine next to the executable instead.
    add(appDir + QStringLiteral("/../Resources/") + name);
    add(appDir + QStringLiteral("/../MacOS/") + name);
#else
    // Also look one level up from bin/ for a Unix tree layout (bin/ + lib/).
    add(appDir + QStringLiteral("/../lib/fetchora/") + name);
#endif

#ifndef Q_OS_WIN
    add(QStringLiteral("/opt/homebrew/bin/") + name); // Apple silicon Homebrew
    add(QStringLiteral("/usr/local/bin/") + name);    // Intel Homebrew / /usr/local
    add(QStringLiteral("/usr/bin/") + name);          // every distribution
#else
    add(QStringLiteral("C:/Program Files/aria2/") + name);
#endif

    for (const QString &c : candidates) {
        const QFileInfo fi(c);
        if (fi.exists() && fi.isFile())
            return QDir::cleanPath(fi.absoluteFilePath());
    }

    // Fall back to PATH lookup. QStandardPaths::findExecutable() already knows
    // the platform's executable suffix, so the bare name is correct here.
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("aria2c"));
    if (!inPath.isEmpty())
        return inPath;

    // Nothing found: return the "next to the executable" path so the caller can
    // report a path the user can act on.
    return QDir::cleanPath(appDir + QLatin1Char('/') + name);
}

void Aria2Process::setExecutable(const QString &path)
{
    m_executable = path;
}

void Aria2Process::setArguments(const QStringList &args)
{
    m_arguments = args;
}

bool Aria2Process::isRunning() const
{
    return m_process->state() == QProcess::Running;
}

qint64 Aria2Process::processId() const
{
    return m_process->processId();
}

void Aria2Process::start()
{
    if (m_process->state() != QProcess::NotRunning) {
        emit logLine(QStringLiteral("Engine is already running."), false);
        return;
    }
    if (m_executable.isEmpty() || !QFileInfo::exists(m_executable)) {
        emit failed(tr("未找到 aria2c，请将其放在程序目录下或加入 PATH。"));
        return;
    }

    m_intentionalStop = false;
    emit logLine(QStringLiteral("Launching: %1 %2").arg(m_executable, m_arguments.join(QLatin1Char(' '))), false);
    m_process->start(m_executable, m_arguments);
}

void Aria2Process::stop()
{
    if (m_process->state() == QProcess::NotRunning)
        return;
    m_intentionalStop = true;
    m_process->terminate();
    if (!m_process->waitForFinished(4000)) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

void Aria2Process::restart()
{
    stop();
    start();
}

void Aria2Process::onStarted()
{
    emit logLine(QStringLiteral("aria2c started (pid %1)").arg(m_process->processId()), false);
    emit started();
}

void Aria2Process::onErrorOccurred(QProcess::ProcessError error)
{
    QString reason;
    switch (error) {
    case QProcess::FailedToStart:
        reason = tr("启动 aria2c 失败（%1）。").arg(m_executable);
        break;
    case QProcess::Crashed:
        reason = tr("aria2c 已崩溃。");
        break;
    case QProcess::Timedout:
        reason = tr("aria2c 未及时响应。");
        break;
    case QProcess::WriteError:
        reason = tr("无法向 aria2c 写入数据。");
        break;
    case QProcess::ReadError:
        reason = tr("无法从 aria2c 读取数据。");
        break;
    default:
        reason = tr("未知的 aria2c 进程错误。");
        break;
    }
    if (!m_intentionalStop)
        emit failed(reason);
    else
        emit logLine(reason, true);
}

void Aria2Process::onFinished(int exitCode, QProcess::ExitStatus status)
{
    drainStdout();
    drainStderr();
    if (status == QProcess::CrashExit && !m_intentionalStop)
        emit logLine(QStringLiteral("aria2c terminated unexpectedly (exit %1).").arg(exitCode), true);
    else
        emit logLine(QStringLiteral("aria2c stopped (exit %1).").arg(exitCode), false);
    emit stopped(exitCode);
    m_intentionalStop = false;
}

void Aria2Process::drainStdout()
{
    emitLines(QProcess::StandardOutput, false);
}

void Aria2Process::drainStderr()
{
    emitLines(QProcess::StandardError, true);
}

void Aria2Process::emitLines(QProcess::ProcessChannel channel, bool isError)
{
    QByteArray &buffer = isError ? m_stderrBuffer : m_stdoutBuffer;
    buffer.append(isError ? m_process->readAllStandardError() : m_process->readAllStandardOutput());

    int idx;
    while ((idx = buffer.indexOf('\n')) >= 0) {
        QByteArray line = buffer.left(idx);
        buffer.remove(0, idx + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (!line.isEmpty())
            emit logLine(QString::fromUtf8(line), isError);
    }
    // Keep a runaway partial line from growing without bound.
    if (buffer.size() > 64 * 1024)
        buffer.clear();
    Q_UNUSED(channel)
}
