#include "Aria2Manager.h"

#include "Logger.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QDebug>

namespace {

/// Every field we want from aria2.tellStatus / tellActive / tellWaiting /
/// tellStopped. Keeping the list in one place avoids heavy default payloads.
const QStringList &statusKeys()
{
    static const QStringList keys = {
        QStringLiteral("gid"),           QStringLiteral("status"),
        QStringLiteral("totalLength"),   QStringLiteral("completedLength"),
        QStringLiteral("uploadLength"),  QStringLiteral("downloadSpeed"),
        QStringLiteral("uploadSpeed"),   QStringLiteral("connections"),
        QStringLiteral("numSeeders"),    QStringLiteral("seeder"),
        QStringLiteral("pieceLength"),   QStringLiteral("numPieces"),
        QStringLiteral("errorCode"),     QStringLiteral("errorMessage"),
        QStringLiteral("dir"),           QStringLiteral("files"),
        QStringLiteral("bittorrent"),    QStringLiteral("infoHash"),
        QStringLiteral("belongsTo"),     QStringLiteral("followedBy"),
        QStringLiteral("following"),     QStringLiteral("verifiedLength"),
        QStringLiteral("verifyIntegrityPending"), QStringLiteral("bitfield"),
    };
    return keys;
}

qint64 toLongLong(const QJsonValue &v)
{
    if (v.isString())
        return v.toString().toLongLong();
    if (v.isDouble())
        return qint64(v.toDouble());
    return 0;
}

QString humanSize(double bytes)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int unit = 0;
    while (bytes >= 1024.0 && unit < 5) {
        bytes /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(QString::number(bytes, 'f', unit == 0 ? 0 : 2), units[unit]);
}

} // namespace

Aria2Manager::Aria2Manager(SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    qRegisterMetaType<QVariantList>("QVariantList");

    m_history = new DownloadHistory(this);
    m_history->setDatabasePath(m_settings->enableSqliteHistory() ? m_settings->sqliteDbPath()
                                                                 : QStringLiteral(":memory:"));
    if (m_settings->enableEngineLog()) {
        appendEngineLog(QStringLiteral("history store: ready=%1 path=%2")
                            .arg(m_history->isReady() ? QStringLiteral("yes") : QStringLiteral("no"),
                                 m_history->databasePath()),
                        false);
    }

    initProcess();
    initClient();
    initBridge();
    initTimers();

    connect(m_settings, &SettingsManager::runtimeOptionsChanged, this, &Aria2Manager::applySettingsRuntime);
    connect(m_settings, &SettingsManager::aria2SettingsChanged, this, [this]() {
        // Port/secret changes must be pushed to the client immediately.
        m_client->setEndpoint(QStringLiteral("127.0.0.1"), quint16(m_settings->rpcListenPort()),
                              m_settings->rpcSecret());
        if (m_bridge && m_bridge->port() != quint16(m_settings->browserPort()))
            initBridge();
    });

    connect(m_history, &DownloadHistory::changed, this, &Aria2Manager::historyChanged);

    startEngine();
}

Aria2Manager::~Aria2Manager()
{
    m_shuttingDown = true;
    m_stoppingEngine = true;
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_client && m_client->isConnected())
        m_client->saveSession();
    if (m_bridge)
        m_bridge->stop();
    if (m_process) {
        m_process->stop();
    }
    Logger::line(QStringLiteral("app"), QStringLiteral("shutdown complete"));
}

// ============================================================================
//  Wiring
// ============================================================================

void Aria2Manager::initProcess()
{
    m_process = new Aria2Process(this);
    connect(m_process, &Aria2Process::logLine, this, &Aria2Manager::onProcessLog);
    connect(m_process, &Aria2Process::failed, this, &Aria2Manager::onProcessFailed);
    connect(m_process, &Aria2Process::started, this, &Aria2Manager::onProcessStateChanged);
    connect(m_process, &Aria2Process::started, this, [this]() {
        // A replacement engine is up: arm the hand-over. The poll (or the first
        // successful RPC) does the rest, because that is the moment the new
        // process is actually answering.
        if (!m_pendingQueue.isEmpty())
            m_restoreArmed = true;
    });
    connect(m_process, &Aria2Process::stopped, this, [this](int exitCode) {
        onProcessStateChanged();
        if (m_shuttingDown)
            return;
        // Whatever happens next, remember what was running: a fresh engine starts
        // with an empty queue, and without this the downloads the user was
        // watching simply disappear. The poll-time snapshot comes first because a
        // failed poll cycle may already have erased the task list by now.
        if (m_pendingQueue.isEmpty())
            m_pendingQueue = m_queueSnapshot.isEmpty() ? captureQueue() : m_queueSnapshot;
        if (!m_pendingQueue.isEmpty())
            Logger::line(QStringLiteral("engine"),
                         QStringLiteral("engine exit: %1 download(s) remembered for the restart")
                             .arg(m_pendingQueue.size()));
        // Our own stops (restart, settings change, quit) are not news; anything
        // else means the engine died under the user's feet and the restart guard
        // is about to bring it back. Both the manager and the process itself know
        // whether a stop was asked for - either answer is enough.
        if (m_stoppingEngine || m_process->lastStopWasIntentional()
            || !m_settings->autoRestartEngine())
            return;
        Logger::line(QStringLiteral("engine"),
                     QStringLiteral("aria2c exited unexpectedly (code %1); restarting").arg(exitCode),
                     true);
        appendEngineLog(tr("aria2 引擎意外退出（退出码 %1）。").arg(exitCode), true);
    });
}

void Aria2Manager::initClient()
{
    m_client = new Aria2Client(this);
    m_client->setEndpoint(QStringLiteral("127.0.0.1"), quint16(m_settings->rpcListenPort()),
                          m_settings->rpcSecret());

    connect(m_client, &Aria2Client::connectedChanged, this, &Aria2Manager::onClientConnectedChanged);
    connect(m_client, &Aria2Client::logMessage, this, &Aria2Manager::onRpcLog);
    connect(m_client, &Aria2Client::callFailed, this, [this](const QString &method, const QString &error) {
        if (m_settings->enableRpcConsole())
            appendRpcLog(QStringLiteral("!!"), QStringLiteral("%1: %2").arg(method, error));
        ++m_errorStreak;
        if (m_errorStreak > 12)
            setEngineError(error);
    });
}

void Aria2Manager::initBridge()
{
    if (!m_bridge) {
        m_bridge = new HttpServer(this);
        connect(m_bridge, &HttpServer::downloadRequested, this, &Aria2Manager::onBridgeDownload);
        connect(m_bridge, &HttpServer::downloadBatchRequested, this, &Aria2Manager::onBridgeBatch);
        connect(m_bridge, &HttpServer::torrentRequested, this, &Aria2Manager::onBridgeTorrent);
        connect(m_bridge, &HttpServer::magnetRequested, this, &Aria2Manager::onBridgeMagnet);
        connect(m_bridge, &HttpServer::pauseRequested, this, [this](const QString &gid) {
            if (gid.isEmpty())
                pauseAll();
            else
                pauseTask(gid);
        });
        connect(m_bridge, &HttpServer::unpauseRequested, this, [this](const QString &gid) {
            if (gid.isEmpty())
                resumeAll();
            else
                resumeTask(gid);
        });
        connect(m_bridge, &HttpServer::removeRequested, this, [this](const QString &gid) {
            if (!gid.isEmpty())
                removeTask(gid, 0);
        });
        connect(m_bridge, &HttpServer::statusRequested, this, &Aria2Manager::onSocketStatus);
        connect(m_bridge, &HttpServer::clientCountChanged, this, &Aria2Manager::bridgeClientsChanged);
    }

    if (!m_settings->browserIntegration()) {
        m_bridge->stop();
        emit bridgeClientsChanged();
        return;
    }

    const quint16 desired = quint16(m_settings->browserPort());
    if (m_bridge->isListening() && m_bridge->port() == desired) {
        emit bridgeClientsChanged();
        return;
    }

    m_bridge->stop();
    if (!m_bridge->start(desired)) {
        emit toast(tr("浏览器桥接端口 %1 无法绑定：%2")
                       .arg(desired)
                       .arg(m_bridge->lastError()),
                   true);
    }
    emit bridgeClientsChanged();
}

void Aria2Manager::onSocketStatus(const QString &requestId)
{
    // WebSocket `status` command: reply with a compact live snapshot.
    if (requestId.isEmpty())
        return;

    QJsonObject payload;
    for (auto it = m_statistics.constBegin(); it != m_statistics.constEnd(); ++it)
        payload.insert(it.key(), QJsonValue::fromVariant(it.value()));
    payload.insert(QStringLiteral("paused"), m_paused);
    payload.insert(QStringLiteral("engineReady"), m_client->isConnected());

    QJsonArray tasks;
    for (const QVariant &v : m_activeList) {
        const QVariantMap t = v.toMap();
        QJsonObject o;
        o.insert(QStringLiteral("gid"), t.value(QStringLiteral("gid")).toString());
        o.insert(QStringLiteral("name"), t.value(QStringLiteral("fileName")).toString());
        o.insert(QStringLiteral("progress"), t.value(QStringLiteral("progress")).toInt());
        o.insert(QStringLiteral("speed"), t.value(QStringLiteral("downloadSpeed")).toDouble());
        tasks.append(o);
    }
    payload.insert(QStringLiteral("tasks"), tasks);

    // Diagnostics: expose the tail of the engine log so a stuck download can be
    // diagnosed from the extension's popup or a WebSocket probe.
    if (m_settings->enableEngineLog()) {
        const QStringList lines = m_engineLog.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const int take = qMin(40, lines.size());
        payload.insert(QStringLiteral("engineLog"),
                       lines.mid(lines.size() - take).join(QLatin1Char('\n')));
    }

    m_bridge->wsReply(requestId, payload);
}

void Aria2Manager::initTimers()
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(m_pollInterval);
    connect(m_pollTimer, &QTimer::timeout, this, &Aria2Manager::poll);
    m_pollTimer->start();

    m_clipboardTimer = new QTimer(this);
    m_clipboardTimer->setInterval(700);
    connect(m_clipboardTimer, &QTimer::timeout, this, &Aria2Manager::onClipboardTimer);
    if (m_settings->clipboardMonitor())
        m_clipboardTimer->start();

    m_schedulerTimer = new QTimer(this);
    m_schedulerTimer->setInterval(20000);
    connect(m_schedulerTimer, &QTimer::timeout, this, &Aria2Manager::onSchedulerTimer);
    m_schedulerTimer->start();

    // A *repeating* guard, not a one-shot: as a one-shot it fired 2.5 s after
    // startup - while the engine was still booting, so it returned early - and
    // never again, which is why killing aria2c left the app without an engine.
    m_restartGuard = new QTimer(this);
    m_restartGuard->setInterval(2500);
    connect(m_restartGuard, &QTimer::timeout, this, [this]() {
        if (m_shuttingDown || !m_settings->autoRestartEngine())
            return;
        if (m_process->isRunning() && !m_client->isConnected())
            return; // still booting
        if (!m_process->isRunning() && m_restartAttempts < 5) {
            ++m_restartAttempts;
            appendEngineLog(tr("引擎未运行，正在重启（第 %1 次）。").arg(m_restartAttempts), true);
            // Only the first attempt is announced: five toasts for one dead
            // engine would be noise, and the engine log keeps the count.
            if (m_restartAttempts == 1) {
                m_recoveringEngine = true;
                emit toast(tr("aria2 引擎已退出，正在自动重启…"), true);
            }
            startEngine();
        } else if (!m_process->isRunning() && m_restartAttempts >= 5 && m_recoveringEngine) {
            // Stop trying, but say so: silently giving up looks like the app
            // simply ignoring every download.
            m_recoveringEngine = false;
            emit toast(tr("aria2 引擎连续 %1 次启动失败，已停止自动重启。"
                          "请在「设置 → RPC / 引擎」里检查引擎路径与参数。")
                           .arg(m_restartAttempts),
                       true);
        }
    });
    m_restartGuard->start();
}

// ============================================================================
//  Engine lifecycle
// ============================================================================

void Aria2Manager::startEngine()
{
    if (m_process->isRunning())
        return;

    m_stoppingEngine = false;

    const QString exe = m_settings->aria2Executable().isEmpty()
                            ? Aria2Process::locateAria2()
                            : m_settings->aria2Executable();
    m_process->setExecutable(exe);

    QStringList args = m_settings->buildAria2Arguments();

    // Fail fast with a precise message: aria2c aborts (exit 28) on the first
    // unknown switch, which would otherwise look like "the engine just dies".
    const QString badOption = firstInvalidAria2Option(exe, args);
    if (!badOption.isEmpty()) {
        const QString message = tr("aria2c rejected the option \"%1\". "
                                   "Clear it in Settings → Advanced, or reset the extra arguments.")
                                    .arg(badOption);
        appendEngineLog(message, true);
        setEngineError(message);
        if (!m_engineErrorNotified) {
            m_engineErrorNotified = true;
            emit notification(tr("引擎错误"), message, true);
        }
        return;
    }

    m_process->setArguments(args);

    // Make sure the session directory exists before aria2 tries to write it.
    const QString session = m_settings->sessionFile();
    if (!session.isEmpty())
        QDir().mkpath(QFileInfo(session).absolutePath());
    QDir().mkpath(m_settings->downloadDir());

    setEngineError(QString());
    m_engineErrorNotified = false;
    m_pollTimer->start();
    m_process->start();
}

QString Aria2Manager::firstInvalidAria2Option(const QString &executable, const QStringList &args) const
{
    if (executable.isEmpty() || !QFileInfo::exists(executable))
        return {};

    // Only user-controllable switches can be wrong, so validate just those;
    // the built-in list is covered by the settings unit of work.
    QStringList toCheck;
    for (const QString &a : args) {
        if (!a.startsWith(QLatin1String("--")))
            continue;
        const int eq = a.indexOf(QLatin1Char('='));
        const QString name = eq > 0 ? a.left(eq) : a;
        static const QSet<QString> userOwned = {
            QStringLiteral("--user-agent"),  QStringLiteral("--referer"),
            QStringLiteral("--all-proxy"),   QStringLiteral("--http-proxy"),
            QStringLiteral("--https-proxy"), QStringLiteral("--ftp-proxy"),
            QStringLiteral("--all-proxy-user"), QStringLiteral("--all-proxy-passwd"),
            QStringLiteral("--ca-certificate"), QStringLiteral("--certificate"),
            QStringLiteral("--private-key"), QStringLiteral("--min-tls-version"),
            QStringLiteral("--bt-external-ip"), QStringLiteral("--bt-tracker"),
            QStringLiteral("--dht-entry-point"), QStringLiteral("--dht-entry-point6"),
            QStringLiteral("--dht-file-path"), QStringLiteral("--conf-path"),
            QStringLiteral("--input-file"),  QStringLiteral("--save-session"),
            QStringLiteral("--file-allocation"), QStringLiteral("--stream-piece-selector"),
            QStringLiteral("--disk-cache"),  QStringLiteral("--min-split-size"),
            QStringLiteral("--max-download-limit"), QStringLiteral("--max-upload-limit"),
            QStringLiteral("--rpc-secret"),
        };
        if (userOwned.contains(name))
            toCheck << a;
    }
    for (const QString &a : m_settings->extraAria2ArgumentsList())
        toCheck << a;

    if (toCheck.isEmpty())
        return {};

    // `aria2c --help=#all` prints the whole option table and exits 0.
    QProcess probe;
    probe.start(executable, {QStringLiteral("--help=#all")});
    if (!probe.waitForFinished(8000))
        return {};
    const QString help = QString::fromUtf8(probe.readAllStandardOutput());

    for (const QString &candidate : toCheck) {
        if (!candidate.startsWith(QLatin1String("--")))
            continue;
        const int eq = candidate.indexOf(QLatin1Char('='));
        const QString name = eq > 0 ? candidate.left(eq) : candidate;
        if (!help.contains(name + QLatin1Char(' ')) && !help.contains(name + QLatin1Char('='))
            && !help.contains(name + QLatin1String("[="))) {
            return candidate;
        }
    }
    return {};
}

void Aria2Manager::stopEngine()
{
    if (m_client->isConnected())
        m_client->saveSession();
    m_paused = true;
    emit globalPausedChanged();
    m_stoppingEngine = true;
    m_process->stop();
}

QList<Aria2Manager::QueuedTask> Aria2Manager::captureQueue() const
{
    QList<QueuedTask> queue;
    const QVariantList sources = m_activeList + m_waitingList;
    for (const QVariant &value : sources) {
        const QVariantMap task = value.toMap();
        QueuedTask entry;
        entry.uri = task.value(QStringLiteral("uri")).toString();
        if (entry.uri.isEmpty()) {
            const QStringList uris = task.value(QStringLiteral("uris")).toStringList();
            if (!uris.isEmpty())
                entry.uri = uris.first();
        }
        // A torrent added from a local .torrent file has no URI; its info hash
        // still identifies it, and --bt-load-saved-metadata lets the new engine
        // find the metadata and the control file again.
        if (entry.uri.isEmpty()) {
            const QString infoHash = task.value(QStringLiteral("infoHash")).toString();
            if (!infoHash.isEmpty()) {
                entry.uri = QStringLiteral("magnet:?xt=urn:btih:%1").arg(infoHash);
                const QString name = task.value(QStringLiteral("fileName")).toString();
                if (!name.isEmpty())
                    entry.uri += QStringLiteral("&dn=")
                        + QString::fromLatin1(QUrl::toPercentEncoding(name));
            }
        }
        if (entry.uri.isEmpty())
            continue;   // nothing to hand back - a metalink or a metadata-only task
        entry.dir = task.value(QStringLiteral("dir")).toString();
        entry.paused = task.value(QStringLiteral("status")).toString() == QLatin1String("paused");
        queue.append(entry);
    }
    return queue;
}

QString Aria2Manager::uniqueOutputName(const QString &uri, const QString &dir) const
{
    // QUrl::fileName() percent-decodes, which is what the file on disk will be
    // called. A URI that ends in a slash (or has no path) has no name to build on.
    const QString base = QUrl(uri).fileName();
    if (base.isEmpty())
        return QString();

    const QFileInfo info(base);
    const QString stem = info.completeBaseName();
    const QString suffix = info.suffix();
    if (stem.isEmpty())
        return QString();

    const QString folder = dir.isEmpty() ? m_settings->downloadDir() : dir;
    for (int n = 2; n < 1000; ++n) {
        const QString candidate = suffix.isEmpty()
            ? QStringLiteral("%1 (%2)").arg(stem).arg(n)
            : QStringLiteral("%1 (%2).%3").arg(stem).arg(n).arg(suffix);
        if (!QFileInfo::exists(QDir(folder).filePath(candidate)))
            return candidate;
    }
    return QString();
}

void Aria2Manager::restoreQueue()
{
    const QList<QueuedTask> queue = m_pendingQueue;
    m_pendingQueue.clear();
    if (queue.isEmpty())
        return;

    for (const QueuedTask &task : queue) {
        QVariantMap options;
        if (!task.dir.isEmpty())
            options.insert(QStringLiteral("dir"), task.dir);
        // Resume instead of starting over: aria2 picks the partial file and its
        // .aria2 control file back up.
        options.insert(QStringLiteral("continue"), QStringLiteral("true"));
        if (task.paused)
            options.insert(QStringLiteral("pause"), QStringLiteral("true"));
        m_client->addUri({task.uri}, options, -1, nullptr);
    }

    const QString message = tr("引擎已重启，已恢复 %1 个未完成的下载。").arg(queue.size());
    appendEngineLog(message, false);
    Logger::line(QStringLiteral("engine"),
                 QStringLiteral("restored %1 download(s) after the engine restart").arg(queue.size()));
    emit toast(message, false);
    refreshNow();
}

void Aria2Manager::restartEngine()
{
    appendEngineLog(tr("Restarting aria2 with the current settings…"), false);
    m_pollInFlight = false;
    m_client->setEndpoint(QStringLiteral("127.0.0.1"), quint16(m_settings->rpcListenPort()),
                          m_settings->rpcSecret());
    // Changing a setting restarts the engine, and the downloads have to survive
    // that just like they survive a crash.
    if (m_pendingQueue.isEmpty())
        m_pendingQueue = m_queueSnapshot.isEmpty() ? captureQueue() : m_queueSnapshot;
    m_stoppingEngine = true;
    m_process->stop();
    m_restartAttempts = 0;
    m_recoveringEngine = false;
    QTimer::singleShot(400, this, [this]() { startEngine(); });
}

void Aria2Manager::onProcessLog(const QString &line, bool isError)
{
    appendEngineLog(line, isError);
    // The engine's own output is the first thing to look at after a crash, so it
    // belongs in the log file, not only in the in-memory console.
    Logger::line(QStringLiteral("engine"), line, isError);
}

void Aria2Manager::onProcessFailed(const QString &reason)
{
    setEngineError(reason);
    appendEngineLog(reason, true);
    if (!m_engineErrorNotified) {
        m_engineErrorNotified = true;
        emit notification(tr("引擎错误"), reason, true);
    }
}

void Aria2Manager::onProcessStateChanged()
{
    emit engineRunningChanged();
}

void Aria2Manager::onClientConnectedChanged(bool connected)
{
    if (connected) {
        m_errorStreak = 0;
        m_restartAttempts = 0;
        setEngineError(QString());
        appendEngineLog(tr("已连接到 aria2 %1。").arg(m_client->aria2Version()), false);
        // Worth a line in the file log: "did the app notice the engine came back"
        // is the first question when downloads stop after an engine crash.
        Logger::line(QStringLiteral("engine"),
                     QStringLiteral("connected to aria2 %1 (%2 download(s) waiting to be restored)")
                         .arg(m_client->aria2Version())
                         .arg(m_pendingQueue.size()));
        // The engine came back after dying on its own: close that story, and give
        // it back the downloads it was working on.
        if (m_recoveringEngine) {
            m_recoveringEngine = false;
            emit toast(tr("aria2 引擎已重新启动，下载可以继续了。"), false);
        }
        if (m_restoreArmed || !m_pendingQueue.isEmpty()) {
            m_restoreArmed = false;
            restoreQueue();
        }
        // Anything the user asked for while the engine was away goes in now, in
        // the order it was asked for.
        if (!m_pendingAdds.isEmpty()) {
            const QList<QPair<QString, QVariantMap>> queued = m_pendingAdds;
            m_pendingAdds.clear();
            for (const auto &item : queued)
                addUri(item.first, item.second);
        }
        refreshGlobalOptions();
        poll();
    } else {
        Logger::line(QStringLiteral("engine"),
                     QStringLiteral("lost the connection to aria2"), true);
        m_paused = false;
        emit globalPausedChanged();
    }
    emit engineReadyChanged();
    emit engineStateChanged();
    emit engineRunningChanged();
}

void Aria2Manager::setEngineError(const QString &message)
{
    if (m_engineError == message)
        return;
    m_engineError = message;
    emit engineErrorChanged();
}

void Aria2Manager::appendEngineLog(const QString &line, bool isError)
{
    const QString stamped = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))
        + (isError ? QStringLiteral("  !  ") : QStringLiteral("     ")) + line;
    m_engineLog += stamped + QLatin1Char('\n');
    if (m_engineLog.size() > 200000)
        m_engineLog = m_engineLog.right(180000);
    emit engineLogChanged();
    if (isError)
        qWarning().noquote() << "[aria2]" << line;
}

void Aria2Manager::appendRpcLog(const QString &direction, const QString &text)
{
    m_rpcLog += QStringLiteral("%1 %2 %3\n")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                         direction, text);
    if (m_rpcLog.size() > 400000)
        m_rpcLog = m_rpcLog.right(360000);
    emit rpcLogChanged();
}

void Aria2Manager::onRpcLog(const QString &direction, const QString &text)
{
    if (m_settings->enableRpcConsole())
        appendRpcLog(direction, text);
}

// ============================================================================
//  Polling
// ============================================================================

void Aria2Manager::poll()
{
    if (m_shuttingDown || !m_client->isConnected() || m_pollInFlight)
        return;

    m_pollInFlight = true;
    m_pendingBuckets = 3;
    m_failedBuckets = 0;
    m_newlySeen.clear();

    m_client->tellActive(statusKeys(), [this](const QJsonValue &result, bool isError, const QString &) {
        if (!isError) {
            const QJsonArray arr = result.toArray();
            for (const QJsonValue &v : arr)
                applyTask(v.toObject(), QStringLiteral("active"));
        } else {
            ++m_failedBuckets;
        }
        finishPollCycle();
    });

    m_client->tellWaiting(0, 500, statusKeys(), [this](const QJsonValue &result, bool isError, const QString &) {
        if (!isError) {
            const QJsonArray arr = result.toArray();
            for (const QJsonValue &v : arr)
                applyTask(v.toObject(), QStringLiteral("waiting"));
        } else {
            ++m_failedBuckets;
        }
        finishPollCycle();
    });

    m_client->tellStopped(0, 500, statusKeys(), [this](const QJsonValue &result, bool isError, const QString &) {
        if (!isError) {
            const QJsonArray arr = result.toArray();
            for (const QJsonValue &v : arr)
                applyTask(v.toObject(), QStringLiteral("stopped"));
        } else {
            ++m_failedBuckets;
        }
        finishPollCycle();
    });

    m_client->getGlobalStat([this](const QJsonValue &result, bool isError, const QString &) {
        if (isError)
            return;
        const QJsonObject o = result.toObject();
        QVariantMap stats;
        stats[QStringLiteral("downloadSpeed")] = toLongLong(o.value(QStringLiteral("downloadSpeed")));
        stats[QStringLiteral("uploadSpeed")] = toLongLong(o.value(QStringLiteral("uploadSpeed")));
        stats[QStringLiteral("numActive")] = o.value(QStringLiteral("numActive")).toString().toInt();
        stats[QStringLiteral("numWaiting")] = o.value(QStringLiteral("numWaiting")).toString().toInt();
        stats[QStringLiteral("numStopped")] = o.value(QStringLiteral("numStopped")).toString().toInt();
        stats[QStringLiteral("numStoppedTotal")] =
            o.value(QStringLiteral("numStoppedTotal")).toString().toInt();
        stats[QStringLiteral("rpcLatency")] = m_client->lastLatency();
        stats[QStringLiteral("rpcRequests")] = double(m_client->requestCount());
        stats[QStringLiteral("rpcErrors")] = double(m_client->errorCount());
        stats[QStringLiteral("engineVersion")] = m_client->aria2Version();
        stats[QStringLiteral("enginePid")] = enginePid();

        // Aggregate the per-task totals so the dashboard has one number to show.
        qint64 totalDown = 0, totalUp = 0, doneDown = 0;
        int activeCount = 0, waitingCount = 0, stoppedCount = 0;
        for (auto it = m_taskMap.constBegin(); it != m_taskMap.constEnd(); ++it) {
            const Task &t = it.value();
            totalDown += t.totalLength;
            doneDown += t.completedLength;
            totalUp += t.uploadLength;
            if (t.status == QLatin1String("active"))
                ++activeCount;
            else if (t.status == QLatin1String("waiting") || t.status == QLatin1String("paused"))
                ++waitingCount;
            else
                ++stoppedCount;
        }
        stats[QStringLiteral("totalLength")] = totalDown;
        stats[QStringLiteral("completedLength")] = doneDown;
        stats[QStringLiteral("uploadedLength")] = totalUp;
        stats[QStringLiteral("taskCount")] = m_taskMap.size();
        stats[QStringLiteral("activeCount")] = activeCount;
        stats[QStringLiteral("waitingCount")] = waitingCount;
        stats[QStringLiteral("stoppedCount")] = stoppedCount;
        stats[QStringLiteral("downloadedToday")] = doneDown;

        m_statistics = stats;
        emit statisticsChanged();
    });

    m_client->getSessionInfo([this](const QJsonValue &result, bool isError, const QString &) {
        if (isError)
            return;
        m_sessionInfo = result.toObject().toVariantMap();
        emit sessionInfoChanged();
    });

    // Detail panel for the selected task.
    if (!m_detailGid.isEmpty())
        fetchTaskDetail(m_detailGid);
}

void Aria2Manager::finishPollCycle()
{
    if (--m_pendingBuckets > 0)
        return;

    m_pollInFlight = false;
    rebuildLists();

    // A successful poll against the replacement engine is the moment to give the
    // downloads back (see m_restoreArmed).
    if (m_restoreArmed && m_client->isConnected()) {
        m_restoreArmed = false;
        restoreQueue();
    }

    // Keep the last *complete* picture of what the engine was doing: this is what
    // a replacement engine is handed back if the current one dies. A cycle with
    // failures saw nothing because the engine is gone, not because the user
    // cleared the list, so it must not overwrite the snapshot.
    if (m_failedBuckets == 0)
        m_queueSnapshot = captureQueue();

    // Drop tasks that aria2 no longer knows about (e.g. after removeDownloadResult).
    const QList<QString> known = m_newlySeen.values();
    QSet<QString> stillPresent(known.constBegin(), known.constEnd());
    for (auto it = m_taskMap.begin(); it != m_taskMap.end();) {
        if (!stillPresent.contains(it.key())) {
            m_previousStatus.remove(it.key());
            it = m_taskMap.erase(it);
            m_modelSignature.clear();
        } else {
            ++it;
        }
    }
    rebuildFiltered();
}

void Aria2Manager::applyTask(const QJsonObject &obj, const QString &bucket)
{
    Q_UNUSED(bucket)
    const QString gid = obj.value(QStringLiteral("gid")).toString();
    if (gid.isEmpty())
        return;
    // Removed tasks stay removed even if a poll still sees them (see removeTask).
    if (m_dismissed.contains(gid))
        return;

    Task previous;
    const bool existed = m_taskMap.contains(gid);
    if (existed)
        previous = m_taskMap.value(gid);

    Task task = parseTask(obj);
    if (existed) {
        // Preserve detail data that the list endpoints do not carry.
        task.peers = previous.peers;
        task.servers = previous.servers;
        task.optionsMap = previous.optionsMap;
    }

    const QString prevStatus = m_previousStatus.value(gid);
    m_previousStatus.insert(gid, task.status);
    m_taskMap.insert(gid, task);
    m_newlySeen.insert(gid);

    if (!existed || prevStatus != task.status)
        handleStatusTransition(task, prevStatus);
}

Aria2Manager::Task Aria2Manager::parseTask(const QJsonObject &obj) const
{
    Task t;
    t.gid = obj.value(QStringLiteral("gid")).toString();
    t.status = obj.value(QStringLiteral("status")).toString();
    t.dir = obj.value(QStringLiteral("dir")).toString();
    t.totalLength = toLongLong(obj.value(QStringLiteral("totalLength")));
    t.completedLength = toLongLong(obj.value(QStringLiteral("completedLength")));
    t.uploadLength = toLongLong(obj.value(QStringLiteral("uploadLength")));
    t.downloadSpeed = toLongLong(obj.value(QStringLiteral("downloadSpeed")));
    t.uploadSpeed = toLongLong(obj.value(QStringLiteral("uploadSpeed")));
    t.connections = obj.value(QStringLiteral("connections")).toString().toInt();
    t.numSeeders = obj.value(QStringLiteral("numSeeders")).toString().toInt();
    t.seeder = obj.value(QStringLiteral("seeder")).toString() == QLatin1String("true") ? 1 : 0;
    t.pieceLength = obj.value(QStringLiteral("pieceLength")).toString().toInt();
    t.pieceCount = obj.value(QStringLiteral("numPieces")).toString().toInt();
    t.errorCode = obj.value(QStringLiteral("errorCode")).toString();
    t.errorMessage = obj.value(QStringLiteral("errorMessage")).toString();
    t.infoHash = obj.value(QStringLiteral("infoHash")).toString();
    t.belongsTo = obj.value(QStringLiteral("belongsTo")).toString();
    t.followedBy = obj.value(QStringLiteral("followedBy")).toString();
    t.following = obj.value(QStringLiteral("following")).toString();
    t.verifiedBytes = toLongLong(obj.value(QStringLiteral("verifiedLength")));
    t.verifyPending = obj.value(QStringLiteral("verifyIntegrityPending")).toString() == QLatin1String("true");

    const qint64 createdRaw = toLongLong(obj.value(QStringLiteral("_created")));
    t.createdMs = createdRaw;

    // bittorrent sub-object
    const QJsonObject bt = obj.value(QStringLiteral("bittorrent")).toObject();
    if (!bt.isEmpty()) {
        t.isTorrent = true;
        const QJsonObject info = bt.value(QStringLiteral("info")).toObject();
        if (!info.isEmpty()) {
            t.hasMetadata = true;
            t.comment = QString::fromUtf8(QJsonDocument(info).toJson(QJsonDocument::Compact));
            t.fileName = info.value(QStringLiteral("name")).toString();
        }
        // Rebuild the announce list so the tracker tab has data.
        const QJsonArray announce = bt.value(QStringLiteral("announceList")).toArray();
        for (const QJsonValue &tierValue : announce) {
            const QJsonArray tier = tierValue.toArray();
            for (const QJsonValue &url : tier) {
                const QString u = url.toString();
                if (!u.isEmpty() && !t.trackerUrls.contains(u))
                    t.trackerUrls.append(u);
            }
        }
    } else if (!t.belongsTo.isEmpty()) {
        t.isMetalink = true;
    }

    // files
    const QJsonArray files = obj.value(QStringLiteral("files")).toArray();
    for (const QJsonValue &fv : files) {
        const QJsonObject f = fv.toObject();
        const QString path = f.value(QStringLiteral("path")).toString();
        QVariantMap file;
        file[QStringLiteral("index")] = f.value(QStringLiteral("index")).toString().toInt();
        file[QStringLiteral("path")] = path;
        file[QStringLiteral("name")] = QFileInfo(path).fileName();
        file[QStringLiteral("length")] = double(toLongLong(f.value(QStringLiteral("length"))));
        file[QStringLiteral("completedLength")] =
            double(toLongLong(f.value(QStringLiteral("completedLength"))));
        file[QStringLiteral("selected")] =
            f.value(QStringLiteral("selected")).toString() != QLatin1String("false");

        QVariantList uris;
        const QJsonArray fileUris = f.value(QStringLiteral("uris")).toArray();
        for (const QJsonValue &uv : fileUris) {
            const QJsonObject u = uv.toObject();
            QVariantMap uri;
            uri[QStringLiteral("uri")] = u.value(QStringLiteral("uri")).toString();
            uri[QStringLiteral("status")] = u.value(QStringLiteral("status")).toString();
            uris.append(uri);
            if (t.uris.isEmpty())
                t.uris.append(u.value(QStringLiteral("uri")).toString());
        }
        file[QStringLiteral("uris")] = uris;
        t.files.append(file);

        if (t.fileName.isEmpty() && !path.isEmpty())
            t.fileName = QFileInfo(path).fileName();
        if (t.uri.isEmpty() && !uris.isEmpty())
            t.uri = uris.first().toMap().value(QStringLiteral("uri")).toString();
    }

    if (t.fileName.isEmpty()) {
        if (!t.uri.isEmpty()) {
            t.fileName = QUrl(t.uri).fileName();
            if (t.fileName.isEmpty())
                t.fileName = t.uri;
        } else {
            t.fileName = t.gid;
        }
    }

    if (t.totalLength > 0)
        t.progress = int((t.completedLength * 100) / t.totalLength);
    else
        t.progress = (t.status == QLatin1String("complete")) ? 100 : 0;
    if (t.progress > 100)
        t.progress = 100;

    // Average speed over the task's whole observed lifetime. Sampling the
    // instantaneous speed (as an earlier version did) reports the speed at the
    // moment of the last poll, which is meaningless for a finished task - a
    // 4 MiB file that took 30 s would claim 4 MB/s.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_taskMap.contains(t.gid)) {
        const Task &old = m_taskMap.value(t.gid);
        t.firstSeenMs = old.firstSeenMs > 0 ? old.firstSeenMs : nowMs;
        // A restart of the same gid resets the accounting.
        t.baselineBytes = (t.completedLength < old.completedLength) ? t.completedLength
                                                                   : old.baselineBytes;
    } else {
        t.firstSeenMs = nowMs;
        t.baselineBytes = t.completedLength;
    }
    const qint64 transferred = t.completedLength - t.baselineBytes;
    const qint64 observedMs = nowMs - t.firstSeenMs;
    if (transferred > 0 && observedMs >= 500) {
        // Enough samples for a meaningful lifetime average.
        t.avgSpeed = qint64(double(transferred) * 1000.0 / double(observedMs));
    } else if (t.status == QLatin1String("active")) {
        // Still running but no usable window yet: report the current rate.
        t.avgSpeed = t.downloadSpeed;
    } else if (m_taskMap.contains(t.gid) && m_taskMap.value(t.gid).avgSpeed > 0) {
        // Finished between two polls: keep whatever was measured before.
        t.avgSpeed = m_taskMap.value(t.gid).avgSpeed;
    } else if (t.completedLength > 0) {
        // Finished faster than a single poll interval. Dividing the payload by a
        // window of a few hundred milliseconds produces an absurd number (a
        // 12 MB file "downloaded at 11 GB/s"), so only report a figure once the
        // observed window is long enough to mean something; otherwise say
        // nothing rather than something wrong.
        if (observedMs >= 500) {
            t.avgSpeed = qint64(double(t.completedLength) * 1000.0 / double(observedMs));
        } else if (m_taskMap.contains(t.gid)) {
            t.avgSpeed = m_taskMap.value(t.gid).avgSpeed;
        } else {
            t.avgSpeed = 0;
        }
    } else {
        t.avgSpeed = 0;
    }

    t.elapsedMs = observedMs;

    t.etaSeconds = (t.downloadSpeed > 0 && t.totalLength > t.completedLength)
                       ? qint64((t.totalLength - t.completedLength) / t.downloadSpeed)
                       : -1;

    return t;
}

void Aria2Manager::handleStatusTransition(const Task &task, const QString &previous)
{
    const bool firstSight = previous.isEmpty();

    if (firstSight) {
        // A task can be discovered already finished: it may have completed
        // between two polls, or it may have been restored from the session file
        // as a stopped result. Both still belong in the history.
        if (task.status == QLatin1String("complete")) {
            emit taskCompleted(task.gid, task.fileName);
            emit taskAdded(task.gid);
            recordHistory(task, QStringLiteral("completed"));
            return;
        }
        if (task.status == QLatin1String("error")) {
            const QString message = task.errorMessage.isEmpty()
                                        ? aria2ErrorMessage(task.errorCode)
                                        : task.errorMessage;
            emit taskFailed(task.gid, task.fileName, message);
            emit taskAdded(task.gid);
            recordHistory(task, QStringLiteral("error"));
            return;
        }
        if (task.status == QLatin1String("active"))
            emit taskStarted(task.gid, task.fileName);
        emit taskAdded(task.gid);
        return;
    }

    if (task.status == QLatin1String("complete")) {
        emit taskCompleted(task.gid, task.fileName);
        if (m_settings->enableCompleteNotification())
            emit notification(tr("下载完成"), task.fileName, false);
        recordHistory(task, QStringLiteral("completed"));
        if (m_bridge) {
            m_bridge->wsBroadcast(QJsonObject{
                {QStringLiteral("event"), QStringLiteral("task-completed")},
                {QStringLiteral("gid"), task.gid},
                {QStringLiteral("name"), task.fileName},
                {QStringLiteral("size"), double(task.totalLength)},
            });
        }
    } else if (task.status == QLatin1String("error")) {
        const QString message = task.errorMessage.isEmpty()
                                    ? aria2ErrorMessage(task.errorCode)
                                    : task.errorMessage;
        emit taskFailed(task.gid, task.fileName, message);
        if (m_settings->enableErrorNotification())
            emit notification(tr("下载失败"), task.fileName + QStringLiteral("\n") + message, true);
        recordHistory(task, QStringLiteral("error"));
    } else if (task.status == QLatin1String("active")) {
        emit taskStarted(task.gid, task.fileName);
        if (m_settings->notifyOnStart())
            emit notification(tr("下载已开始"), task.fileName, false);
    }

    // Magnet links gain their metadata asynchronously; announce it once.
    if (task.isTorrent && !m_metadataSeen.contains(task.gid)
        && !task.fileName.isEmpty() && !task.files.isEmpty()) {
        m_metadataSeen.insert(task.gid);
        emit magnetMetadataReady(task.gid, task.fileName);
    }
}

void Aria2Manager::recordHistory(const Task &task, const QString &action)
{
    const QString key = task.gid + QLatin1Char('/') + action;
    if (m_historyRecorded.contains(key) && action != QLatin1String("completed"))
        return;
    m_historyRecorded.insert(key);

    QVariantMap entry;
    entry[QStringLiteral("gid")] = task.gid;
    entry[QStringLiteral("name")] = task.fileName;
    entry[QStringLiteral("uri")] = task.uri;
    entry[QStringLiteral("dir")] = task.dir;
    entry[QStringLiteral("total_length")] = double(task.totalLength);
    entry[QStringLiteral("completed")] = double(task.completedLength);
    entry[QStringLiteral("uploaded")] = double(task.uploadLength);
    entry[QStringLiteral("status")] = task.status;
    entry[QStringLiteral("action")] = action;
    entry[QStringLiteral("error_code")] = task.errorCode;
    entry[QStringLiteral("error_message")] = task.errorMessage;
    entry[QStringLiteral("info_hash")] = task.infoHash;
    entry[QStringLiteral("is_torrent")] = task.isTorrent;
    entry[QStringLiteral("avg_speed")] = double(task.avgSpeed);
    entry[QStringLiteral("elapsed")] = double(task.elapsedMs);
    entry[QStringLiteral("files")] = task.files.size();

    m_history->record(entry);
    if (m_settings->historyKeepEntries() > 0)
        m_history->prune(m_settings->historyKeepEntries());

    if (m_settings->enableEngineLog()) {
        appendEngineLog(QStringLiteral("history: %1 %2 (%3) -> %4")
                            .arg(action, task.fileName, task.gid,
                                 m_history->isReady() ? m_history->databasePath()
                                                      : QStringLiteral("sqlite unavailable")),
                        false);
    }
}

// ============================================================================
//  List rebuilding
// ============================================================================

void Aria2Manager::rebuildLists()
{
    // Keep the original task map order stable but sort buckets sensibly.
    QList<Task> all = m_taskMap.values();

    std::sort(all.begin(), all.end(), [](const Task &a, const Task &b) {
        auto rank = [](const QString &s) {
            if (s == QLatin1String("active")) return 0;
            if (s == QLatin1String("waiting")) return 1;
            if (s == QLatin1String("paused")) return 2;
            if (s == QLatin1String("error")) return 3;
            if (s == QLatin1String("complete")) return 4;
            return 5;
        };
        const int ra = rank(a.status);
        const int rb = rank(b.status);
        if (ra != rb)
            return ra < rb;
        return a.gid < b.gid;
    });

    // A fingerprint lets us skip the rebuild (and the QML model reset) while
    // nothing actually moved. Polling runs every second, so this matters.
    QString signature;
    signature.reserve(all.size() * 64);
    for (const Task &t : all) {
        signature += t.gid;
        signature += t.status;
        signature += QString::number(t.completedLength);
        signature += QString::number(t.downloadSpeed);
        signature += QString::number(t.uploadSpeed);
        signature += QString::number(t.uploadLength);
        signature += QString::number(t.connections);
        signature += QString::number(t.numSeeders);
        signature += QString::number(t.files.size());
        signature += QString::number(t.progress);
        signature += t.errorCode;
        signature += t.fileName;
        signature += QLatin1Char('|');
    }
    if (signature == m_modelSignature)
        return;
    m_modelSignature = signature;

    m_taskList.clear();
    m_activeList.clear();
    m_waitingList.clear();
    m_stoppedList.clear();

    auto toVariant = [](const Task &t) {
        QVariantMap m;
        m[QStringLiteral("gid")] = t.gid;
        m[QStringLiteral("status")] = t.status;
        m[QStringLiteral("fileName")] = t.fileName;
        m[QStringLiteral("dir")] = t.dir;
        m[QStringLiteral("uri")] = t.uri;
        m[QStringLiteral("uris")] = t.uris;
        m[QStringLiteral("totalLength")] = double(t.totalLength);
        m[QStringLiteral("completedLength")] = double(t.completedLength);
        m[QStringLiteral("uploadLength")] = double(t.uploadLength);
        m[QStringLiteral("downloadSpeed")] = double(t.downloadSpeed);
        m[QStringLiteral("uploadSpeed")] = double(t.uploadSpeed);
        m[QStringLiteral("avgSpeed")] = double(t.avgSpeed);
        m[QStringLiteral("progress")] = t.progress;
        m[QStringLiteral("eta")] = double(t.etaSeconds);
        m[QStringLiteral("connections")] = t.connections;
        m[QStringLiteral("numSeeders")] = t.numSeeders;
        m[QStringLiteral("seeder")] = t.seeder == 1;
        m[QStringLiteral("pieceLength")] = t.pieceLength;
        m[QStringLiteral("pieceCount")] = t.pieceCount;
        m[QStringLiteral("errorCode")] = t.errorCode;
        m[QStringLiteral("errorMessage")] = t.errorMessage.isEmpty()
                                                ? Aria2Manager::aria2ErrorMessage(t.errorCode)
                                                : t.errorMessage;
        m[QStringLiteral("infoHash")] = t.infoHash;
        m[QStringLiteral("isTorrent")] = t.isTorrent;
        m[QStringLiteral("isMetalink")] = t.isMetalink;
        m[QStringLiteral("hasMetadata")] = t.hasMetadata;
        m[QStringLiteral("verifyPending")] = t.verifyPending;
        m[QStringLiteral("verifiedBytes")] = double(t.verifiedBytes);
        m[QStringLiteral("fileCount")] = t.files.size();
        m[QStringLiteral("files")] = t.files;
        m[QStringLiteral("trackers")] = t.trackerUrls;
        m[QStringLiteral("belongsTo")] = t.belongsTo;
        m[QStringLiteral("followedBy")] = t.followedBy;
        m[QStringLiteral("following")] = t.following;
        return m;
    };

    for (const Task &t : all) {
        const QVariantMap vm = toVariant(t);
        m_taskList.append(vm);
        if (t.status == QLatin1String("active"))
            m_activeList.append(vm);
        else if (t.status == QLatin1String("waiting") || t.status == QLatin1String("paused"))
            m_waitingList.append(vm);
        else
            m_stoppedList.append(vm);
    }

    // The view model is derived from m_taskList, so it has to be rebuilt here as
    // well - setFilter()/setSearchText() only cover the case where the filter
    // itself changes. Without this the filtered list stays empty forever and the
    // UI reports "no downloads" while the statistics show the real task count.
    rebuildFiltered();

    emit tasksChanged();
}

void Aria2Manager::rebuildFiltered()
{
    m_filteredList.clear();
    const QString needle = m_searchText.trimmed().toLower();

    for (const QVariant &v : m_taskList) {
        const QVariantMap t = v.toMap();
        const QString status = t.value(QStringLiteral("status")).toString();

        bool visible = true;
        if (m_filter == QLatin1String("active"))
            visible = (status == QLatin1String("active"));
        else if (m_filter == QLatin1String("waiting"))
            visible = (status == QLatin1String("waiting") || status == QLatin1String("paused"));
        else if (m_filter == QLatin1String("complete"))
            visible = (status == QLatin1String("complete"));
        else if (m_filter == QLatin1String("error"))
            visible = (status == QLatin1String("error"));
        else if (m_filter == QLatin1String("bt"))
            visible = t.value(QStringLiteral("isTorrent")).toBool();

        if (visible && !needle.isEmpty()) {
            visible = t.value(QStringLiteral("fileName")).toString().toLower().contains(needle)
                || t.value(QStringLiteral("uri")).toString().toLower().contains(needle);
        }
        if (visible)
            m_filteredList.append(v);
    }
    emit viewChanged();
}

void Aria2Manager::setFilter(const QString &f)
{
    if (m_filter == f)
        return;
    m_filter = f;
    rebuildFiltered();
}

void Aria2Manager::setSearchText(const QString &t)
{
    if (m_searchText == t)
        return;
    m_searchText = t;
    rebuildFiltered();
}

void Aria2Manager::setPollInterval(int ms)
{
    ms = qBound(200, ms, 10000);
    if (m_pollInterval == ms)
        return;
    m_pollInterval = ms;
    if (m_pollTimer)
        m_pollTimer->setInterval(ms);
    emit pollIntervalChanged();
}

void Aria2Manager::refreshNow()
{
    m_pollInFlight = false;
    poll();
}

// ============================================================================
//  Adding downloads
// ============================================================================

void Aria2Manager::addUri(const QString &urls, const QVariantMap &options)
{
    QStringList list;
    const QStringList raw = urls.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : raw) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            list << trimmed;
    }
    if (list.isEmpty()) {
        emit toast(tr("未提供链接。"), true);
        return;
    }

    // Nothing to send it to yet: the engine is starting up, or a restart is in
    // progress. Queue it and say so - dropping it with "cannot add" would lose a
    // download the user typed, and the URL often arrives before the engine is
    // listening (the command line hands it over during startup).
    if (!m_client->isConnected()) {
        for (const QString &u : list) {
            if (m_pendingAdds.size() >= 100)
                break;
            m_pendingAdds.append(qMakePair(u, options));
        }
        emit toast(tr("引擎尚未就绪，已排队 %1 个任务，连接后自动开始。").arg(list.size()), false);
        return;
    }

    // Magnet links and local .torrent files take dedicated code paths.
    QStringList httpUris;
    for (const QString &u : list) {
        if (u.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)) {
            addMagnet(u, options);
        } else if (u.endsWith(QLatin1String(".torrent"), Qt::CaseInsensitive) && QFileInfo::exists(u)) {
            addTorrentFile(u, options);
        } else if (u.endsWith(QLatin1String(".metalink"), Qt::CaseInsensitive) && QFileInfo::exists(u)) {
            addMetalinkFile(u, options);
        } else {
            httpUris << u;
        }
    }
    if (httpUris.isEmpty())
        return;

    for (const QString &u : httpUris) {
        QVariantMap taskOptions = options;
        // Asking for something that was already downloaded once means "another
        // copy": aria2 finds the finished file, and its control file, and reports
        // the new task as complete without transferring a byte. A different
        // output name is what makes it download again.
        if (!taskOptions.contains(QStringLiteral("out")) && m_history && m_history->hasCompletedUri(u)) {
            const QString dir = taskOptions.value(QStringLiteral("dir"),
                                                  m_settings->downloadDir()).toString();
            const QString name = uniqueOutputName(u, dir);
            if (!name.isEmpty())
                taskOptions.insert(QStringLiteral("out"), name);
        }
        m_client->addUri({u}, taskOptions, -1, [this, u](const QJsonValue &result, bool isError, const QString &err) {
            if (isError) {
                emit toast(tr("无法添加 %1：%2").arg(u, err), true);
                return;
            }
            const QString gid = result.toString();
            emit toast(tr("已添加：%1").arg(u), false);
            Q_UNUSED(gid)
            refreshNow();
        });
    }
}

void Aria2Manager::addFromText(const QString &text, const QVariantMap &options)
{
    addUri(text, options);
}

void Aria2Manager::addTorrentFile(const QString &filePath, const QVariantMap &options)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit toast(tr("无法读取种子文件：%1").arg(filePath), true);
        return;
    }
    const QByteArray data = file.readAll();
    file.close();

    QStringList webSeeds;
    if (options.contains(QStringLiteral("webSeeds")))
        webSeeds = options.value(QStringLiteral("webSeeds")).toStringList();
    QVariantMap opts = options;
    opts.remove(QStringLiteral("webSeeds"));

    m_client->addTorrent(data, webSeeds, opts, -1,
                         [this, filePath](const QJsonValue &result, bool isError, const QString &err) {
                             if (isError) {
                                 emit toast(tr("无法添加种子：%1").arg(err), true);
                                 return;
                             }
                             emit toast(tr("已添加种子：%1").arg(QFileInfo(filePath).fileName()), false);
                             Q_UNUSED(result)
                             refreshNow();
                         });
}

void Aria2Manager::addTorrentData(const QByteArray &data, const QVariantMap &options)
{
    m_client->addTorrent(data, QStringList(), options, -1,
                         [this](const QJsonValue &, bool isError, const QString &err) {
                             if (isError)
                                 emit toast(tr("无法添加种子：%1").arg(err), true);
                             refreshNow();
                         });
}

void Aria2Manager::addMetalinkFile(const QString &filePath, const QVariantMap &options)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit toast(tr("无法读取 Metalink 文件：%1").arg(filePath), true);
        return;
    }
    const QByteArray data = file.readAll();
    file.close();
    m_client->addMetalink(data, options, -1, [this](const QJsonValue &, bool isError, const QString &err) {
        if (isError)
            emit toast(tr("无法添加 Metalink：%1").arg(err), true);
        refreshNow();
    });
}

void Aria2Manager::addMagnet(const QString &magnet, const QVariantMap &options)
{
    m_client->addUri({magnet}, options, -1, [this](const QJsonValue &, bool isError, const QString &err) {
        if (isError) {
            emit toast(tr("无法添加磁力链接：%1").arg(err), true);
            return;
        }
        refreshNow();
    });
}

// ============================================================================
//  Task control
// ============================================================================

void Aria2Manager::pauseTask(const QString &gid)
{
    callWithToast(QStringLiteral("aria2.pause"), QJsonArray{gid}, QString());
}

void Aria2Manager::resumeTask(const QString &gid)
{
    callWithToast(QStringLiteral("aria2.unpause"), QJsonArray{gid}, QString());
}

void Aria2Manager::forcePauseTask(const QString &gid)
{
    callWithToast(QStringLiteral("aria2.forcePause"), QJsonArray{gid}, QString());
}

void Aria2Manager::togglePauseTask(const QString &gid)
{
    const Task *t = findTask(gid);
    if (!t)
        return;
    if (t->status == QLatin1String("active"))
        pauseTask(gid);
    else if (t->status == QLatin1String("paused") || t->status == QLatin1String("waiting"))
        resumeTask(gid);
}

void Aria2Manager::pauseAll()
{
    m_client->pauseAll([this](const QJsonValue &, bool isError, const QString &err) {
        if (isError) {
            emit toast(err, true);
            return;
        }
        m_paused = true;
        emit globalPausedChanged();
        emit toast(tr("已暂停全部下载。"), false);
        refreshNow();
    });
}

void Aria2Manager::resumeAll()
{
    m_client->unpauseAll([this](const QJsonValue &, bool isError, const QString &err) {
        if (isError) {
            emit toast(err, true);
            return;
        }
        m_paused = false;
        emit globalPausedChanged();
        emit toast(tr("已开始全部下载。"), false);
        refreshNow();
    });
}

void Aria2Manager::removeTask(const QString &gid, int mode)
{
    Task *t = findTask(gid);
    if (!t)
        return;

    const QStringList paths = [&]() {
        QStringList p;
        for (const QVariant &fv : t->files) {
            const QString path = fv.toMap().value(QStringLiteral("path")).toString();
            if (!path.isEmpty())
                p << path;
        }
        return p;
    }();

    const bool finished = (t->status == QLatin1String("complete") || t->status == QLatin1String("error"));
    const Task snapshot = *t;

    // For a live task aria2 must forget it first; force-remove so that a paused
    // or in-flight download is dropped immediately.
    auto afterRemove = [this, gid, mode, paths, snapshot]() {
        // Remember that this gid is gone for good: aria2 keeps answering with a
        // stopped result until removeDownloadResult has been processed, and the
        // next poll would otherwise put the row the user just deleted straight
        // back into the list.
        m_dismissed.insert(gid);
        m_client->removeDownloadResult(gid, [](const QJsonValue &, bool, const QString &) {});
        if (mode == 1) {
            for (const QString &path : paths) {
                if (path.isEmpty())
                    continue;
                QFileInfo fi(path);
                if (fi.exists() && fi.isFile()) {
                    if (!QFile::remove(path))
                        qWarning() << "Could not delete" << path;
                }
                // aria2 keeps a .aria2 control file next to each download.
                const QString control = path + QStringLiteral(".aria2");
                if (QFileInfo::exists(control))
                    QFile::remove(control);
            }
            // Remove now-empty parent directories of multi-file torrents.
            QSet<QString> dirs;
            for (const QString &path : paths)
                dirs.insert(QFileInfo(path).absolutePath());
            for (const QString &dir : dirs) {
                QDir d(dir);
                if (d.exists() && d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
                    d.rmdir(dir);
            }
        }
        recordHistory(snapshot, QStringLiteral("removed"));
        m_taskMap.remove(gid);
        m_previousStatus.remove(gid);
        m_metadataSeen.remove(gid);
        m_modelSignature.clear();
        if (m_detailGid == gid)
            setDetailGid(QString());
        rebuildLists();
        rebuildFiltered();
        refreshNow();
    };

    if (finished) {
        afterRemove();
        return;
    }

    m_client->forceRemove(gid, [this, afterRemove](const QJsonValue &, bool isError, const QString &err) {
        if (isError)
            emit toast(err, true);
        afterRemove();
    });
}

void Aria2Manager::removeCompleted()
{
    m_client->purgeDownloadResult([this](const QJsonValue &, bool isError, const QString &err) {
        if (isError) {
            emit toast(err, true);
            return;
        }
        emit toast(tr("已清除完成记录。"), false);
        refreshNow();
    });
}

void Aria2Manager::purgeResults()
{
    removeCompleted();
}

void Aria2Manager::retryTask(const QString &gid)
{
    Task *t = findTask(gid);
    if (!t)
        return;

    QVariantMap options;
    QString uri = t->uri;
    if (uri.isEmpty() && !t->uris.isEmpty())
        uri = t->uris.first();

    if (t->isTorrent || uri.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)) {
        // Re-add from the magnet or from the saved metadata if we have any.
        const QString source = uri.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)
                                   ? uri
                                   : QString();
        if (source.isEmpty()) {
            emit toast(tr("无法自动重试该种子，请重新添加种子文件或磁力链接。"), true);
            return;
        }
        if (!t->dir.isEmpty())
            options.insert(QStringLiteral("dir"), t->dir);
        removeTask(gid, 0);
        addMagnet(source, options);
        return;
    }

    if (uri.isEmpty()) {
        emit toast(tr("该任务没有可用于重试的源地址。"), true);
        return;
    }

    if (!t->dir.isEmpty())
        options.insert(QStringLiteral("dir"), t->dir);
    const bool finished = (t->status == QLatin1String("complete") || t->status == QLatin1String("error"));
    if (finished)
        m_client->removeDownloadResult(gid, [](const QJsonValue &, bool, const QString &) {});
    else
        m_client->forceRemove(gid, [](const QJsonValue &, bool, const QString &) {});

    m_taskMap.remove(gid);
    m_previousStatus.remove(gid);
    rebuildLists();
    addUri(uri, options);
}

void Aria2Manager::changePosition(const QString &gid, int pos, int how)
{
    m_client->changePosition(gid, pos, how, [this](const QJsonValue &result, bool isError, const QString &err) {
        if (isError) {
            emit toast(err, true);
            return;
        }
        if (result.toInt() >= 0)
            refreshNow();
    });
}

void Aria2Manager::moveUp(const QString &gid)
{
    changePosition(gid, -1, 1);
}

void Aria2Manager::moveDown(const QString &gid)
{
    changePosition(gid, 1, 1);
}

void Aria2Manager::moveTop(const QString &gid)
{
    changePosition(gid, 0, 0);
}

void Aria2Manager::callWithToast(const QString &method, const QJsonArray &params, const QString &action)
{
    m_client->call(method, params,
                   [this, action](const QJsonValue &, bool isError, const QString &err) {
                       if (isError) {
                           emit toast(err, true);
                           return;
                       }
                       if (!action.isEmpty())
                           emit toast(action, false);
                       refreshNow();
                   });
}

// ============================================================================
//  Files / folders
// ============================================================================

QString Aria2Manager::firstPath(const Task &task) const
{
    for (const QVariant &fv : task.files) {
        const QString path = fv.toMap().value(QStringLiteral("path")).toString();
        if (!path.isEmpty())
            return path;
    }
    return {};
}

void Aria2Manager::openFile(const QString &gid)
{
    const Task *t = findTask(gid);
    if (!t)
        return;
    const QString path = firstPath(*t);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        emit toast(tr("尚未找到文件。"), true);
        return;
    }
    openPath(path);
}

void Aria2Manager::openFolder(const QString &gid)
{
    const Task *t = findTask(gid);
    if (!t)
        return;
    const QString path = firstPath(*t);
    QString dir = path.isEmpty() ? t->dir : QFileInfo(path).absolutePath();
    if (dir.isEmpty())
        dir = m_settings->downloadDir();
    openPath(dir);
}

void Aria2Manager::openPath(const QString &path)
{
    if (path.isEmpty())
        return;
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        emit toast(tr("无法打开 %1").arg(path), true);
}

void Aria2Manager::revealFile(const QString &path)
{
    if (path.isEmpty())
        return;
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(path);
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,") + native});
#else
    openPath(QFileInfo(path).absolutePath());
#endif
}

void Aria2Manager::copyToClipboard(const QString &text) const
{
    if (QClipboard *cb = QGuiApplication::clipboard())
        cb->setText(text);
}

QString Aria2Manager::clipboardText() const
{
    if (QClipboard *cb = QGuiApplication::clipboard())
        return cb->text().trimmed();
    return {};
}

QString Aria2Manager::detectClipboardUri() const
{
    const QString text = clipboardText();
    if (text.isEmpty() || text.contains(QLatin1Char('\n')))
        return {};
    static const QRegularExpression re(
        QStringLiteral("^(https?|ftp|magnet|thunder|ed2k):"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch() ? text : QString();
}

void Aria2Manager::onClipboardTimer()
{
    if (!m_settings->clipboardMonitor())
        return;
    const QString uri = detectClipboardUri();
    if (uri.isEmpty() || uri == m_lastClipboardUri)
        return;
    m_lastClipboardUri = uri;
    emit requestAddDownloadDialog(uri);
}

// ============================================================================
//  Detail / inspection
// ============================================================================

void Aria2Manager::setDetailGid(const QString &gid)
{
    if (m_detailGid == gid)
        return;
    m_detailGid = gid;
    emit detailGidChanged();
    if (!gid.isEmpty())
        fetchTaskDetail(gid);
    else {
        m_taskDetail.clear();
        emit taskDetailChanged();
    }
}

void Aria2Manager::fetchTaskDetail(const QString &gid)
{
    m_client->tellStatus(gid, statusKeys(), [this, gid](const QJsonValue &result, bool isError,
                                                        const QString &) {
        if (isError || gid != m_detailGid)
            return;
        const QJsonObject obj = result.toObject();

        Task live = parseTask(obj);
        if (m_taskMap.contains(gid)) {
            Task &stored = m_taskMap[gid];
            stored.peers = stored.peers; // peers/servers fetched separately
            live.peers = stored.peers;
            live.servers = stored.servers;
            live.optionsMap = stored.optionsMap;
            m_taskMap[gid] = live;
        }

        QVariantMap detail;
        detail[QStringLiteral("gid")] = live.gid;
        detail[QStringLiteral("status")] = live.status;
        detail[QStringLiteral("fileName")] = live.fileName;
        detail[QStringLiteral("dir")] = live.dir;
        detail[QStringLiteral("uri")] = live.uri;
        detail[QStringLiteral("uris")] = live.uris;
        detail[QStringLiteral("totalLength")] = double(live.totalLength);
        detail[QStringLiteral("completedLength")] = double(live.completedLength);
        detail[QStringLiteral("uploadLength")] = double(live.uploadLength);
        detail[QStringLiteral("downloadSpeed")] = double(live.downloadSpeed);
        detail[QStringLiteral("uploadSpeed")] = double(live.uploadSpeed);
        detail[QStringLiteral("avgSpeed")] = double(live.avgSpeed);
        detail[QStringLiteral("progress")] = live.progress;
        detail[QStringLiteral("eta")] = double(live.etaSeconds);
        detail[QStringLiteral("connections")] = live.connections;
        detail[QStringLiteral("numSeeders")] = live.numSeeders;
        detail[QStringLiteral("seeder")] = live.seeder == 1;
        detail[QStringLiteral("pieceLength")] = live.pieceLength;
        detail[QStringLiteral("pieceCount")] = live.pieceCount;
        detail[QStringLiteral("infoHash")] = live.infoHash;
        detail[QStringLiteral("errorCode")] = live.errorCode;
        detail[QStringLiteral("errorMessage")] = live.errorMessage.isEmpty()
                                                     ? aria2ErrorMessage(live.errorCode)
                                                     : live.errorMessage;
        detail[QStringLiteral("isTorrent")] = live.isTorrent;
        detail[QStringLiteral("hasMetadata")] = live.hasMetadata;
        detail[QStringLiteral("verifyPending")] = live.verifyPending;
        detail[QStringLiteral("verifiedBytes")] = double(live.verifiedBytes);
        detail[QStringLiteral("files")] = live.files;
        detail[QStringLiteral("peers")] = live.peers;
        detail[QStringLiteral("servers")] = live.servers;
        detail[QStringLiteral("trackers")] = live.trackerUrls;
        detail[QStringLiteral("btMetaInfo")] = live.comment;

        m_taskDetail = detail;
        emit taskDetailChanged();
    });

    fetchPeers(gid);
    fetchServers(gid);
    fetchUris(gid);
}

void Aria2Manager::fetchPeers(const QString &gid)
{
    if (gid.isEmpty())
        return;
    m_client->getPeers(gid, [this, gid](const QJsonValue &result, bool isError, const QString &) {
        if (isError || gid != m_detailGid)
            return;
        QVariantList peers;
        const QJsonArray arr = result.toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject p = v.toObject();
            QVariantMap peer;
            peer[QStringLiteral("peerId")] = p.value(QStringLiteral("peerId")).toString();
            peer[QStringLiteral("ip")] = p.value(QStringLiteral("ip")).toString();
            peer[QStringLiteral("port")] = p.value(QStringLiteral("port")).toString();
            peer[QStringLiteral("speed")] = double(toLongLong(p.value(QStringLiteral("downloadSpeed"))));
            peer[QStringLiteral("uploadSpeed")] = double(toLongLong(p.value(QStringLiteral("uploadSpeed"))));
            peer[QStringLiteral("seeder")] = p.value(QStringLiteral("seeder")).toString() == QLatin1String("true");
            peer[QStringLiteral("amChoking")] = p.value(QStringLiteral("amChoking")).toString() == QLatin1String("true");
            peer[QStringLiteral("peerChoking")] =
                p.value(QStringLiteral("peerChoking")).toString() == QLatin1String("true");
            peers.append(peer);
        }
        if (m_taskMap.contains(gid))
            m_taskMap[gid].peers = peers;
        m_taskDetail[QStringLiteral("peers")] = peers;
        emit taskDetailChanged();
    });
}

void Aria2Manager::fetchServers(const QString &gid)
{
    if (gid.isEmpty())
        return;
    m_client->getServers(gid, [this, gid](const QJsonValue &result, bool isError, const QString &) {
        if (isError || gid != m_detailGid)
            return;
        QVariantList servers;
        const QJsonArray arr = result.toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject s = v.toObject();
            QVariantMap server;
            server[QStringLiteral("index")] = s.value(QStringLiteral("index")).toString();
            server[QStringLiteral("currentUri")] = s.value(QStringLiteral("currentUri")).toString();
            server[QStringLiteral("downloadSpeed")] =
                double(toLongLong(s.value(QStringLiteral("downloadSpeed"))));
            QStringList uris;
            const QJsonArray uriArray = s.value(QStringLiteral("uri")).toArray();
            for (const QJsonValue &u : uriArray) {
                const QJsonObject uo = u.toObject();
                QVariantMap uri;
                uri[QStringLiteral("uri")] = uo.value(QStringLiteral("uri")).toString();
                uri[QStringLiteral("status")] = uo.value(QStringLiteral("status")).toString();
                uris << uri.value(QStringLiteral("uri")).toString();
            }
            server[QStringLiteral("uris")] = uris;
            servers.append(server);
        }
        if (m_taskMap.contains(gid))
            m_taskMap[gid].servers = servers;
        m_taskDetail[QStringLiteral("servers")] = servers;
        emit taskDetailChanged();
    });
}

void Aria2Manager::fetchUris(const QString &gid)
{
    if (gid.isEmpty())
        return;
    m_client->getUris(gid, [this, gid](const QJsonValue &result, bool isError, const QString &) {
        if (isError || gid != m_detailGid)
            return;
        QVariantList uris;
        const QJsonArray arr = result.toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject u = v.toObject();
            QVariantMap uri;
            uri[QStringLiteral("uri")] = u.value(QStringLiteral("uri")).toString();
            uri[QStringLiteral("status")] = u.value(QStringLiteral("status")).toString();
            uris.append(uri);
        }
        m_taskDetail[QStringLiteral("uris")] = uris;
        emit taskDetailChanged();
    });
}

void Aria2Manager::fetchFiles(const QString &gid)
{
    if (gid.isEmpty())
        return;
    m_client->getFiles(gid, [this, gid](const QJsonValue &result, bool isError, const QString &) {
        if (isError || gid != m_detailGid)
            return;
        QVariantList files;
        const QJsonArray arr = result.toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject f = v.toObject();
            const QString path = f.value(QStringLiteral("path")).toString();
            QVariantMap file;
            file[QStringLiteral("index")] = f.value(QStringLiteral("index")).toString().toInt();
            file[QStringLiteral("path")] = path;
            file[QStringLiteral("name")] = QFileInfo(path).fileName();
            file[QStringLiteral("length")] = double(toLongLong(f.value(QStringLiteral("length"))));
            file[QStringLiteral("completedLength")] =
                double(toLongLong(f.value(QStringLiteral("completedLength"))));
            file[QStringLiteral("selected")] =
                f.value(QStringLiteral("selected")).toString() != QLatin1String("false");
            files.append(file);
        }
        m_taskDetail[QStringLiteral("files")] = files;
        emit taskDetailChanged();
    });
}

void Aria2Manager::fetchBtMetaInfo(const QString &gid)
{
    if (gid.isEmpty())
        return;
    m_client->getBtMetaInfo(gid, [this, gid](const QJsonValue &result, bool isError, const QString &) {
        if (isError || gid != m_detailGid)
            return;
        m_taskDetail[QStringLiteral("btMetaRaw")] = result.toObject().toVariantMap();
        emit taskDetailChanged();
    });
}

void Aria2Manager::fetchTaskOptions(const QString &gid)
{
    if (gid.isEmpty())
        return;
    m_client->getOption(gid, [this, gid](const QJsonValue &result, bool isError, const QString &) {
        if (isError)
            return;
        const QVariantMap opts = result.toObject().toVariantMap();
        if (m_taskMap.contains(gid))
            m_taskMap[gid].optionsMap = QVariantList{opts};
        if (gid == m_detailGid) {
            m_taskDetail[QStringLiteral("options")] = opts;
            emit taskDetailChanged();
        }
    });
}

void Aria2Manager::applyTaskOptions(const QString &gid, const QVariantMap &options)
{
    m_client->changeOption(gid, options, [this](const QJsonValue &, bool isError, const QString &err) {
        if (isError) {
            emit toast(err, true);
            return;
        }
        emit toast(tr("任务选项已更新。"), false);
        refreshNow();
    });
}

void Aria2Manager::selectTaskFiles(const QString &gid, const QStringList &fileIndexes)
{
    QVariantMap options;
    options.insert(QStringLiteral("select-file"), fileIndexes.join(QLatin1Char(',')));
    applyTaskOptions(gid, options);
}

void Aria2Manager::addTrackers(const QString &gid, const QStringList &trackers)
{
    m_client->getBtMetaInfo(gid, [this, trackers](const QJsonValue &result, bool isError, const QString &) {
        if (isError)
            return;
        const QString infoHashB64 = result.toObject().value(QStringLiteral("infoHash")).toString();
        if (infoHashB64.isEmpty()) {
            emit toast(tr("尚未获取信息哈希，请等待元数据下载完成。"), true);
            return;
        }
        QByteArray hash = QByteArray::fromBase64(infoHashB64.toLatin1());
        if (hash.isEmpty())
            hash = QByteArray::fromHex(infoHashB64.toLatin1());
        m_client->addBtTracker(hash, trackers, [this](const QJsonValue &, bool isError, const QString &err) {
            if (isError) {
                emit toast(err, true);
                return;
            }
            emit toast(tr("已添加 Tracker。"), false);
        });
    });
}

void Aria2Manager::removeTracker(const QString &gid, const QString &tracker)
{
    m_client->getBtMetaInfo(gid, [this, tracker](const QJsonValue &result, bool isError, const QString &) {
        if (isError)
            return;
        const QString infoHashB64 = result.toObject().value(QStringLiteral("infoHash")).toString();
        QByteArray hash = QByteArray::fromBase64(infoHashB64.toLatin1());
        if (hash.isEmpty())
            hash = QByteArray::fromHex(infoHashB64.toLatin1());
        m_client->removeBtTracker(hash, tracker, [this](const QJsonValue &, bool isError, const QString &err) {
            if (isError) {
                emit toast(err, true);
                return;
            }
            emit toast(tr("已移除 Tracker。"), false);
        });
    });
}

// ============================================================================
//  Global options
// ============================================================================

void Aria2Manager::refreshGlobalOptions()
{
    m_client->getGlobalOption([this](const QJsonValue &result, bool isError, const QString &) {
        if (isError)
            return;
        m_globalOptions = result.toObject().toVariantMap();
        emit globalOptionsChanged();
    });
}

void Aria2Manager::applyGlobalOptions(const QVariantMap &options)
{
    if (options.isEmpty())
        return;
    m_client->changeGlobalOption(options, [this](const QJsonValue &, bool isError, const QString &err) {
        if (isError) {
            emit toast(err, true);
            return;
        }
        emit toast(tr("已应用引擎选项。"), false);
        refreshGlobalOptions();
    });
}

void Aria2Manager::applySettingsRuntime()
{
    if (!m_client->isConnected())
        return;
    const QVariantMap options = m_settings->runtimeOptions();
    if (options.isEmpty())
        return;
    m_client->changeGlobalOption(options, [this](const QJsonValue &, bool isError, const QString &err) {
        if (isError)
            appendEngineLog(tr("无法应用运行时选项：%1").arg(err), true);
        else
            refreshGlobalOptions();
    });
}

void Aria2Manager::saveSession()
{
    m_client->saveSession([this](const QJsonValue &, bool isError, const QString &err) {
        if (isError) {
            emit toast(err, true);
            return;
        }
        emit toast(tr("会话已保存。"), false);
    });
}

// ============================================================================
//  Browser bridge handlers
// ============================================================================

void Aria2Manager::onBridgeDownload(const QString &url, const QString &origin, const QVariantMap &options)
{
    if (m_settings->enableEngineLog()) {
        appendEngineLog(QStringLiteral("bridge download from %1: %2 | options=%3")
                            .arg(origin, url,
                                 QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(options))
                                                       .toJson(QJsonDocument::Compact))),
                        false);
    }
    if (url.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)) {
        addMagnet(url, options);
        return;
    }
    m_client->addUri({url}, options, -1, [this, url](const QJsonValue &, bool isError, const QString &err) {
        if (isError) {
            emit toast(tr("浏览器请求失败：%1").arg(err), true);
            return;
        }
        emit toast(tr("已从浏览器添加：%1").arg(url), false);
        refreshNow();
    });
}

void Aria2Manager::onBridgeBatch(const QStringList &urls, const QString &origin, const QVariantMap &options)
{
    if (m_settings->enableEngineLog()) {
        appendEngineLog(QStringLiteral("bridge batch from %1: %2 item(s) | options=%3")
                            .arg(origin)
                            .arg(urls.size())
                            .arg(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(options))
                                                       .toJson(QJsonDocument::Compact))),
                        false);
    }
    m_client->addUri(urls, options, -1, [this, count = urls.size()](const QJsonValue &, bool isError,
                                                                    const QString &err) {
        if (isError) {
            emit toast(tr("浏览器批量添加失败：%1").arg(err), true);
            return;
        }
        emit toast(tr("已从浏览器添加 %1 个下载任务。").arg(count), false);
        refreshNow();
    });
}

void Aria2Manager::onBridgeTorrent(const QByteArray &data, const QString &origin)
{
    Q_UNUSED(origin)
    addTorrentData(data, QVariantMap());
}

void Aria2Manager::onBridgeMagnet(const QString &magnet, const QString &origin)
{
    Q_UNUSED(origin)
    addMagnet(magnet, QVariantMap());
}

// ============================================================================
//  Scheduler
// ============================================================================

void Aria2Manager::onSchedulerTimer()
{
    if (!m_settings->schedulerEnabled()) {
        if (!m_scheduledTasks.isEmpty()) {
            m_scheduledTasks.clear();
            emit scheduledTasksChanged();
        }
        return;
    }

    const QTime now = QTime::currentTime();
    const QTime start = QTime::fromString(m_settings->scheduleStart(), QStringLiteral("HH:mm"));
    const QTime stop = QTime::fromString(m_settings->scheduleStop(), QStringLiteral("HH:mm"));
    if (!start.isValid() || !stop.isValid())
        return;

    const bool inside = (start <= stop) ? (now >= start && now < stop) : (now >= start || now < stop);

    QVariantMap entry;
    entry[QStringLiteral("time")] = now.toString(QStringLiteral("HH:mm:ss"));
    entry[QStringLiteral("window")] = m_settings->scheduleStart() + QStringLiteral(" - ")
        + m_settings->scheduleStop();
    entry[QStringLiteral("inside")] = inside;
    entry[QStringLiteral("downloadLimit")] = m_settings->scheduledDownloadLimitKB();
    entry[QStringLiteral("uploadLimit")] = m_settings->scheduledUploadLimitKB();

    QVariantList list;
    list.append(entry);
    m_scheduledTasks = list;
    emit scheduledTasksChanged();

    if (!m_client->isConnected())
        return;

    // Outside the window the transfer is throttled to 1 KiB/s (a clean "stop"
    // that still lets aria2 keep its connections) unless a limit is configured.
    const int down = inside ? m_settings->scheduledDownloadLimitKB()
                            : (m_settings->scheduledDownloadLimitKB() > 0 ? m_settings->scheduledDownloadLimitKB() : 1);
    const int up = inside ? m_settings->scheduledUploadLimitKB()
                          : (m_settings->scheduledUploadLimitKB() > 0 ? m_settings->scheduledUploadLimitKB() : 1);

    QVariantMap options;
    options[QStringLiteral("max-overall-download-limit")] =
        QString::number(down) + QLatin1Char('K');
    options[QStringLiteral("max-overall-upload-limit")] = QString::number(up) + QLatin1Char('K');
    m_client->changeGlobalOption(options, [](const QJsonValue &, bool, const QString &) {});
}

// ============================================================================
//  Helpers
// ============================================================================

QList<Aria2Manager::Task *> Aria2Manager::taskPointers()
{
    QList<Task *> out;
    for (auto it = m_taskMap.begin(); it != m_taskMap.end(); ++it)
        out.append(&it.value());
    return out;
}

Aria2Manager::Task *Aria2Manager::findTask(const QString &gid)
{
    auto it = m_taskMap.find(gid);
    return it == m_taskMap.end() ? nullptr : &it.value();
}

const Aria2Manager::Task *Aria2Manager::findTask(const QString &gid) const
{
    auto it = m_taskMap.constFind(gid);
    return it == m_taskMap.constEnd() ? nullptr : &it.value();
}

QString Aria2Manager::resolveName(const QJsonArray &files, const QString &gid) const
{
    if (!files.isEmpty()) {
        const QString path = files.first().toObject().value(QStringLiteral("path")).toString();
        if (!path.isEmpty())
            return QFileInfo(path).fileName();
    }
    return gid;
}

QVariantList Aria2Manager::completedHistory() const
{
    return m_history->fetch(QStringLiteral("all"), QString(), 500);
}

QString Aria2Manager::formatSize(double bytes)
{
    return humanSize(bytes);
}

QString Aria2Manager::formatSpeed(double bytesPerSecond)
{
    if (bytesPerSecond <= 0)
        return QStringLiteral("-");
    return humanSize(bytesPerSecond) + QStringLiteral("/s");
}

QString Aria2Manager::formatDuration(double seconds)
{
    if (seconds < 0)
        return QStringLiteral("-");
    const int total = int(seconds);
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    if (h > 0)
        return QStringLiteral("%1h %2m").arg(h).arg(m);
    if (m > 0)
        return QStringLiteral("%1m %2s").arg(m).arg(s);
    return QStringLiteral("%1s").arg(s);
}

QString Aria2Manager::formatEta(double remainingBytes, double speed)
{
    if (speed <= 0 || remainingBytes <= 0)
        return QStringLiteral("--");
    return formatDuration(remainingBytes / speed);
}

QString Aria2Manager::statusLabel(const QString &status)
{
    // Kept in sync with Theme.statusLabel() in Theme.qml.
    if (status == QLatin1String("active"))
        return tr("下载中");
    if (status == QLatin1String("waiting"))
        return tr("等待中");
    if (status == QLatin1String("paused"))
        return tr("已暂停");
    if (status == QLatin1String("complete"))
        return tr("已完成");
    if (status == QLatin1String("error"))
        return tr("失败");
    if (status == QLatin1String("removed"))
        return tr("已移除");
    return status;
}

QString Aria2Manager::aria2ErrorMessage(const QString &code)
{
    if (code.isEmpty() || code == QLatin1String("0"))
        return {};
    bool ok = false;
    const int c = code.toInt(&ok);
    if (!ok)
        return code;
    switch (c) {
    case 1: return tr("未知错误");
    case 2: return tr("超时");
    case 3: return tr("资源未找到");
    case 4: return tr("“未找到”响应过多");
    case 5: return tr("下载速度过慢");
    case 6: return tr("网络问题");
    case 7: return tr("仍有未完成的下载");
    case 8: return tr("远端服务器不支持断点续传");
    case 9: return tr("磁盘空间不足");
    case 10: return tr("分片长度与控制文件不一致");
    case 11: return tr("重复任务：该文件已在下载");
    case 12: return tr("重复任务：该种子已在下载");
    case 13: return tr("文件已存在");
    case 14: return tr("重命名失败：文件已存在");
    case 15: return tr("无法打开已存在的文件");
    case 16: return tr("无法创建新文件或删除旧文件");
    case 17: return tr("文件系统错误");
    case 18: return tr("无法创建目录");
    case 19: return tr("域名解析失败");
    case 20: return tr("无法解析 Metalink 文档");
    case 21: return tr("FTP 命令失败");
    case 22: return tr("HTTP 响应头异常");
    case 23: return tr("重定向次数过多");
    case 24: return tr("HTTP 认证失败");
    case 25: return tr("无法解析 bencode 文件");
    case 26: return tr("种子文件已损坏或缺少信息");
    case 27: return tr("磁力链接格式错误");
    case 28: return tr("选项错误或不被支持");
    case 29: return tr("远端服务器暂时无法处理该请求");
    case 30: return tr("无法解析 JSON-RPC 请求");
    case 31: return tr("保留项，已忽略");
    case 32: return tr("校验和不匹配");
    default: return tr("错误 %1").arg(c);
    }
}
