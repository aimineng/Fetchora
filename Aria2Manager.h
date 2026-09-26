#ifndef ARIA2MANAGER_H
#define ARIA2MANAGER_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>

#include "Aria2Client.h"
#include "Aria2Process.h"
#include "DownloadHistory.h"
#include "HttpServer.h"
#include "SettingsManager.h"

/**
 * Aria2Manager - the application core.
 *
 * Owns the aria2c process, the RPC client, the task model that QML binds to and
 * every user-facing operation (add / pause / resume / reorder / remove / seed /
 * inspect ...).  It exposes the whole aria2 surface, not a subset.
 */
class Aria2Manager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY tasksChanged)
    Q_PROPERTY(QVariantList activeTasks READ activeTasks NOTIFY tasksChanged)
    Q_PROPERTY(QVariantList waitingTasks READ waitingTasks NOTIFY tasksChanged)
    Q_PROPERTY(QVariantList stoppedTasks READ stoppedTasks NOTIFY tasksChanged)
    Q_PROPERTY(QVariantList filteredTasks READ filteredTasks NOTIFY viewChanged)

    Q_PROPERTY(QVariantMap statistics READ statistics NOTIFY statisticsChanged)
    Q_PROPERTY(QVariantMap sessionInfo READ sessionInfo NOTIFY sessionInfoChanged)
    Q_PROPERTY(QVariantMap globalOptions READ globalOptions NOTIFY globalOptionsChanged)
    Q_PROPERTY(QVariantMap taskDetail READ taskDetail NOTIFY taskDetailChanged)

    Q_PROPERTY(bool engineRunning READ engineRunning NOTIFY engineRunningChanged)
    Q_PROPERTY(bool engineReady READ engineReady NOTIFY engineReadyChanged)
    Q_PROPERTY(QString engineVersion READ engineVersion NOTIFY engineStateChanged)
    Q_PROPERTY(QString engineError READ engineError NOTIFY engineErrorChanged)
    Q_PROPERTY(QString engineLog READ engineLog NOTIFY engineLogChanged)
    Q_PROPERTY(QString rpcLog READ rpcLog NOTIFY rpcLogChanged)
    Q_PROPERTY(int enginePid READ enginePid NOTIFY engineRunningChanged)

    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY viewChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY viewChanged)
    Q_PROPERTY(QString detailGid READ detailGid WRITE setDetailGid NOTIFY detailGidChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY globalPausedChanged)
    Q_PROPERTY(int pollInterval READ pollInterval WRITE setPollInterval NOTIFY pollIntervalChanged)
    Q_PROPERTY(QVariantList scheduledTasks READ scheduledTasks NOTIFY scheduledTasksChanged)
    Q_PROPERTY(QVariantList completedHistory READ completedHistory NOTIFY historyChanged)
    Q_PROPERTY(int bridgeClients READ bridgeClients NOTIFY bridgeClientsChanged)
    Q_PROPERTY(bool bridgeListening READ bridgeListening NOTIFY bridgeClientsChanged)
    Q_PROPERTY(int bridgePort READ bridgePort NOTIFY bridgeClientsChanged)

public:
    explicit Aria2Manager(SettingsManager *settings, QObject *parent = nullptr);
    ~Aria2Manager() override;

    QVariantList tasks() const { return m_taskList; }
    QVariantList activeTasks() const { return m_activeList; }
    QVariantList waitingTasks() const { return m_waitingList; }
    QVariantList stoppedTasks() const { return m_stoppedList; }
    QVariantList filteredTasks() const { return m_filteredList; }
    QVariantMap statistics() const { return m_statistics; }
    QVariantMap sessionInfo() const { return m_sessionInfo; }
    QVariantMap globalOptions() const { return m_globalOptions; }
    QVariantMap taskDetail() const { return m_taskDetail; }
    QVariantList scheduledTasks() const { return m_scheduledTasks; }
    QVariantList completedHistory() const;

    bool engineRunning() const { return m_process && m_process->isRunning(); }
    bool engineReady() const { return m_client && m_client->isConnected(); }
    QString engineVersion() const { return m_client ? m_client->aria2Version() : QString(); }
    QString engineError() const { return m_engineError; }
    QString engineLog() const { return m_engineLog; }
    QString rpcLog() const { return m_rpcLog; }
    int enginePid() const { return m_process ? int(m_process->processId()) : 0; }

    QString filter() const { return m_filter; }
    void setFilter(const QString &f);
    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &t);
    QString detailGid() const { return m_detailGid; }
    void setDetailGid(const QString &gid);
    bool paused() const { return m_paused; }
    int pollInterval() const { return m_pollInterval; }
    void setPollInterval(int ms);

    DownloadHistory *history() const { return m_history; }
    HttpServer *browserBridge() const { return m_bridge; }
    /// The application settings this manager was built with. Exposed so dialogs
    /// can read and write the *same* instance instead of a second copy of the
    /// QSettings store.
    SettingsManager *settings() const { return m_settings; }
    int bridgeClients() const { return m_bridge ? m_bridge->webSocketClientCount() : 0; }
    bool bridgeListening() const { return m_bridge && m_bridge->isListening(); }
    int bridgePort() const { return m_settings ? m_settings->browserPort() : 0; }

    // ============================================================ task actions
    /// Add one or more URIs with optional per-task aria2 options.
    Q_INVOKABLE void addUri(const QString &urls, const QVariantMap &options = QVariantMap());
    /// Add from a raw text blob: URLs, magnet links, .torrent paths, one per line.
    Q_INVOKABLE void addFromText(const QString &text, const QVariantMap &options = QVariantMap());
    Q_INVOKABLE void addTorrentFile(const QString &filePath, const QVariantMap &options = QVariantMap());
    Q_INVOKABLE void addTorrentData(const QByteArray &data, const QVariantMap &options = QVariantMap());
    Q_INVOKABLE void addMetalinkFile(const QString &filePath, const QVariantMap &options = QVariantMap());
    Q_INVOKABLE void addMagnet(const QString &magnet, const QVariantMap &options = QVariantMap());

    Q_INVOKABLE void pauseTask(const QString &gid);
    Q_INVOKABLE void resumeTask(const QString &gid);
    Q_INVOKABLE void pauseAll();
    Q_INVOKABLE void resumeAll();
    Q_INVOKABLE void togglePauseTask(const QString &gid);
    Q_INVOKABLE void forcePauseTask(const QString &gid);

    /// mode: 0 = remove a finished record, 1 = remove and delete the payload.
    Q_INVOKABLE void removeTask(const QString &gid, int mode = 0);
    Q_INVOKABLE void removeCompleted();
    Q_INVOKABLE void purgeResults();

    Q_INVOKABLE void retryTask(const QString &gid);
    Q_INVOKABLE void openFile(const QString &gid);
    Q_INVOKABLE void openFolder(const QString &gid);
    Q_INVOKABLE void openPath(const QString &path);
    Q_INVOKABLE void revealFile(const QString &path);
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    Q_INVOKABLE QString clipboardText() const;

    /// Move a task inside the waiting queue. how: 0 = absolute, 1 = up, 2 = down.
    Q_INVOKABLE void changePosition(const QString &gid, int pos, int how);
    Q_INVOKABLE void moveUp(const QString &gid);
    Q_INVOKABLE void moveDown(const QString &gid);
    Q_INVOKABLE void moveTop(const QString &gid);

    // ======================================================== per-task options
    Q_INVOKABLE void fetchTaskOptions(const QString &gid);
    Q_INVOKABLE void applyTaskOptions(const QString &gid, const QVariantMap &options);
    Q_INVOKABLE void selectTaskFiles(const QString &gid, const QStringList &fileIndexes);

    // ============================================================ engine control
    Q_INVOKABLE void startEngine();
    Q_INVOKABLE void stopEngine();
    Q_INVOKABLE void restartEngine();
    Q_INVOKABLE void saveSession();
    Q_INVOKABLE void refreshNow();
    Q_INVOKABLE void refreshGlobalOptions();
    Q_INVOKABLE void applyGlobalOptions(const QVariantMap &options);
    Q_INVOKABLE void applySettingsRuntime();

    // ================================================================ clipboard
    Q_INVOKABLE QString detectClipboardUri() const;

    // ==================================================================== BT ops
    Q_INVOKABLE void fetchPeers(const QString &gid);
    Q_INVOKABLE void fetchServers(const QString &gid);
    Q_INVOKABLE void fetchUris(const QString &gid);
    Q_INVOKABLE void fetchFiles(const QString &gid);
    Q_INVOKABLE void fetchBtMetaInfo(const QString &gid);
    Q_INVOKABLE void addTrackers(const QString &gid, const QStringList &trackers);
    Q_INVOKABLE void removeTracker(const QString &gid, const QString &tracker);

    // ==================================================================== utils
    Q_INVOKABLE static QString formatSize(double bytes);
    Q_INVOKABLE static QString formatSpeed(double bytesPerSecond);
    Q_INVOKABLE static QString formatDuration(double seconds);
    Q_INVOKABLE static QString formatEta(double remainingBytes, double speed);
    Q_INVOKABLE static QString statusLabel(const QString &status);
    Q_INVOKABLE static QString aria2ErrorMessage(const QString &code);

signals:
    void tasksChanged();
    void viewChanged();
    void statisticsChanged();
    void sessionInfoChanged();
    void globalOptionsChanged();
    void taskDetailChanged();
    void detailGidChanged();
    void globalPausedChanged();
    void engineRunningChanged();
    void engineReadyChanged();
    void engineStateChanged();
    void engineErrorChanged();
    void engineLogChanged();
    void rpcLogChanged();
    void pollIntervalChanged();
    void scheduledTasksChanged();
    void historyChanged();
    void bridgeClientsChanged();

    void taskAdded(const QString &gid);
    void taskCompleted(const QString &gid, const QString &fileName);
    void taskFailed(const QString &gid, const QString &fileName, const QString &errorMessage);
    void taskStarted(const QString &gid, const QString &fileName);
    void notification(const QString &title, const QString &message, bool isError);
    void toast(const QString &message, bool isError);
    void requestAddDownloadDialog(const QString &url);
    void magnetMetadataReady(const QString &gid, const QString &name);

private slots:
    void poll();
    void onProcessLog(const QString &line, bool isError);
    void onProcessFailed(const QString &reason);
    void onProcessStateChanged();
    void onRpcLog(const QString &direction, const QString &text);
    void onClientConnectedChanged(bool connected);
    void onBridgeDownload(const QString &url, const QString &origin, const QVariantMap &options);
    void onBridgeBatch(const QStringList &urls, const QString &origin, const QVariantMap &options);
    void onBridgeTorrent(const QByteArray &data, const QString &origin);
    void onBridgeMagnet(const QString &magnet, const QString &origin);
    void onClipboardTimer();
    void onSchedulerTimer();

private:
    // -- browser bridge (WebSocket transport handled by HttpServer) ------------
    void onSocketStatus(const QString &requestId);

    /// One entry of the queue snapshot: everything a fresh engine needs to pick a
    /// task up again. aria2 resumes from the .aria2 control file it left behind,
    /// so the URI is enough - no progress has to be carried across.
    struct QueuedTask {
        QString uri;
        QString dir;
        bool paused = false;
    };

    /// What the engine is working on right now, as something we can hand back.
    QList<QueuedTask> captureQueue() const;
    /// Hand m_pendingQueue to a freshly started engine and say so.
    void restoreQueue();

    /// A file name in `dir` that nothing is using yet ("report (2).pdf"), derived
    /// from the URI; empty when the URI has no usable name. Used when a URI is
    /// asked for a second time: aria2 needs a different target to download at all,
    /// because it otherwise finds the finished file and its control file and
    /// reports the new task as complete without transferring anything.
    QString uniqueOutputName(const QString &uri, const QString &dir) const;

private:
    struct Task {
        QString gid;
        QString status;
        QString fileName;
        QString dir;
        QString uri;
        QStringList uris;
        QString errorCode;
        QString errorMessage;
        QString infoHash;
        QString comment;
        QString followedBy;
        QString following;
        QString belongsTo;
        QString verifiedLength;
        QStringList trackerUrls;

        qint64 totalLength = 0;
        qint64 completedLength = 0;
        qint64 uploadLength = 0;
        qint64 downloadSpeed = 0;
        qint64 uploadSpeed = 0;
        qint64 avgSpeed = 0;
        qint64 elapsedMs = 0;
        qint64 createdMs = 0;
        qint64 verifiedBytes = 0;
        /// Wall-clock ms when this task was first observed, and the bytes that
        /// had already been transferred at that point. Together they give a
        /// true lifetime average instead of an instantaneous sample.
        qint64 firstSeenMs = 0;
        qint64 baselineBytes = 0;

        int connections = 0;
        int numSeeders = 0;
        int pieceLength = 0;
        int pieceCount = 0;
        int seeder = 0;

        bool isTorrent = false;
        bool isMetalink = false;
        bool verifyPending = false;
        bool hasMetadata = false;

        int progress = 0;
        qint64 etaSeconds = -1;

        QVariantList files;       // { index, path, name, length, completed, selected, uris }
        QVariantList peers;       // { peerId, ip, port, speed, uploadSpeed, seeder, amChoking }
        QVariantList servers;     // { index, currentUri, downloadSpeed, uris }
        QVariantList optionsMap;  // raw option list from aria2.getOption
    };

    // ------------------------------------------------------------- internals
    void initProcess();
    void initClient();
    void initBridge();
    void initTimers();

    void applyTask(const QJsonObject &obj, const QString &bucket);
    void finishPollCycle();
    Task parseTask(const QJsonObject &obj) const;
    void rebuildLists();
    void rebuildFiltered();
    void recordHistory(const Task &task, const QString &action);
    void handleStatusTransition(const Task &task, const QString &previous);

    QString resolveName(const QJsonArray &files, const QString &gid) const;
    QString firstPath(const Task &task) const;
    void fetchTaskDetail(const QString &gid);
    QList<Task *> taskPointers();
    Task *findTask(const QString &gid);
    const Task *findTask(const QString &gid) const;

    void setEngineError(const QString &message);
    void appendEngineLog(const QString &line, bool isError);
    void appendRpcLog(const QString &direction, const QString &text);
    void callWithToast(const QString &method, const QJsonArray &params, const QString &action);

    /// Validate user-supplied switches against `aria2c --help=#all` so an
    /// unknown option produces a clear message instead of a dead engine.
    QString firstInvalidAria2Option(const QString &executable, const QStringList &args) const;

    SettingsManager *m_settings;
    Aria2Process *m_process = nullptr;
    Aria2Client *m_client = nullptr;
    HttpServer *m_bridge = nullptr;
    DownloadHistory *m_history = nullptr;

    QTimer *m_pollTimer = nullptr;
    QTimer *m_clipboardTimer = nullptr;
    QTimer *m_schedulerTimer = nullptr;
    QTimer *m_restartGuard = nullptr;

    QMap<QString, Task> m_taskMap;
    QMap<QString, QString> m_previousStatus;
    QSet<QString> m_newlySeen;
    QSet<QString> m_metadataSeen;
    /// Cheap fingerprint of the visible task state; rebuildLists() only emits
    /// when it changes, so idle polling does not thrash the QML model.
    QString m_modelSignature;
    QString m_lastDetailSignature;

    QVariantList m_taskList;
    QVariantList m_activeList;
    QVariantList m_waitingList;
    QVariantList m_stoppedList;
    QVariantList m_filteredList;
    QVariantList m_scheduledTasks;
    QVariantMap m_statistics;
    QVariantMap m_sessionInfo;
    QVariantMap m_globalOptions;
    QVariantMap m_taskDetail;

    QString m_filter = QStringLiteral("all");
    QString m_searchText;
    QString m_detailGid;
    QString m_engineError;
    QString m_engineLog;
    QString m_rpcLog;
    QString m_lastClipboardUri;

    bool m_paused = false;
    bool m_pollInFlight = false;
    int m_pendingBuckets = 0;
    int m_pollInterval = 1000;
    int m_errorStreak = 0;
    int m_restartAttempts = 0;
    bool m_shuttingDown = false;
    bool m_engineErrorNotified = false;
    /// True while we are the ones stopping the engine (restart / settings change /
    /// quit), so an exit is not reported to the user as a crash.
    bool m_stoppingEngine = false;
    /// The engine died on its own and we are bringing it back: the recovery is
    /// worth one "it is back" message.
    bool m_recoveringEngine = false;
    /// Downloads that were running when the engine went away, waiting for the
    /// replacement to be ready (see captureQueue()/restoreQueue()).
    QList<QueuedTask> m_pendingQueue;
    /// The same picture, refreshed by every poll cycle that came back complete.
    ///
    /// Taking the snapshot when the engine dies is too late: the RPC calls that
    /// were in flight fail first, and a poll cycle that saw nothing erases every
    /// known task - so by the time QProcess reports the exit there is nothing left
    /// to remember.
    QList<QueuedTask> m_queueSnapshot;
    /// Buckets of the current poll cycle that returned an error; a cycle with
    /// failures must not overwrite the snapshot above.
    int m_failedBuckets = 0;
    /// Set when a *replacement* engine has been started while m_pendingQueue is
    /// waiting, cleared once the queue has been handed over.
    ///
    /// The restore cannot hang off "the client reconnected": a restarted aria2c
    /// is listening again in well under a second, so the 4 s connection probe
    /// never observes a disconnect and never reports a reconnection either.
    bool m_restoreArmed = false;
    /// Downloads the user asked for while the engine was not answering yet -
    /// starting up, or in the middle of a restart. They are added as soon as it
    /// is back instead of failing with a toast nobody can act on.
    QList<QPair<QString, QVariantMap>> m_pendingAdds;
    /// Gids the user removed. A poll that still sees them (aria2 answers with a
    /// stopped result until removeDownloadResult is processed) must not resurrect
    /// the row.
    QSet<QString> m_dismissed;

    QSet<QString> m_historyRecorded;
};

Q_DECLARE_METATYPE(QVariantList)

#endif // ARIA2MANAGER_H
