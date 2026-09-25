#include "SettingsManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

// ============================================================================
//  Boilerplate reduction
//
//  Each setting follows the same shape: skip if unchanged, store, persist,
//  emit the individual change signal, then tell the aria2 layer that something
//  it cares about moved.
//
//  The parameter type is spelled out at every call site (QSTRING_PARAM /
//  INT_PARAM / ...) so the generated definition matches the declaration in the
//  header exactly.
// ============================================================================

#define QSTRING_PARAM const QString &
#define INT_PARAM int
#define BOOL_PARAM bool
#define DOUBLE_PARAM double

/// Persist + notify for a setting that is part of the aria2 command line.
#define IMPL_SETTING(TYPE, NAME, MEMBER, KEY, SIGNAL)          \
    void SettingsManager::set##NAME(TYPE v)                    \
    {                                                          \
        if (MEMBER == v)                                       \
            return;                                            \
        MEMBER = v;                                            \
        m_settings.setValue(KEY, serialize(MEMBER));           \
        emit SIGNAL();                                         \
        touch(KEY);                                            \
    }

/// Same, but the value can be pushed to a running aria2 without a restart.
#define IMPL_SETTING_RUNTIME(TYPE, NAME, MEMBER, KEY, SIGNAL)  \
    void SettingsManager::set##NAME(TYPE v)                    \
    {                                                          \
        if (MEMBER == v)                                       \
            return;                                            \
        MEMBER = v;                                            \
        m_settings.setValue(KEY, serialize(MEMBER));           \
        emit SIGNAL();                                         \
        touch(KEY, true);                                      \
    }

namespace {

/// QSettings round-trips QVariant fine, but floats/doubles need a nudge so
/// "1.0" does not turn into the int 1.
QVariant serialize(const QVariant &v)
{
    return v;
}

int boundedInt(int value, int lo, int hi)
{
    return qBound(lo, value, hi);
}

/// aria2 accepts a size suffix (K/M/G). Normalise user input like "512", "512k"
/// or "1M" into something aria2 understands and "" for "unlimited".
QString normalizeSize(const QString &raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty())
        return {};
    static const QRegularExpression re(QStringLiteral("^(\\d+)\\s*([kKmMgG]?)[bB]?$"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch())
        return s; // hand it to aria2 verbatim and let it complain
    const QString suffix = m.captured(2).toUpper();
    return m.captured(1) + suffix;
}

} // namespace

// ---------------------------------------------------------------------------

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("Fetchora"), QStringLiteral("Fetchora"))
{
    m_settings.setFallbacksEnabled(false);
    loadSettings();
}

// ------------------------------------------------------------- generic access

QVariant SettingsManager::value(const QString &key, const QVariant &fallback) const
{
    return m_settings.value(key, fallback);
}

void SettingsManager::setValue(const QString &key, const QVariant &v)
{
    m_settings.setValue(key, v);
    m_settings.sync();
    // The typed accessors - not the raw store - are what buildAria2Arguments()
    // and the UI read, so a write through the generic API has to flow back into
    // them. loadSettings() is a pure read and idempotent.
    loadSettings();
    bumpRevision();
    emit aria2SettingsChanged();
}

bool SettingsManager::contains(const QString &key) const
{
    return m_settings.contains(key);
}

void SettingsManager::resetToDefaults()
{
    m_settings.clear();
    m_settings.sync();
    loadSettings();
    bumpRevision();
    emit aria2SettingsChanged();
    emit runtimeOptionsChanged();
}

void SettingsManager::sync()
{
    m_settings.sync();
}

void SettingsManager::touch(const char *key, bool runtimeOnly)
{
    Q_UNUSED(key)
    if (runtimeOnly)
        emit runtimeOptionsChanged();
    bumpRevision();
    emit aria2SettingsChanged();
}

void SettingsManager::bumpRevision()
{
    ++m_settingsRevision;
    emit settingsRevisionChanged();
}

// ----------------------------------------------------------------- load/save

void SettingsManager::loadSettings()
{
    auto get = [this](const char *k, const QVariant &def) { return m_settings.value(QString::fromLatin1(k), def); };

    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);

    // ---- application -------------------------------------------------------
    m_autoStart = get("autoStart", false).toBool();
    m_startMinimized = get("startMinimized", false).toBool();
    m_minimizeToTray = get("minimizeToTray", true).toBool();
    m_closeToTray = get("closeToTray", true).toBool();
    m_downloadDir = get("downloadDir", defaultDir.isEmpty()
                                                 ? QCoreApplication::applicationDirPath() + "/downloads"
                                                 : defaultDir)
                        .toString();
    if (m_downloadDir.isEmpty())
        m_downloadDir = QCoreApplication::applicationDirPath() + "/downloads";
    m_language = get("language", "system").toString();
    m_uiFontFamily = get("uiFontFamily", "").toString();
    m_uiFontSize = boundedInt(get("uiFontSize", 14).toInt(), 11, 20);
    m_theme = get("theme", "dark").toString();
    m_accentColor = get("accentColor", "").toString();
    m_useMica = get("useMica", true).toBool();
    m_enableAnimations = get("enableAnimations", true).toBool();
    m_enableCompleteNotification = get("enableCompleteNotification", true).toBool();
    m_enableErrorNotification = get("enableErrorNotification", true).toBool();
    m_notifyOnStart = get("notifyOnStart", false).toBool();
    m_autoPasteClipboard = get("autoPasteClipboard", true).toBool();
    m_clipboardMonitor = get("clipboardMonitor", false).toBool();
    m_showTraySpeed = get("showTraySpeed", true).toBool();
    m_browserIntegration = get("browserIntegration", true).toBool();
    m_browserPort = boundedInt(get("browserPort", 8899).toInt(), 1024, 65535);
    m_confirmOnExit = get("confirmOnExit", false).toBool();
    m_showDetailsPanel = get("showDetailsPanel", true).toBool();
    m_checkForUpdates = get("checkForUpdates", true).toBool();
    m_updateIncludePrerelease = get("updateIncludePrerelease", false).toBool();
    m_lastNotifiedVersion = get("lastNotifiedVersion", "").toString();

    // ---- download behaviour ------------------------------------------------
    m_userAgent = get("userAgent", "").toString();
    m_referer = get("referer", "").toString();
    m_alwaysResume = get("alwaysResume", false).toBool();
    m_continueDownload = get("continueDownload", true).toBool();
    m_remoteTime = get("remoteTime", false).toBool();
    m_autoRename = get("autoRename", true).toBool();
    m_allowOverwrite = get("allowOverwrite", true).toBool();
    m_contentDispositionDefaultUtf8 = get("contentDispositionDefaultUtf8", true).toBool();
    m_fileAllocation = get("fileAllocation", "prealloc").toString();
    m_inputFile = get("inputFile", "").toString();
    m_checkIntegrity = get("checkIntegrity", false).toBool();
    m_realtimeChunkChecksum = get("realtimeChunkChecksum", true).toBool();
    m_hashCheckOnly = get("hashCheckOnly", false).toBool();
    m_removeControlFile = get("removeControlFile", true).toBool();
    m_autoSaveSession = get("autoSaveSession", true).toBool();
    m_saveSessionInterval = boundedInt(get("saveSessionInterval", 60).toInt(), 0, 86400);
    m_sessionFile = get("sessionFile", appData + "/aria2.session").toString();
    m_btExternalIp = get("btExternalIp", "").toString();
    m_btTracker = get("btTracker", "").toString();
    m_dhtEntryPoint = get("dhtEntryPoint", "").toString();
    m_dhtEntryPoint6 = get("dhtEntryPoint6", "").toString();
    m_dhtFilePath = get("dhtFilePath", "").toString();
    m_btSaveMetadataFile = get("btSaveMetadataFile", "").toString();

    // ---- throughput --------------------------------------------------------
    m_maxConcurrentDownloads = boundedInt(get("maxConcurrentDownloads", 5).toInt(), 1, 128);
    m_split = boundedInt(get("split", 16).toInt(), 1, 128);
    m_maxConnectionPerServer = boundedInt(get("maxConnectionPerServer", 16).toInt(), 1, 128);
    m_minSplitSize = get("minSplitSize", "1M").toString();
    m_maxDownloadLimit = get("maxDownloadLimit", "").toString();
    m_maxUploadLimit = get("maxUploadLimit", "").toString();
    m_maxOverallDownloadLimitKB = get("maxOverallDownloadLimitKB", 0).toInt();
    m_maxOverallUploadLimitKB = get("maxOverallUploadLimitKB", 0).toInt();
    m_optimizeConcurrentDownloads = get("optimizeConcurrentDownloads", true).toBool();
    m_optimizePieceLength = get("optimizePieceLength", true).toBool();
    m_diskCache = get("diskCache", "64M").toString();
    m_enableHttpKeepAlive = get("enableHttpKeepAlive", true).toBool();
    m_enableHttpPipelining = get("enableHttpPipelining", false).toBool();
    m_noWantDigestHeader = get("noWantDigestHeader", true).toBool();
    m_conditionalGet = get("conditionalGet", false).toBool();
    m_useHead = get("useHead", false).toBool();
    m_streamPieceSelector = get("streamPieceSelector", true).toBool();

    // ---- resilience --------------------------------------------------------
    m_connectTimeout = boundedInt(get("connectTimeout", 30).toInt(), 1, 600);
    m_socketTimeout = boundedInt(get("socketTimeout", 60).toInt(), 1, 600);
    m_timeout = boundedInt(get("timeout", 60).toInt(), 1, 600);
    m_maxTries = boundedInt(get("maxTries", 5).toInt(), 0, 100);
    m_retryWait = boundedInt(get("retryWait", 3).toInt(), 0, 600);
    m_lowestSpeedLimit = get("lowestSpeedLimit", 0).toInt();
    m_autoFileRenaming = get("autoFileRenaming", true).toBool();
    m_parameterizedUri = get("parameterizedUri", true).toBool();
    m_noProxy = get("noProxy", true).toBool();
    m_checkCertificate = get("checkCertificate", true).toBool();
    m_caCertificate = get("caCertificate", "").toString();
    m_certificate = get("certificate", "").toString();
    m_privateKey = get("privateKey", "").toString();
    m_minTlsVersion = get("minTlsVersion", "TLSv1.2").toString();

    // ---- proxy -------------------------------------------------------------
    m_proxyMode = get("proxyMode", "system").toString();
    m_allProxy = get("allProxy", "").toString();
    m_httpProxy = get("httpProxy", "").toString();
    m_httpsProxy = get("httpsProxy", "").toString();
    m_ftpProxy = get("ftpProxy", "").toString();
    m_allProxyUser = get("allProxyUser", "").toString();
    m_allProxyPasswd = get("allProxyPasswd", "").toString();
    m_noProxyList = get("noProxyList", "localhost,127.0.0.1").toString();

    // ---- bittorrent --------------------------------------------------------
    m_btListenPort = boundedInt(get("btListenPort", 6881).toInt(), 1024, 65535);
    m_dhtListenPort = boundedInt(get("dhtListenPort", 6881).toInt(), 1024, 65535);
    m_enableDht = get("enableDht", true).toBool();
    m_enableDht6 = get("enableDht6", false).toBool();
    m_enableLpd = get("enableLpd", true).toBool();
    m_btEnableHookAfterCheck = get("btEnableHookAfterCheck", false).toBool();
    m_btRequireCrypto = get("btRequireCrypto", false).toBool();
    m_btSaveMetadata = get("btSaveMetadata", true).toBool();
    m_btLoadSavedMetadata = get("btLoadSavedMetadata", true).toBool();
    m_btDetachSeedOnly = get("btDetachSeedOnly", false).toBool();
    m_btRemoveUnselectedFile = get("btRemoveUnselectedFile", false).toBool();
    m_followTorrent = get("followTorrent", true).toBool();
    m_seedUnverified = get("seedUnverified", false).toBool();
    m_btMaxPeers = boundedInt(get("btMaxPeers", 55).toInt(), 0, 1000);
    m_btMaxOpenFiles = boundedInt(get("btMaxOpenFiles", 100).toInt(), 1, 4096);
    m_btRequestTimeout = boundedInt(get("btRequestTimeout", 60).toInt(), 1, 600);
    m_btStopTimeout = get("btStopTimeout", 0).toInt();
    m_btMetadataTimeout = boundedInt(get("btMetadataTimeout", 60).toInt(), 1, 3600);
    m_btTrackerInterval = get("btTrackerInterval", 0).toInt();
    m_btTrackerTimeout = boundedInt(get("btTrackerTimeout", 60).toInt(), 1, 600);
    m_btTimeout = get("btTimeout", 0).toInt();
    m_dhtEntryPointInterval = get("dhtEntryPointInterval", 0).toInt();
    m_dhtMessageTimeout = boundedInt(get("dhtMessageTimeout", 10).toInt(), 1, 600);

    // ---- seeding -----------------------------------------------------------
    m_seedRatio = get("seedRatio", 1.0).toDouble();
    if (m_seedRatio <= 0.0)
        m_seedRatio = 0.0;
    m_seedTime = get("seedTime", 0).toInt();

    // ---- rpc ---------------------------------------------------------------
    m_rpcListenPort = boundedInt(get("rpcListenPort", 6800).toInt(), 1024, 65535);
    m_rpcSecret = get("rpcSecret", "").toString();
    m_rpcAllowOriginAll = get("rpcAllowOriginAll", true).toBool();
    m_rpcListenAll = get("rpcListenAll", false).toBool();
    m_rpcMaxRequestSize = boundedInt(get("rpcMaxRequestSize", 32 * 1024 * 1024).toInt(), 1024 * 1024, 1024 * 1024 * 1024);
    m_pauseMetadata = get("pauseMetadata", false).toBool();
    m_keepUnfinishedDownloadResult = get("keepUnfinishedDownloadResult", false).toBool();
    m_stopWithProcess = get("stopWithProcess", false).toBool();

    // ---- advanced ----------------------------------------------------------
    m_enableRpcConsole = get("enableRpcConsole", false).toBool();
    m_enableEngineLog = get("enableEngineLog", false).toBool();
    m_advancedUser = get("advancedUser", false).toBool();
    m_enableSqliteHistory = get("enableSqliteHistory", get("enableSqlite", true)).toBool();
    m_sqliteDbPath = get("sqliteDbPath", "").toString();
    m_historyKeepEntries = boundedInt(get("historyKeepEntries", 2000).toInt(), 0, 1000000);
    m_extraAria2Args = get("extraAria2Args", "").toString();
    m_configFilePath = get("configFilePath", "").toString();
    m_aria2Executable = get("aria2Executable", "").toString();
    m_autoRestartEngine = get("autoRestartEngine", true).toBool();

    // ---- scheduling --------------------------------------------------------
    m_schedulerEnabled = get("schedulerEnabled", false).toBool();
    m_scheduleStart = get("scheduleStart", "09:00").toString();
    m_scheduleStop = get("scheduleStop", "23:00").toString();
    m_scheduledDownloadLimitKB = get("scheduledDownloadLimitKB", 0).toInt();
    m_scheduledUploadLimitKB = get("scheduledUploadLimitKB", 0).toInt();
}

void SettingsManager::applyAutoStart(bool enable)
{
#ifdef Q_OS_WIN
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    if (enable) {
        QString appPath = QCoreApplication::applicationFilePath();
        appPath.replace('/', '\\');
        reg.setValue(QStringLiteral("Aria2Downloader"), QLatin1Char('"') + appPath + QLatin1String("\" --minimized"));
    } else {
        reg.remove(QStringLiteral("Aria2Downloader"));
    }
#else
    Q_UNUSED(enable)
#endif
}

QString SettingsManager::appTitle() const
{
    return QStringLiteral("Aria2 Downloader");
}

// ============================================================================
//  Setter implementations
// ============================================================================

// ---- application ----------------------------------------------------------
void SettingsManager::setAutoStart(bool v)
{
    if (m_autoStart == v)
        return;
    m_autoStart = v;
    m_settings.setValue("autoStart", v);
    applyAutoStart(v);
}
IMPL_SETTING(BOOL_PARAM, StartMinimized, m_startMinimized, "startMinimized", startMinimizedChanged)
IMPL_SETTING(BOOL_PARAM, MinimizeToTray, m_minimizeToTray, "minimizeToTray", minimizeToTrayChanged)
IMPL_SETTING(BOOL_PARAM, CloseToTray, m_closeToTray, "closeToTray", closeToTrayChanged)

void SettingsManager::setDownloadDir(const QString &v)
{
    if (m_downloadDir == v)
        return;
    m_downloadDir = v;
    m_settings.setValue("downloadDir", v);
    emit downloadDirChanged();
    emit aria2SettingsChanged();
}

IMPL_SETTING(QSTRING_PARAM, Language, m_language, "language", languageChanged)
IMPL_SETTING(QSTRING_PARAM, UiFontFamily, m_uiFontFamily, "uiFontFamily", uiFontFamilyChanged)
IMPL_SETTING(INT_PARAM, UiFontSize, m_uiFontSize, "uiFontSize", uiFontSizeChanged)
IMPL_SETTING(QSTRING_PARAM, Theme, m_theme, "theme", themeChanged)
IMPL_SETTING(QSTRING_PARAM, AccentColor, m_accentColor, "accentColor", accentColorChanged)
IMPL_SETTING(BOOL_PARAM, UseMica, m_useMica, "useMica", useMicaChanged)
IMPL_SETTING(BOOL_PARAM, EnableAnimations, m_enableAnimations, "enableAnimations", enableAnimationsChanged)
IMPL_SETTING(BOOL_PARAM, EnableCompleteNotification, m_enableCompleteNotification, "enableCompleteNotification", enableCompleteNotificationChanged)
IMPL_SETTING(BOOL_PARAM, EnableErrorNotification, m_enableErrorNotification, "enableErrorNotification", enableErrorNotificationChanged)
IMPL_SETTING(BOOL_PARAM, NotifyOnStart, m_notifyOnStart, "notifyOnStart", notifyOnStartChanged)
IMPL_SETTING(BOOL_PARAM, AutoPasteClipboard, m_autoPasteClipboard, "autoPasteClipboard", autoPasteClipboardChanged)
IMPL_SETTING(BOOL_PARAM, ClipboardMonitor, m_clipboardMonitor, "clipboardMonitor", clipboardMonitorChanged)
IMPL_SETTING(BOOL_PARAM, ShowTraySpeed, m_showTraySpeed, "showTraySpeed", showTraySpeedChanged)
IMPL_SETTING(BOOL_PARAM, BrowserIntegration, m_browserIntegration, "browserIntegration", browserIntegrationChanged)
IMPL_SETTING(INT_PARAM, BrowserPort, m_browserPort, "browserPort", browserPortChanged)
IMPL_SETTING(BOOL_PARAM, ConfirmOnExit, m_confirmOnExit, "confirmOnExit", confirmOnExitChanged)
IMPL_SETTING(BOOL_PARAM, ShowDetailsPanel, m_showDetailsPanel, "showDetailsPanel", showDetailsPanelChanged)
IMPL_SETTING(BOOL_PARAM, CheckForUpdates, m_checkForUpdates, "checkForUpdates", checkForUpdatesChanged)
IMPL_SETTING(BOOL_PARAM, UpdateIncludePrerelease, m_updateIncludePrerelease, "updateIncludePrerelease", updateIncludePrereleaseChanged)
IMPL_SETTING(QSTRING_PARAM, LastNotifiedVersion, m_lastNotifiedVersion, "lastNotifiedVersion", lastNotifiedVersionChanged)

// ---- download behaviour ---------------------------------------------------
IMPL_SETTING(QSTRING_PARAM, UserAgent, m_userAgent, "userAgent", userAgentChanged)
IMPL_SETTING(QSTRING_PARAM, Referer, m_referer, "referer", refererChanged)
IMPL_SETTING(BOOL_PARAM, AlwaysResume, m_alwaysResume, "alwaysResume", alwaysResumeChanged)
IMPL_SETTING(BOOL_PARAM, ContinueDownload, m_continueDownload, "continueDownload", continueDownloadChanged)
IMPL_SETTING(BOOL_PARAM, RemoteTime, m_remoteTime, "remoteTime", remoteTimeChanged)
IMPL_SETTING_RUNTIME(BOOL_PARAM, AutoRename, m_autoRename, "autoRename", autoRenameChanged)
IMPL_SETTING_RUNTIME(BOOL_PARAM, AllowOverwrite, m_allowOverwrite, "allowOverwrite", allowOverwriteChanged)
IMPL_SETTING(BOOL_PARAM, ContentDispositionDefaultUtf8, m_contentDispositionDefaultUtf8, "contentDispositionDefaultUtf8", contentDispositionDefaultUtf8Changed)
IMPL_SETTING(QSTRING_PARAM, FileAllocation, m_fileAllocation, "fileAllocation", fileAllocationChanged)
IMPL_SETTING(QSTRING_PARAM, InputFile, m_inputFile, "inputFile", inputFileChanged)
IMPL_SETTING(BOOL_PARAM, CheckIntegrity, m_checkIntegrity, "checkIntegrity", checkIntegrityChanged)
IMPL_SETTING_RUNTIME(BOOL_PARAM, RealtimeChunkChecksum, m_realtimeChunkChecksum, "realtimeChunkChecksum", realtimeChunkChecksumChanged)
IMPL_SETTING(BOOL_PARAM, HashCheckOnly, m_hashCheckOnly, "hashCheckOnly", hashCheckOnlyChanged)
IMPL_SETTING(BOOL_PARAM, RemoveControlFile, m_removeControlFile, "removeControlFile", removeControlFileChanged)
IMPL_SETTING(BOOL_PARAM, AutoSaveSession, m_autoSaveSession, "autoSaveSession", autoSaveSessionChanged)
IMPL_SETTING(INT_PARAM, SaveSessionInterval, m_saveSessionInterval, "saveSessionInterval", saveSessionIntervalChanged)
IMPL_SETTING(QSTRING_PARAM, SessionFile, m_sessionFile, "sessionFile", sessionFileChanged)

// ---- throughput -----------------------------------------------------------
IMPL_SETTING_RUNTIME(INT_PARAM, MaxConcurrentDownloads, m_maxConcurrentDownloads, "maxConcurrentDownloads", maxConcurrentDownloadsChanged)
IMPL_SETTING(INT_PARAM, Split, m_split, "split", splitChanged)
IMPL_SETTING(INT_PARAM, MaxConnectionPerServer, m_maxConnectionPerServer, "maxConnectionPerServer", maxConnectionPerServerChanged)
IMPL_SETTING(QSTRING_PARAM, MinSplitSize, m_minSplitSize, "minSplitSize", minSplitSizeChanged)
IMPL_SETTING(QSTRING_PARAM, MaxDownloadLimit, m_maxDownloadLimit, "maxDownloadLimit", maxDownloadLimitChanged)
IMPL_SETTING(QSTRING_PARAM, MaxUploadLimit, m_maxUploadLimit, "maxUploadLimit", maxUploadLimitChanged)
IMPL_SETTING_RUNTIME(INT_PARAM, MaxOverallDownloadLimitKB, m_maxOverallDownloadLimitKB, "maxOverallDownloadLimitKB", maxOverallDownloadLimitKBChanged)
IMPL_SETTING_RUNTIME(INT_PARAM, MaxOverallUploadLimitKB, m_maxOverallUploadLimitKB, "maxOverallUploadLimitKB", maxOverallUploadLimitKBChanged)

void SettingsManager::setOptimizeConcurrentDownloads(bool v)
{
    if (m_optimizeConcurrentDownloads == v)
        return;
    m_optimizeConcurrentDownloads = v;
    m_settings.setValue("optimizeConcurrentDownloads", v);
    emit optimizeConcurrentDownloadsChanged();
    emit aria2SettingsChanged();
    if (v)
        emit runtimeOptionsChanged();
}

void SettingsManager::setOptimizePieceLength(bool v)
{
    if (m_optimizePieceLength == v)
        return;
    m_optimizePieceLength = v;
    m_settings.setValue("optimizePieceLength", v);
    emit optimizePieceLengthChanged();
    emit aria2SettingsChanged();
    if (!v)
        emit runtimeOptionsChanged();
}

IMPL_SETTING(QSTRING_PARAM, DiskCache, m_diskCache, "diskCache", diskCacheChanged)
IMPL_SETTING(BOOL_PARAM, EnableHttpKeepAlive, m_enableHttpKeepAlive, "enableHttpKeepAlive", enableHttpKeepAliveChanged)
IMPL_SETTING(BOOL_PARAM, EnableHttpPipelining, m_enableHttpPipelining, "enableHttpPipelining", enableHttpPipeliningChanged)
IMPL_SETTING(BOOL_PARAM, NoWantDigestHeader, m_noWantDigestHeader, "noWantDigestHeader", noWantDigestHeaderChanged)
IMPL_SETTING(BOOL_PARAM, ConditionalGet, m_conditionalGet, "conditionalGet", conditionalGetChanged)
IMPL_SETTING(BOOL_PARAM, UseHead, m_useHead, "useHead", useHeadChanged)

void SettingsManager::setStreamPieceSelector(bool v)
{
    if (m_streamPieceSelector == v)
        return;
    m_streamPieceSelector = v;
    m_settings.setValue("streamPieceSelector", v);
    emit streamPieceSelectorChanged();
    emit aria2SettingsChanged();
    if (v)
        emit runtimeOptionsChanged();
}

// ---- resilience -----------------------------------------------------------
IMPL_SETTING(INT_PARAM, ConnectTimeout, m_connectTimeout, "connectTimeout", connectTimeoutChanged)
IMPL_SETTING(INT_PARAM, SocketTimeout, m_socketTimeout, "socketTimeout", socketTimeoutChanged)
IMPL_SETTING(INT_PARAM, Timeout, m_timeout, "timeout", timeoutChanged)
IMPL_SETTING(INT_PARAM, MaxTries, m_maxTries, "maxTries", maxTriesChanged)
IMPL_SETTING(INT_PARAM, RetryWait, m_retryWait, "retryWait", retryWaitChanged)
IMPL_SETTING(INT_PARAM, LowestSpeedLimit, m_lowestSpeedLimit, "lowestSpeedLimit", lowestSpeedLimitChanged)
IMPL_SETTING(BOOL_PARAM, AutoFileRenaming, m_autoFileRenaming, "autoFileRenaming", autoFileRenamingChanged)
IMPL_SETTING(BOOL_PARAM, ParameterizedUri, m_parameterizedUri, "parameterizedUri", parameterizedUriChanged)
IMPL_SETTING(BOOL_PARAM, NoProxy, m_noProxy, "noProxy", noProxyChanged)
IMPL_SETTING(BOOL_PARAM, CheckCertificate, m_checkCertificate, "checkCertificate", checkCertificateChanged)
IMPL_SETTING(QSTRING_PARAM, CaCertificate, m_caCertificate, "caCertificate", caCertificateChanged)
IMPL_SETTING(QSTRING_PARAM, Certificate, m_certificate, "certificate", certificateChanged)
IMPL_SETTING(QSTRING_PARAM, PrivateKey, m_privateKey, "privateKey", privateKeyChanged)
IMPL_SETTING(QSTRING_PARAM, MinTlsVersion, m_minTlsVersion, "minTlsVersion", minTlsVersionChanged)

// ---- proxy ----------------------------------------------------------------
IMPL_SETTING(QSTRING_PARAM, ProxyMode, m_proxyMode, "proxyMode", proxyModeChanged)
IMPL_SETTING(QSTRING_PARAM, AllProxy, m_allProxy, "allProxy", allProxyChanged)
IMPL_SETTING(QSTRING_PARAM, HttpProxy, m_httpProxy, "httpProxy", httpProxyChanged)
IMPL_SETTING(QSTRING_PARAM, HttpsProxy, m_httpsProxy, "httpsProxy", httpsProxyChanged)
IMPL_SETTING(QSTRING_PARAM, FtpProxy, m_ftpProxy, "ftpProxy", ftpProxyChanged)
IMPL_SETTING(QSTRING_PARAM, AllProxyUser, m_allProxyUser, "allProxyUser", allProxyUserChanged)
IMPL_SETTING(QSTRING_PARAM, AllProxyPasswd, m_allProxyPasswd, "allProxyPasswd", allProxyPasswdChanged)
IMPL_SETTING(QSTRING_PARAM, NoProxyList, m_noProxyList, "noProxyList", noProxyListChanged)

// ---- bittorrent -----------------------------------------------------------
IMPL_SETTING(INT_PARAM, BtListenPort, m_btListenPort, "btListenPort", btListenPortChanged)
IMPL_SETTING(INT_PARAM, DhtListenPort, m_dhtListenPort, "dhtListenPort", dhtListenPortChanged)
IMPL_SETTING_RUNTIME(BOOL_PARAM, EnableDht, m_enableDht, "enableDht", enableDhtChanged)
IMPL_SETTING(BOOL_PARAM, EnableDht6, m_enableDht6, "enableDht6", enableDht6Changed)
IMPL_SETTING_RUNTIME(BOOL_PARAM, EnableLpd, m_enableLpd, "enableLpd", enableLpdChanged)
IMPL_SETTING(BOOL_PARAM, BtEnableHookAfterCheck, m_btEnableHookAfterCheck, "btEnableHookAfterCheck", btEnableHookAfterCheckChanged)
IMPL_SETTING(BOOL_PARAM, BtRequireCrypto, m_btRequireCrypto, "btRequireCrypto", btRequireCryptoChanged)
IMPL_SETTING(BOOL_PARAM, BtSaveMetadata, m_btSaveMetadata, "btSaveMetadata", btSaveMetadataChanged)
IMPL_SETTING(BOOL_PARAM, BtLoadSavedMetadata, m_btLoadSavedMetadata, "btLoadSavedMetadata", btLoadSavedMetadataChanged)
IMPL_SETTING(BOOL_PARAM, BtDetachSeedOnly, m_btDetachSeedOnly, "btDetachSeedOnly", btDetachSeedOnlyChanged)
IMPL_SETTING(BOOL_PARAM, BtRemoveUnselectedFile, m_btRemoveUnselectedFile, "btRemoveUnselectedFile", btRemoveUnselectedFileChanged)
IMPL_SETTING(BOOL_PARAM, FollowTorrent, m_followTorrent, "followTorrent", followTorrentChanged)
IMPL_SETTING(BOOL_PARAM, SeedUnverified, m_seedUnverified, "seedUnverified", seedUnverifiedChanged)
IMPL_SETTING_RUNTIME(INT_PARAM, BtMaxPeers, m_btMaxPeers, "btMaxPeers", btMaxPeersChanged)
IMPL_SETTING(INT_PARAM, BtMaxOpenFiles, m_btMaxOpenFiles, "btMaxOpenFiles", btMaxOpenFilesChanged)
IMPL_SETTING(INT_PARAM, BtRequestTimeout, m_btRequestTimeout, "btRequestTimeout", btRequestTimeoutChanged)
IMPL_SETTING(INT_PARAM, BtStopTimeout, m_btStopTimeout, "btStopTimeout", btStopTimeoutChanged)
IMPL_SETTING_RUNTIME(INT_PARAM, BtMetadataTimeout, m_btMetadataTimeout, "btMetadataTimeout", btMetadataTimeoutChanged)
IMPL_SETTING(INT_PARAM, BtTrackerInterval, m_btTrackerInterval, "btTrackerInterval", btTrackerIntervalChanged)
IMPL_SETTING(INT_PARAM, BtTrackerTimeout, m_btTrackerTimeout, "btTrackerTimeout", btTrackerTimeoutChanged)
IMPL_SETTING(INT_PARAM, BtTimeout, m_btTimeout, "btTimeout", btTimeoutChanged)
IMPL_SETTING(INT_PARAM, DhtEntryPointInterval, m_dhtEntryPointInterval, "dhtEntryPointInterval", dhtEntryPointIntervalChanged)
IMPL_SETTING(INT_PARAM, DhtMessageTimeout, m_dhtMessageTimeout, "dhtMessageTimeout", dhtMessageTimeoutChanged)
IMPL_SETTING(QSTRING_PARAM, BtExternalIp, m_btExternalIp, "btExternalIp", btExternalIpChanged)
IMPL_SETTING(QSTRING_PARAM, BtTracker, m_btTracker, "btTracker", btTrackerChanged)
IMPL_SETTING(QSTRING_PARAM, DhtEntryPoint, m_dhtEntryPoint, "dhtEntryPoint", dhtEntryPointChanged)
IMPL_SETTING(QSTRING_PARAM, DhtEntryPoint6, m_dhtEntryPoint6, "dhtEntryPoint6", dhtEntryPoint6Changed)
IMPL_SETTING(QSTRING_PARAM, DhtFilePath, m_dhtFilePath, "dhtFilePath", dhtFilePathChanged)
IMPL_SETTING(QSTRING_PARAM, BtSaveMetadataFile, m_btSaveMetadataFile, "btSaveMetadataFile", btSaveMetadataFileChanged)

// ---- seeding --------------------------------------------------------------
IMPL_SETTING_RUNTIME(DOUBLE_PARAM, SeedRatio, m_seedRatio, "seedRatio", seedRatioChanged)
IMPL_SETTING_RUNTIME(INT_PARAM, SeedTime, m_seedTime, "seedTime", seedTimeChanged)

// ---- rpc ------------------------------------------------------------------
void SettingsManager::setRpcListenPort(int v)
{
    v = boundedInt(v, 1024, 65535);
    if (m_rpcListenPort == v)
        return;
    m_rpcListenPort = v;
    m_settings.setValue("rpcListenPort", v);
    emit rpcListenPortChanged();
    emit aria2SettingsChanged();
}

void SettingsManager::setRpcSecret(const QString &v)
{
    if (m_rpcSecret == v)
        return;
    m_rpcSecret = v;
    m_settings.setValue("rpcSecret", v);
    emit rpcSecretChanged();
    emit aria2SettingsChanged();
}

IMPL_SETTING(BOOL_PARAM, RpcAllowOriginAll, m_rpcAllowOriginAll, "rpcAllowOriginAll", rpcAllowOriginAllChanged)
IMPL_SETTING(BOOL_PARAM, RpcListenAll, m_rpcListenAll, "rpcListenAll", rpcListenAllChanged)
IMPL_SETTING(INT_PARAM, RpcMaxRequestSize, m_rpcMaxRequestSize, "rpcMaxRequestSize", rpcMaxRequestSizeChanged)
IMPL_SETTING_RUNTIME(BOOL_PARAM, PauseMetadata, m_pauseMetadata, "pauseMetadata", pauseMetadataChanged)
IMPL_SETTING(BOOL_PARAM, KeepUnfinishedDownloadResult, m_keepUnfinishedDownloadResult, "keepUnfinishedDownloadResult", keepUnfinishedDownloadResultChanged)
IMPL_SETTING(BOOL_PARAM, StopWithProcess, m_stopWithProcess, "stopWithProcess", stopWithProcessChanged)

// ---- advanced -------------------------------------------------------------
IMPL_SETTING(BOOL_PARAM, EnableRpcConsole, m_enableRpcConsole, "enableRpcConsole", enableRpcConsoleChanged)
IMPL_SETTING(BOOL_PARAM, EnableEngineLog, m_enableEngineLog, "enableEngineLog", enableEngineLogChanged)
IMPL_SETTING(BOOL_PARAM, AdvancedUser, m_advancedUser, "advancedUser", advancedUserChanged)
IMPL_SETTING(BOOL_PARAM, EnableSqliteHistory, m_enableSqliteHistory, "enableSqliteHistory", enableSqliteHistoryChanged)
IMPL_SETTING(QSTRING_PARAM, SqliteDbPath, m_sqliteDbPath, "sqliteDbPath", sqliteDbPathChanged)
IMPL_SETTING(INT_PARAM, HistoryKeepEntries, m_historyKeepEntries, "historyKeepEntries", historyKeepEntriesChanged)
IMPL_SETTING(QSTRING_PARAM, ExtraAria2Args, m_extraAria2Args, "extraAria2Args", extraAria2ArgsChanged)
IMPL_SETTING(QSTRING_PARAM, ConfigFilePath, m_configFilePath, "configFilePath", configFilePathChanged)
IMPL_SETTING(QSTRING_PARAM, Aria2Executable, m_aria2Executable, "aria2Executable", aria2ExecutableChanged)
IMPL_SETTING(BOOL_PARAM, AutoRestartEngine, m_autoRestartEngine, "autoRestartEngine", autoRestartEngineChanged)

// ---- scheduling -----------------------------------------------------------
IMPL_SETTING(BOOL_PARAM, SchedulerEnabled, m_schedulerEnabled, "schedulerEnabled", schedulerEnabledChanged)
IMPL_SETTING(QSTRING_PARAM, ScheduleStart, m_scheduleStart, "scheduleStart", scheduleStartChanged)
IMPL_SETTING(QSTRING_PARAM, ScheduleStop, m_scheduleStop, "scheduleStop", scheduleStopChanged)
IMPL_SETTING_RUNTIME(INT_PARAM, ScheduledDownloadLimitKB, m_scheduledDownloadLimitKB, "scheduledDownloadLimitKB", scheduledDownloadLimitKBChanged)
IMPL_SETTING_RUNTIME(INT_PARAM, ScheduledUploadLimitKB, m_scheduledUploadLimitKB, "scheduledUploadLimitKB", scheduledUploadLimitKBChanged)

#undef IMPL_SETTING
#undef IMPL_SETTING_RUNTIME

// ============================================================================
//  aria2 command line / runtime options
// ============================================================================

QVariantMap SettingsManager::runtimeOptions() const
{
    QVariantMap o;

    // Only options that aria2 accepts through `changeGlobalOption` belong here,
    // and every name has to exist in aria2 1.37's option table.
    auto boolean = [](bool v) { return v ? QStringLiteral("true") : QStringLiteral("false"); };

    // ---- connection / throughput
    o.insert(QStringLiteral("max-concurrent-downloads"), m_maxConcurrentDownloads);
    o.insert(QStringLiteral("max-connection-per-server"), m_maxConnectionPerServer);
    o.insert(QStringLiteral("split"), m_split);
    o.insert(QStringLiteral("min-split-size"), m_minSplitSize);
    o.insert(QStringLiteral("optimize-concurrent-downloads"), boolean(m_optimizeConcurrentDownloads));
    o.insert(QStringLiteral("lowest-speed-limit"), QString::number(m_lowestSpeedLimit) + QLatin1Char('K'));
    o.insert(QStringLiteral("max-overall-download-limit"),
             m_maxOverallDownloadLimitKB > 0 ? QString::number(m_maxOverallDownloadLimitKB) + QLatin1Char('K')
                                             : QStringLiteral("0"));
    o.insert(QStringLiteral("max-overall-upload-limit"),
             m_maxOverallUploadLimitKB > 0 ? QString::number(m_maxOverallUploadLimitKB) + QLatin1Char('K')
                                           : QStringLiteral("0"));
    o.insert(QStringLiteral("max-download-limit"),
             m_maxDownloadLimit.isEmpty() ? QStringLiteral("0") : normalizeSize(m_maxDownloadLimit));
    o.insert(QStringLiteral("max-upload-limit"),
             m_maxUploadLimit.isEmpty() ? QStringLiteral("0") : normalizeSize(m_maxUploadLimit));
    o.insert(QStringLiteral("max-tries"), m_maxTries);
    o.insert(QStringLiteral("retry-wait"), m_retryWait);
    o.insert(QStringLiteral("connect-timeout"), m_connectTimeout);
    o.insert(QStringLiteral("timeout"), m_timeout);
    o.insert(QStringLiteral("stream-piece-selector"),
             m_streamPieceSelector ? QStringLiteral("default") : QStringLiteral("inorder"));

    // ---- files & naming
    o.insert(QStringLiteral("auto-file-renaming"), boolean(m_autoFileRenaming));
    o.insert(QStringLiteral("allow-overwrite"), boolean(m_allowOverwrite));
    o.insert(QStringLiteral("always-resume"), boolean(m_alwaysResume));
    o.insert(QStringLiteral("continue"), boolean(m_continueDownload));
    o.insert(QStringLiteral("remote-time"), boolean(m_remoteTime));
    o.insert(QStringLiteral("realtime-chunk-checksum"), boolean(m_realtimeChunkChecksum));
    o.insert(QStringLiteral("content-disposition-default-utf8"), boolean(m_contentDispositionDefaultUtf8));

    // ---- bittorrent
    o.insert(QStringLiteral("enable-dht"), boolean(m_enableDht));
    o.insert(QStringLiteral("enable-dht6"), boolean(m_enableDht6));
    o.insert(QStringLiteral("bt-enable-lpd"), boolean(m_enableLpd));
    o.insert(QStringLiteral("follow-torrent"), boolean(m_followTorrent));
    o.insert(QStringLiteral("bt-seed-unverified"), boolean(m_seedUnverified));
    o.insert(QStringLiteral("bt-max-peers"), m_btMaxPeers);
    o.insert(QStringLiteral("bt-stop-timeout"), m_btStopTimeout);
    o.insert(QStringLiteral("bt-tracker-timeout"), m_btTrackerTimeout);
    o.insert(QStringLiteral("bt-tracker-connect-timeout"), m_btTrackerTimeout);
    o.insert(QStringLiteral("pause-metadata"), boolean(m_pauseMetadata));

    // ---- seeding
    o.insert(QStringLiteral("seed-ratio"), QString::number(m_seedRatio, 'g', 6));
    if (m_seedTime > 0)
        o.insert(QStringLiteral("seed-time"), QString::number(m_seedTime));

    // ---- misc global options
    o.insert(QStringLiteral("keep-unfinished-download-result"),
             m_keepUnfinishedDownloadResult ? QStringLiteral("true") : QStringLiteral("false"));

    return o;
}

QStringList SettingsManager::extraAria2ArgumentsList() const
{
    if (m_extraAria2Args.trimmed().isEmpty())
        return {};
    return QProcess::splitCommand(m_extraAria2Args.trimmed());
}

QStringList SettingsManager::buildAria2Arguments() const
{
    QStringList args;
    auto add = [&args](const QString &option, const QString &value) {
        args << QStringLiteral("--") + option + QLatin1Char('=') + value;
    };
    auto addBool = [&add](const QString &option, bool v) { add(option, v ? QStringLiteral("true") : QStringLiteral("false")); };
    auto addNum = [&add](const QString &option, int v) { add(option, QString::number(v)); };

    // ---- extended config file first: explicit switches must win.
    if (!m_configFilePath.isEmpty() && QFileInfo::exists(m_configFilePath))
        add(QStringLiteral("conf-path"), QDir::toNativeSeparators(m_configFilePath));
    else
        add(QStringLiteral("no-conf"), QStringLiteral("true"));

    // ---- rpc / process
    addBool(QStringLiteral("enable-rpc"), true);
    addBool(QStringLiteral("rpc-allow-origin-all"), m_rpcAllowOriginAll);
    addBool(QStringLiteral("rpc-listen-all"), m_rpcListenAll);
    addNum(QStringLiteral("rpc-listen-port"), m_rpcListenPort);
    addNum(QStringLiteral("rpc-max-request-size"), m_rpcMaxRequestSize);
    if (!m_rpcSecret.isEmpty())
        add(QStringLiteral("rpc-secret"), m_rpcSecret);
    // NOTE: --stop-with-process takes a PID, not a boolean. Handing it "false"
    // makes aria2c fail with "Bad number false" and exit immediately. Passing
    // our own PID ties the engine lifetime to this application.
    if (m_stopWithProcess)
        addNum(QStringLiteral("stop-with-process"), int(QCoreApplication::applicationPid()));
    addBool(QStringLiteral("pause"), false);

    // ---- console noise: keep the engine log readable
    add(QStringLiteral("console-log-level"), QStringLiteral("warn"));
    add(QStringLiteral("log-level"), QStringLiteral("warn"));
    addNum(QStringLiteral("summary-interval"), 0);
    addBool(QStringLiteral("show-console-readout"), false);

    // ---- destination & session
    // NOTE: there is no `auto-save-session` switch in aria2; persistence is
    // driven purely by --save-session plus --save-session-interval. Passing an
    // unknown option makes aria2c exit immediately with status 28.
    add(QStringLiteral("dir"), QDir::toNativeSeparators(m_downloadDir));
    if (m_autoSaveSession) {
        add(QStringLiteral("save-session"), QDir::toNativeSeparators(m_sessionFile));
        addNum(QStringLiteral("save-session-interval"), m_saveSessionInterval);
        addBool(QStringLiteral("force-save"), true);
    }
    // Seconds, not a boolean (valid range 0-600).
    addNum(QStringLiteral("auto-save-interval"), 60);
    if (!m_inputFile.isEmpty() && QFileInfo::exists(m_inputFile))
        add(QStringLiteral("input-file"), QDir::toNativeSeparators(m_inputFile));

    // ---- throughput
    addNum(QStringLiteral("max-concurrent-downloads"), m_maxConcurrentDownloads);
    addNum(QStringLiteral("split"), m_split);
    addNum(QStringLiteral("max-connection-per-server"), m_maxConnectionPerServer);
    add(QStringLiteral("min-split-size"), normalizeSize(m_minSplitSize));
    addBool(QStringLiteral("optimize-concurrent-downloads"), m_optimizeConcurrentDownloads);
    add(QStringLiteral("disk-cache"), normalizeSize(m_diskCache));
    add(QStringLiteral("file-allocation"), m_fileAllocation);
    addBool(QStringLiteral("enable-http-keep-alive"), m_enableHttpKeepAlive);
    addBool(QStringLiteral("enable-http-pipelining"), m_enableHttpPipelining);
    addBool(QStringLiteral("no-want-digest-header"), m_noWantDigestHeader);
    addBool(QStringLiteral("conditional-get"), m_conditionalGet);
    addBool(QStringLiteral("use-head"), m_useHead);
    add(QStringLiteral("stream-piece-selector"),
        m_streamPieceSelector ? QStringLiteral("default") : QStringLiteral("inorder"));
    add(QStringLiteral("lowest-speed-limit"), QString::number(m_lowestSpeedLimit) + QLatin1Char('K'));
    add(QStringLiteral("max-overall-download-limit"),
        m_maxOverallDownloadLimitKB > 0 ? QString::number(m_maxOverallDownloadLimitKB) + QLatin1Char('K')
                                        : QStringLiteral("0"));
    add(QStringLiteral("max-overall-upload-limit"),
        m_maxOverallUploadLimitKB > 0 ? QString::number(m_maxOverallUploadLimitKB) + QLatin1Char('K')
                                      : QStringLiteral("0"));
    if (!m_maxDownloadLimit.isEmpty())
        add(QStringLiteral("max-download-limit"), normalizeSize(m_maxDownloadLimit));
    if (!m_maxUploadLimit.isEmpty())
        add(QStringLiteral("max-upload-limit"), normalizeSize(m_maxUploadLimit));

    // ---- resilience
    addNum(QStringLiteral("connect-timeout"), m_connectTimeout);
    addNum(QStringLiteral("timeout"), m_timeout);
    addNum(QStringLiteral("max-tries"), m_maxTries);
    addNum(QStringLiteral("retry-wait"), m_retryWait);
    addBool(QStringLiteral("auto-file-renaming"), m_autoFileRenaming);
    addBool(QStringLiteral("allow-overwrite"), m_allowOverwrite);
    addBool(QStringLiteral("always-resume"), m_alwaysResume);
    addBool(QStringLiteral("continue"), m_continueDownload);
    addBool(QStringLiteral("remote-time"), m_remoteTime);
    addBool(QStringLiteral("parameterized-uri"), m_parameterizedUri);
    addBool(QStringLiteral("realtime-chunk-checksum"), m_realtimeChunkChecksum);
    addBool(QStringLiteral("content-disposition-default-utf8"), m_contentDispositionDefaultUtf8);
    addBool(QStringLiteral("check-integrity"), m_checkIntegrity);
    addBool(QStringLiteral("hash-check-only"), m_hashCheckOnly);
    addBool(QStringLiteral("remove-control-file"), m_removeControlFile);

    // ---- TLS
    addBool(QStringLiteral("check-certificate"), m_checkCertificate);
    add(QStringLiteral("min-tls-version"), m_minTlsVersion);
    QString ca = m_caCertificate;
    if (ca.isEmpty()) {
        const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/ca-bundle.crt");
        if (QFileInfo::exists(beside))
            ca = beside;
    }
    if (!ca.isEmpty() && QFileInfo::exists(ca))
        add(QStringLiteral("ca-certificate"), QDir::toNativeSeparators(ca));
    if (!m_certificate.isEmpty() && QFileInfo::exists(m_certificate))
        add(QStringLiteral("certificate"), QDir::toNativeSeparators(m_certificate));
    if (!m_privateKey.isEmpty() && QFileInfo::exists(m_privateKey))
        add(QStringLiteral("private-key"), QDir::toNativeSeparators(m_privateKey));

    // ---- proxy
    // NOTE: `--no-proxy` is NOT a boolean switch in aria2; it takes a comma
    // separated domain list. Disabling proxies is spelled `--all-proxy=""`.
    if (m_proxyMode == QLatin1String("none")) {
        add(QStringLiteral("all-proxy"), QString());
    } else if (m_proxyMode == QLatin1String("manual")) {
        if (!m_allProxy.isEmpty())
            add(QStringLiteral("all-proxy"), m_allProxy);
        if (!m_httpProxy.isEmpty())
            add(QStringLiteral("http-proxy"), m_httpProxy);
        if (!m_httpsProxy.isEmpty())
            add(QStringLiteral("https-proxy"), m_httpsProxy);
        if (!m_ftpProxy.isEmpty())
            add(QStringLiteral("ftp-proxy"), m_ftpProxy);
        if (!m_allProxyUser.isEmpty())
            add(QStringLiteral("all-proxy-user"), m_allProxyUser);
        if (!m_allProxyPasswd.isEmpty())
            add(QStringLiteral("all-proxy-passwd"), m_allProxyPasswd);
    } else if (m_noProxy) { // "system" + explicit opt-out
        add(QStringLiteral("all-proxy"), QString());
    }

    if (!m_userAgent.isEmpty())
        add(QStringLiteral("user-agent"), m_userAgent);
    if (!m_referer.isEmpty())
        add(QStringLiteral("referer"), m_referer);

    // ---- bittorrent
    // Option names below are verified against `aria2c --help=#all` for 1.37:
    // aria2c aborts with exit code 28 on the first unknown switch, so a typo
    // here means the engine never starts at all.
    addNum(QStringLiteral("listen-port"), m_btListenPort);
    addNum(QStringLiteral("dht-listen-port"), m_dhtListenPort);
    addBool(QStringLiteral("enable-dht"), m_enableDht);
    addBool(QStringLiteral("enable-dht6"), m_enableDht6);
    addBool(QStringLiteral("bt-enable-lpd"), m_enableLpd);
    addBool(QStringLiteral("bt-enable-hook-after-hash-check"), m_btEnableHookAfterCheck);
    addBool(QStringLiteral("bt-require-crypto"), m_btRequireCrypto);
    addBool(QStringLiteral("bt-save-metadata"), m_btSaveMetadata);
    addBool(QStringLiteral("bt-load-saved-metadata"), m_btLoadSavedMetadata);
    addBool(QStringLiteral("bt-detach-seed-only"), m_btDetachSeedOnly);
    addBool(QStringLiteral("bt-remove-unselected-file"), m_btRemoveUnselectedFile);
    addBool(QStringLiteral("follow-torrent"), m_followTorrent);
    addBool(QStringLiteral("bt-seed-unverified"), m_seedUnverified);
    addNum(QStringLiteral("bt-max-peers"), m_btMaxPeers);
    addNum(QStringLiteral("bt-max-open-files"), m_btMaxOpenFiles);
    // The per-peer stall guard is `--bt-request-peer-speed-limit`, not a timeout.
    add(QStringLiteral("bt-request-peer-speed-limit"),
        QString::number(qMax(0, m_lowestSpeedLimit)) + QLatin1Char('K'));
    add(QStringLiteral("bt-stop-timeout"), QString::number(m_btStopTimeout));
    addNum(QStringLiteral("bt-tracker-connect-timeout"), m_btTrackerTimeout);
    addNum(QStringLiteral("bt-tracker-timeout"), m_btTrackerTimeout);
    if (m_btTrackerInterval > 0)
        addNum(QStringLiteral("bt-tracker-interval"), m_btTrackerInterval);
    if (!m_btExternalIp.isEmpty())
        add(QStringLiteral("bt-external-ip"), m_btExternalIp);
    if (!m_btTracker.isEmpty())
        add(QStringLiteral("bt-tracker"), m_btTracker);

    addNum(QStringLiteral("dht-message-timeout"), m_dhtMessageTimeout);
    if (!m_dhtEntryPoint.isEmpty())
        add(QStringLiteral("dht-entry-point"), m_dhtEntryPoint);
    if (!m_dhtEntryPoint6.isEmpty())
        add(QStringLiteral("dht-entry-point6"), m_dhtEntryPoint6);
    if (!m_dhtFilePath.isEmpty())
        add(QStringLiteral("dht-file-path"), QDir::toNativeSeparators(m_dhtFilePath));

    // ---- seeding
    add(QStringLiteral("seed-ratio"), QString::number(m_seedRatio, 'g', 6));
    if (m_seedTime > 0)
        addNum(QStringLiteral("seed-time"), m_seedTime);

    addBool(QStringLiteral("keep-unfinished-download-result"), m_keepUnfinishedDownloadResult);
    addBool(QStringLiteral("pause-metadata"), m_pauseMetadata);
    addBool(QStringLiteral("enable-mmap"), true);

    // ---- user supplied extras (last word wins)
    const QStringList extra = extraAria2ArgumentsList();
    for (const QString &e : extra)
        args << e;

    return args;
}
