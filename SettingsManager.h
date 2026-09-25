#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QSettings>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

/**
 * SettingsManager - single source of truth for every user preference.
 *
 * Preferences come in three flavours:
 *   * application settings  (tray, notifications, appearance, integration)
 *   * aria2 launch options  (mapped 1:1 onto aria2c command line switches)
 *   * aria2 global options  (changeable at runtime via changeGlobalOption)
 *
 * Every value that aria2 cares about emits `aria2SettingsChanged()`; values in
 * the runtime-changeable subset additionally emit `runtimeOptionsChanged()` so
 * the controller can push them live instead of restarting the engine.
 */
class SettingsManager : public QObject
{
    Q_OBJECT
public:
    explicit SettingsManager(QObject *parent = nullptr);

    // ----------------------------------------------------------- generic access
    Q_INVOKABLE QVariant value(const QString &key, const QVariant &fallback = QVariant()) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE bool contains(const QString &key) const;
    Q_INVOKABLE void resetToDefaults();
    Q_INVOKABLE void sync();

    /// The RPC port actually in use.
    Q_INVOKABLE int rpcPort() const { return m_rpcListenPort; }

    /// Bumped on every settings change. QML binds appearance tokens to this so
    /// a single notification refreshes the whole theme.
    Q_PROPERTY(int settingsRevision READ settingsRevision NOTIFY settingsRevisionChanged)
    int settingsRevision() const { return m_settingsRevision; }

    /// Build the complete aria2c command line from the current preferences.
    QStringList buildAria2Arguments() const;
    /// The user-supplied extra switches, already split into arguments.
    QStringList extraAria2ArgumentsList() const;
    /// The subset of options aria2 can change while running.
    QVariantMap runtimeOptions() const;
    QString appTitle() const;

    // ======================================================== application knobs
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY autoStartChanged)
    Q_PROPERTY(bool startMinimized READ startMinimized WRITE setStartMinimized NOTIFY startMinimizedChanged)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray NOTIFY minimizeToTrayChanged)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY closeToTrayChanged)
    Q_PROPERTY(QString downloadDir READ downloadDir WRITE setDownloadDir NOTIFY downloadDirChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    /// UI font family (empty = the platform's own UI font) and base size in px.
    Q_PROPERTY(QString uiFontFamily READ uiFontFamily WRITE setUiFontFamily NOTIFY uiFontFamilyChanged)
    Q_PROPERTY(int uiFontSize READ uiFontSize WRITE setUiFontSize NOTIFY uiFontSizeChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY accentColorChanged)
    Q_PROPERTY(bool useMica READ useMica WRITE setUseMica NOTIFY useMicaChanged)
    Q_PROPERTY(bool enableAnimations READ enableAnimations WRITE setEnableAnimations NOTIFY enableAnimationsChanged)
    Q_PROPERTY(bool enableCompleteNotification READ enableCompleteNotification WRITE setEnableCompleteNotification NOTIFY enableCompleteNotificationChanged)
    Q_PROPERTY(bool enableErrorNotification READ enableErrorNotification WRITE setEnableErrorNotification NOTIFY enableErrorNotificationChanged)
    Q_PROPERTY(bool notifyOnStart READ notifyOnStart WRITE setNotifyOnStart NOTIFY notifyOnStartChanged)
    Q_PROPERTY(bool autoPasteClipboard READ autoPasteClipboard WRITE setAutoPasteClipboard NOTIFY autoPasteClipboardChanged)
    Q_PROPERTY(bool clipboardMonitor READ clipboardMonitor WRITE setClipboardMonitor NOTIFY clipboardMonitorChanged)
    Q_PROPERTY(bool showTraySpeed READ showTraySpeed WRITE setShowTraySpeed NOTIFY showTraySpeedChanged)
    Q_PROPERTY(bool browserIntegration READ browserIntegration WRITE setBrowserIntegration NOTIFY browserIntegrationChanged)
    Q_PROPERTY(int browserPort READ browserPort WRITE setBrowserPort NOTIFY browserPortChanged)
    Q_PROPERTY(bool confirmOnExit READ confirmOnExit WRITE setConfirmOnExit NOTIFY confirmOnExitChanged)
    Q_PROPERTY(bool showDetailsPanel READ showDetailsPanel WRITE setShowDetailsPanel NOTIFY showDetailsPanelChanged)

    // Update checking. lastNotifiedVersion remembers which release the user has
    // already been told about, so a new one is announced once rather than on
    // every launch.
    Q_PROPERTY(bool checkForUpdates READ checkForUpdates WRITE setCheckForUpdates NOTIFY checkForUpdatesChanged)
    Q_PROPERTY(bool updateIncludePrerelease READ updateIncludePrerelease WRITE setUpdateIncludePrerelease NOTIFY updateIncludePrereleaseChanged)
    Q_PROPERTY(QString lastNotifiedVersion READ lastNotifiedVersion WRITE setLastNotifiedVersion NOTIFY lastNotifiedVersionChanged)

    bool autoStart() const { return m_autoStart; }
    void setAutoStart(bool v);
    bool startMinimized() const { return m_startMinimized; }
    void setStartMinimized(bool v);
    bool minimizeToTray() const { return m_minimizeToTray; }
    void setMinimizeToTray(bool v);
    bool closeToTray() const { return m_closeToTray; }
    void setCloseToTray(bool v);
    QString downloadDir() const { return m_downloadDir; }
    void setDownloadDir(const QString &v);
    QString language() const { return m_language; }
    void setLanguage(const QString &v);
    QString uiFontFamily() const { return m_uiFontFamily; }
    void setUiFontFamily(const QString &v);
    int uiFontSize() const { return m_uiFontSize; }
    void setUiFontSize(int v);
    QString theme() const { return m_theme; }
    void setTheme(const QString &v);
    QString accentColor() const { return m_accentColor; }
    void setAccentColor(const QString &v);
    bool useMica() const { return m_useMica; }
    void setUseMica(bool v);
    bool enableAnimations() const { return m_enableAnimations; }
    void setEnableAnimations(bool v);
    bool enableCompleteNotification() const { return m_enableCompleteNotification; }
    void setEnableCompleteNotification(bool v);
    bool enableErrorNotification() const { return m_enableErrorNotification; }
    void setEnableErrorNotification(bool v);
    bool notifyOnStart() const { return m_notifyOnStart; }
    void setNotifyOnStart(bool v);
    bool autoPasteClipboard() const { return m_autoPasteClipboard; }
    void setAutoPasteClipboard(bool v);
    bool clipboardMonitor() const { return m_clipboardMonitor; }
    void setClipboardMonitor(bool v);
    bool showTraySpeed() const { return m_showTraySpeed; }
    void setShowTraySpeed(bool v);
    bool browserIntegration() const { return m_browserIntegration; }
    void setBrowserIntegration(bool v);
    int browserPort() const { return m_browserPort; }
    void setBrowserPort(int v);
    bool confirmOnExit() const { return m_confirmOnExit; }
    void setConfirmOnExit(bool v);

    bool showDetailsPanel() const { return m_showDetailsPanel; }
    void setShowDetailsPanel(bool v);
    bool checkForUpdates() const { return m_checkForUpdates; }
    void setCheckForUpdates(bool v);
    bool updateIncludePrerelease() const { return m_updateIncludePrerelease; }
    void setUpdateIncludePrerelease(bool v);
    QString lastNotifiedVersion() const { return m_lastNotifiedVersion; }
    void setLastNotifiedVersion(const QString &v);

    // ===================================================== download behaviour
    Q_PROPERTY(QString userAgent READ userAgent WRITE setUserAgent NOTIFY userAgentChanged)
    Q_PROPERTY(QString referer READ referer WRITE setReferer NOTIFY refererChanged)
    Q_PROPERTY(bool alwaysResume READ alwaysResume WRITE setAlwaysResume NOTIFY alwaysResumeChanged)
    Q_PROPERTY(bool continueDownload READ continueDownload WRITE setContinueDownload NOTIFY continueDownloadChanged)
    Q_PROPERTY(bool remoteTime READ remoteTime WRITE setRemoteTime NOTIFY remoteTimeChanged)
    Q_PROPERTY(bool autoRename READ autoRename WRITE setAutoRename NOTIFY autoRenameChanged)
    Q_PROPERTY(bool allowOverwrite READ allowOverwrite WRITE setAllowOverwrite NOTIFY allowOverwriteChanged)
    Q_PROPERTY(bool contentDispositionDefaultUtf8 READ contentDispositionDefaultUtf8 WRITE setContentDispositionDefaultUtf8 NOTIFY contentDispositionDefaultUtf8Changed)
    Q_PROPERTY(QString fileAllocation READ fileAllocation WRITE setFileAllocation NOTIFY fileAllocationChanged)
    Q_PROPERTY(QString inputFile READ inputFile WRITE setInputFile NOTIFY inputFileChanged)
    Q_PROPERTY(bool checkIntegrity READ checkIntegrity WRITE setCheckIntegrity NOTIFY checkIntegrityChanged)
    Q_PROPERTY(bool realtimeChunkChecksum READ realtimeChunkChecksum WRITE setRealtimeChunkChecksum NOTIFY realtimeChunkChecksumChanged)
    Q_PROPERTY(bool hashCheckOnly READ hashCheckOnly WRITE setHashCheckOnly NOTIFY hashCheckOnlyChanged)
    Q_PROPERTY(bool removeControlFile READ removeControlFile WRITE setRemoveControlFile NOTIFY removeControlFileChanged)
    Q_PROPERTY(bool autoSaveSession READ autoSaveSession WRITE setAutoSaveSession NOTIFY autoSaveSessionChanged)
    Q_PROPERTY(int saveSessionInterval READ saveSessionInterval WRITE setSaveSessionInterval NOTIFY saveSessionIntervalChanged)
    Q_PROPERTY(QString sessionFile READ sessionFile WRITE setSessionFile NOTIFY sessionFileChanged)

    QString userAgent() const { return m_userAgent; }
    void setUserAgent(const QString &v);
    QString referer() const { return m_referer; }
    void setReferer(const QString &v);
    bool alwaysResume() const { return m_alwaysResume; }
    void setAlwaysResume(bool v);
    bool continueDownload() const { return m_continueDownload; }
    void setContinueDownload(bool v);
    bool remoteTime() const { return m_remoteTime; }
    void setRemoteTime(bool v);
    bool autoRename() const { return m_autoRename; }
    void setAutoRename(bool v);
    bool allowOverwrite() const { return m_allowOverwrite; }
    void setAllowOverwrite(bool v);
    bool contentDispositionDefaultUtf8() const { return m_contentDispositionDefaultUtf8; }
    void setContentDispositionDefaultUtf8(bool v);
    QString fileAllocation() const { return m_fileAllocation; }
    void setFileAllocation(const QString &v);
    QString inputFile() const { return m_inputFile; }
    void setInputFile(const QString &v);
    bool checkIntegrity() const { return m_checkIntegrity; }
    void setCheckIntegrity(bool v);
    bool realtimeChunkChecksum() const { return m_realtimeChunkChecksum; }
    void setRealtimeChunkChecksum(bool v);
    bool hashCheckOnly() const { return m_hashCheckOnly; }
    void setHashCheckOnly(bool v);
    bool removeControlFile() const { return m_removeControlFile; }
    void setRemoveControlFile(bool v);
    bool autoSaveSession() const { return m_autoSaveSession; }
    void setAutoSaveSession(bool v);
    int saveSessionInterval() const { return m_saveSessionInterval; }
    void setSaveSessionInterval(int v);
    QString sessionFile() const { return m_sessionFile; }
    void setSessionFile(const QString &v);

    // ============================================================ throughput
    Q_PROPERTY(int maxConcurrentDownloads READ maxConcurrentDownloads WRITE setMaxConcurrentDownloads NOTIFY maxConcurrentDownloadsChanged)
    Q_PROPERTY(int split READ split WRITE setSplit NOTIFY splitChanged)
    Q_PROPERTY(int maxConnectionPerServer READ maxConnectionPerServer WRITE setMaxConnectionPerServer NOTIFY maxConnectionPerServerChanged)
    Q_PROPERTY(QString minSplitSize READ minSplitSize WRITE setMinSplitSize NOTIFY minSplitSizeChanged)
    Q_PROPERTY(QString maxDownloadLimit READ maxDownloadLimit WRITE setMaxDownloadLimit NOTIFY maxDownloadLimitChanged)
    Q_PROPERTY(QString maxUploadLimit READ maxUploadLimit WRITE setMaxUploadLimit NOTIFY maxUploadLimitChanged)
    Q_PROPERTY(int maxOverallDownloadLimitKB READ maxOverallDownloadLimitKB WRITE setMaxOverallDownloadLimitKB NOTIFY maxOverallDownloadLimitKBChanged)
    Q_PROPERTY(int maxOverallUploadLimitKB READ maxOverallUploadLimitKB WRITE setMaxOverallUploadLimitKB NOTIFY maxOverallUploadLimitKBChanged)
    Q_PROPERTY(bool optimizeConcurrentDownloads READ optimizeConcurrentDownloads WRITE setOptimizeConcurrentDownloads NOTIFY optimizeConcurrentDownloadsChanged)
    Q_PROPERTY(bool optimizePieceLength READ optimizePieceLength WRITE setOptimizePieceLength NOTIFY optimizePieceLengthChanged)
    Q_PROPERTY(QString diskCache READ diskCache WRITE setDiskCache NOTIFY diskCacheChanged)
    Q_PROPERTY(bool enableHttpKeepAlive READ enableHttpKeepAlive WRITE setEnableHttpKeepAlive NOTIFY enableHttpKeepAliveChanged)
    Q_PROPERTY(bool enableHttpPipelining READ enableHttpPipelining WRITE setEnableHttpPipelining NOTIFY enableHttpPipeliningChanged)
    Q_PROPERTY(bool noWantDigestHeader READ noWantDigestHeader WRITE setNoWantDigestHeader NOTIFY noWantDigestHeaderChanged)
    Q_PROPERTY(bool conditionalGet READ conditionalGet WRITE setConditionalGet NOTIFY conditionalGetChanged)
    Q_PROPERTY(bool useHead READ useHead WRITE setUseHead NOTIFY useHeadChanged)
    Q_PROPERTY(bool streamPieceSelector READ streamPieceSelector WRITE setStreamPieceSelector NOTIFY streamPieceSelectorChanged)

    int maxConcurrentDownloads() const { return m_maxConcurrentDownloads; }
    void setMaxConcurrentDownloads(int v);
    int split() const { return m_split; }
    void setSplit(int v);
    int maxConnectionPerServer() const { return m_maxConnectionPerServer; }
    void setMaxConnectionPerServer(int v);
    QString minSplitSize() const { return m_minSplitSize; }
    void setMinSplitSize(const QString &v);
    QString maxDownloadLimit() const { return m_maxDownloadLimit; }
    void setMaxDownloadLimit(const QString &v);
    QString maxUploadLimit() const { return m_maxUploadLimit; }
    void setMaxUploadLimit(const QString &v);
    int maxOverallDownloadLimitKB() const { return m_maxOverallDownloadLimitKB; }
    void setMaxOverallDownloadLimitKB(int v);
    int maxOverallUploadLimitKB() const { return m_maxOverallUploadLimitKB; }
    void setMaxOverallUploadLimitKB(int v);
    bool optimizeConcurrentDownloads() const { return m_optimizeConcurrentDownloads; }
    void setOptimizeConcurrentDownloads(bool v);
    bool optimizePieceLength() const { return m_optimizePieceLength; }
    void setOptimizePieceLength(bool v);
    QString diskCache() const { return m_diskCache; }
    void setDiskCache(const QString &v);
    bool enableHttpKeepAlive() const { return m_enableHttpKeepAlive; }
    void setEnableHttpKeepAlive(bool v);
    bool enableHttpPipelining() const { return m_enableHttpPipelining; }
    void setEnableHttpPipelining(bool v);
    bool noWantDigestHeader() const { return m_noWantDigestHeader; }
    void setNoWantDigestHeader(bool v);
    bool conditionalGet() const { return m_conditionalGet; }
    void setConditionalGet(bool v);
    bool useHead() const { return m_useHead; }
    void setUseHead(bool v);
    bool streamPieceSelector() const { return m_streamPieceSelector; }
    void setStreamPieceSelector(bool v);

    // ============================================================ resilience
    Q_PROPERTY(int connectTimeout READ connectTimeout WRITE setConnectTimeout NOTIFY connectTimeoutChanged)
    Q_PROPERTY(int socketTimeout READ socketTimeout WRITE setSocketTimeout NOTIFY socketTimeoutChanged)
    Q_PROPERTY(int timeout READ timeout WRITE setTimeout NOTIFY timeoutChanged)
    Q_PROPERTY(int maxTries READ maxTries WRITE setMaxTries NOTIFY maxTriesChanged)
    Q_PROPERTY(int retryWait READ retryWait WRITE setRetryWait NOTIFY retryWaitChanged)
    Q_PROPERTY(int lowestSpeedLimit READ lowestSpeedLimit WRITE setLowestSpeedLimit NOTIFY lowestSpeedLimitChanged)
    Q_PROPERTY(bool autoFileRenaming READ autoFileRenaming WRITE setAutoFileRenaming NOTIFY autoFileRenamingChanged)
    Q_PROPERTY(bool parameterizedUri READ parameterizedUri WRITE setParameterizedUri NOTIFY parameterizedUriChanged)
    Q_PROPERTY(bool noProxy READ noProxy WRITE setNoProxy NOTIFY noProxyChanged)
    Q_PROPERTY(bool checkCertificate READ checkCertificate WRITE setCheckCertificate NOTIFY checkCertificateChanged)
    Q_PROPERTY(QString caCertificate READ caCertificate WRITE setCaCertificate NOTIFY caCertificateChanged)
    Q_PROPERTY(QString certificate READ certificate WRITE setCertificate NOTIFY certificateChanged)
    Q_PROPERTY(QString privateKey READ privateKey WRITE setPrivateKey NOTIFY privateKeyChanged)
    Q_PROPERTY(QString minTlsVersion READ minTlsVersion WRITE setMinTlsVersion NOTIFY minTlsVersionChanged)

    int connectTimeout() const { return m_connectTimeout; }
    void setConnectTimeout(int v);
    int socketTimeout() const { return m_socketTimeout; }
    void setSocketTimeout(int v);
    int timeout() const { return m_timeout; }
    void setTimeout(int v);
    int maxTries() const { return m_maxTries; }
    void setMaxTries(int v);
    int retryWait() const { return m_retryWait; }
    void setRetryWait(int v);
    int lowestSpeedLimit() const { return m_lowestSpeedLimit; }
    void setLowestSpeedLimit(int v);
    bool autoFileRenaming() const { return m_autoFileRenaming; }
    void setAutoFileRenaming(bool v);
    bool parameterizedUri() const { return m_parameterizedUri; }
    void setParameterizedUri(bool v);
    bool noProxy() const { return m_noProxy; }
    void setNoProxy(bool v);
    bool checkCertificate() const { return m_checkCertificate; }
    void setCheckCertificate(bool v);
    QString caCertificate() const { return m_caCertificate; }
    void setCaCertificate(const QString &v);
    QString certificate() const { return m_certificate; }
    void setCertificate(const QString &v);
    QString privateKey() const { return m_privateKey; }
    void setPrivateKey(const QString &v);
    QString minTlsVersion() const { return m_minTlsVersion; }
    void setMinTlsVersion(const QString &v);

    // ================================================================= proxy
    Q_PROPERTY(QString proxyMode READ proxyMode WRITE setProxyMode NOTIFY proxyModeChanged)
    Q_PROPERTY(QString allProxy READ allProxy WRITE setAllProxy NOTIFY allProxyChanged)
    Q_PROPERTY(QString httpProxy READ httpProxy WRITE setHttpProxy NOTIFY httpProxyChanged)
    Q_PROPERTY(QString httpsProxy READ httpsProxy WRITE setHttpsProxy NOTIFY httpsProxyChanged)
    Q_PROPERTY(QString ftpProxy READ ftpProxy WRITE setFtpProxy NOTIFY ftpProxyChanged)
    Q_PROPERTY(QString allProxyUser READ allProxyUser WRITE setAllProxyUser NOTIFY allProxyUserChanged)
    Q_PROPERTY(QString allProxyPasswd READ allProxyPasswd WRITE setAllProxyPasswd NOTIFY allProxyPasswdChanged)
    Q_PROPERTY(QString noProxyList READ noProxyList WRITE setNoProxyList NOTIFY noProxyListChanged)

    QString proxyMode() const { return m_proxyMode; }
    void setProxyMode(const QString &v);
    QString allProxy() const { return m_allProxy; }
    void setAllProxy(const QString &v);
    QString httpProxy() const { return m_httpProxy; }
    void setHttpProxy(const QString &v);
    QString httpsProxy() const { return m_httpsProxy; }
    void setHttpsProxy(const QString &v);
    QString ftpProxy() const { return m_ftpProxy; }
    void setFtpProxy(const QString &v);
    QString allProxyUser() const { return m_allProxyUser; }
    void setAllProxyUser(const QString &v);
    QString allProxyPasswd() const { return m_allProxyPasswd; }
    void setAllProxyPasswd(const QString &v);
    QString noProxyList() const { return m_noProxyList; }
    void setNoProxyList(const QString &v);

    // ============================================================ BitTorrent
    Q_PROPERTY(int btListenPort READ btListenPort WRITE setBtListenPort NOTIFY btListenPortChanged)
    Q_PROPERTY(int dhtListenPort READ dhtListenPort WRITE setDhtListenPort NOTIFY dhtListenPortChanged)
    Q_PROPERTY(bool enableDht READ enableDht WRITE setEnableDht NOTIFY enableDhtChanged)
    Q_PROPERTY(bool enableDht6 READ enableDht6 WRITE setEnableDht6 NOTIFY enableDht6Changed)
    Q_PROPERTY(bool enableLpd READ enableLpd WRITE setEnableLpd NOTIFY enableLpdChanged)
    Q_PROPERTY(bool btEnableHookAfterCheck READ btEnableHookAfterCheck WRITE setBtEnableHookAfterCheck NOTIFY btEnableHookAfterCheckChanged)
    Q_PROPERTY(bool btRequireCrypto READ btRequireCrypto WRITE setBtRequireCrypto NOTIFY btRequireCryptoChanged)
    Q_PROPERTY(bool btSaveMetadata READ btSaveMetadata WRITE setBtSaveMetadata NOTIFY btSaveMetadataChanged)
    Q_PROPERTY(bool btLoadSavedMetadata READ btLoadSavedMetadata WRITE setBtLoadSavedMetadata NOTIFY btLoadSavedMetadataChanged)
    Q_PROPERTY(bool btDetachSeedOnly READ btDetachSeedOnly WRITE setBtDetachSeedOnly NOTIFY btDetachSeedOnlyChanged)
    Q_PROPERTY(bool btRemoveUnselectedFile READ btRemoveUnselectedFile WRITE setBtRemoveUnselectedFile NOTIFY btRemoveUnselectedFileChanged)
    Q_PROPERTY(bool followTorrent READ followTorrent WRITE setFollowTorrent NOTIFY followTorrentChanged)
    Q_PROPERTY(bool seedUnverified READ seedUnverified WRITE setSeedUnverified NOTIFY seedUnverifiedChanged)
    Q_PROPERTY(int btMaxPeers READ btMaxPeers WRITE setBtMaxPeers NOTIFY btMaxPeersChanged)
    Q_PROPERTY(int btMaxOpenFiles READ btMaxOpenFiles WRITE setBtMaxOpenFiles NOTIFY btMaxOpenFilesChanged)
    Q_PROPERTY(int btRequestTimeout READ btRequestTimeout WRITE setBtRequestTimeout NOTIFY btRequestTimeoutChanged)
    Q_PROPERTY(int btStopTimeout READ btStopTimeout WRITE setBtStopTimeout NOTIFY btStopTimeoutChanged)
    Q_PROPERTY(int btMetadataTimeout READ btMetadataTimeout WRITE setBtMetadataTimeout NOTIFY btMetadataTimeoutChanged)
    Q_PROPERTY(int btTrackerInterval READ btTrackerInterval WRITE setBtTrackerInterval NOTIFY btTrackerIntervalChanged)
    Q_PROPERTY(int btTrackerTimeout READ btTrackerTimeout WRITE setBtTrackerTimeout NOTIFY btTrackerTimeoutChanged)
    Q_PROPERTY(int btTimeout READ btTimeout WRITE setBtTimeout NOTIFY btTimeoutChanged)
    Q_PROPERTY(int dhtEntryPointInterval READ dhtEntryPointInterval WRITE setDhtEntryPointInterval NOTIFY dhtEntryPointIntervalChanged)
    Q_PROPERTY(int dhtMessageTimeout READ dhtMessageTimeout WRITE setDhtMessageTimeout NOTIFY dhtMessageTimeoutChanged)
    Q_PROPERTY(QString btExternalIp READ btExternalIp WRITE setBtExternalIp NOTIFY btExternalIpChanged)
    Q_PROPERTY(QString btTracker READ btTracker WRITE setBtTracker NOTIFY btTrackerChanged)
    Q_PROPERTY(QString dhtEntryPoint READ dhtEntryPoint WRITE setDhtEntryPoint NOTIFY dhtEntryPointChanged)
    Q_PROPERTY(QString dhtEntryPoint6 READ dhtEntryPoint6 WRITE setDhtEntryPoint6 NOTIFY dhtEntryPoint6Changed)
    Q_PROPERTY(QString dhtFilePath READ dhtFilePath WRITE setDhtFilePath NOTIFY dhtFilePathChanged)
    Q_PROPERTY(QString btSaveMetadataFile READ btSaveMetadataFile WRITE setBtSaveMetadataFile NOTIFY btSaveMetadataFileChanged)

    int btListenPort() const { return m_btListenPort; }
    void setBtListenPort(int v);
    int dhtListenPort() const { return m_dhtListenPort; }
    void setDhtListenPort(int v);
    bool enableDht() const { return m_enableDht; }
    void setEnableDht(bool v);
    bool enableDht6() const { return m_enableDht6; }
    void setEnableDht6(bool v);
    bool enableLpd() const { return m_enableLpd; }
    void setEnableLpd(bool v);
    bool btEnableHookAfterCheck() const { return m_btEnableHookAfterCheck; }
    void setBtEnableHookAfterCheck(bool v);
    bool btRequireCrypto() const { return m_btRequireCrypto; }
    void setBtRequireCrypto(bool v);
    bool btSaveMetadata() const { return m_btSaveMetadata; }
    void setBtSaveMetadata(bool v);
    bool btLoadSavedMetadata() const { return m_btLoadSavedMetadata; }
    void setBtLoadSavedMetadata(bool v);
    bool btDetachSeedOnly() const { return m_btDetachSeedOnly; }
    void setBtDetachSeedOnly(bool v);
    bool btRemoveUnselectedFile() const { return m_btRemoveUnselectedFile; }
    void setBtRemoveUnselectedFile(bool v);
    bool followTorrent() const { return m_followTorrent; }
    void setFollowTorrent(bool v);
    bool seedUnverified() const { return m_seedUnverified; }
    void setSeedUnverified(bool v);
    int btMaxPeers() const { return m_btMaxPeers; }
    void setBtMaxPeers(int v);
    int btMaxOpenFiles() const { return m_btMaxOpenFiles; }
    void setBtMaxOpenFiles(int v);
    int btRequestTimeout() const { return m_btRequestTimeout; }
    void setBtRequestTimeout(int v);
    int btStopTimeout() const { return m_btStopTimeout; }
    void setBtStopTimeout(int v);
    int btMetadataTimeout() const { return m_btMetadataTimeout; }
    void setBtMetadataTimeout(int v);
    int btTrackerInterval() const { return m_btTrackerInterval; }
    void setBtTrackerInterval(int v);
    int btTrackerTimeout() const { return m_btTrackerTimeout; }
    void setBtTrackerTimeout(int v);
    int btTimeout() const { return m_btTimeout; }
    void setBtTimeout(int v);
    int dhtEntryPointInterval() const { return m_dhtEntryPointInterval; }
    void setDhtEntryPointInterval(int v);
    int dhtMessageTimeout() const { return m_dhtMessageTimeout; }
    void setDhtMessageTimeout(int v);
    QString btExternalIp() const { return m_btExternalIp; }
    void setBtExternalIp(const QString &v);
    QString btTracker() const { return m_btTracker; }
    void setBtTracker(const QString &v);
    QString dhtEntryPoint() const { return m_dhtEntryPoint; }
    void setDhtEntryPoint(const QString &v);
    QString dhtEntryPoint6() const { return m_dhtEntryPoint6; }
    void setDhtEntryPoint6(const QString &v);
    QString dhtFilePath() const { return m_dhtFilePath; }
    void setDhtFilePath(const QString &v);
    QString btSaveMetadataFile() const { return m_btSaveMetadataFile; }
    void setBtSaveMetadataFile(const QString &v);

    // ================================================================ seeding
    Q_PROPERTY(double seedRatio READ seedRatio WRITE setSeedRatio NOTIFY seedRatioChanged)
    Q_PROPERTY(int seedTime READ seedTime WRITE setSeedTime NOTIFY seedTimeChanged)

    double seedRatio() const { return m_seedRatio; }
    void setSeedRatio(double v);
    int seedTime() const { return m_seedTime; }
    void setSeedTime(int v);

    // ==================================================================== RPC
    Q_PROPERTY(int rpcListenPort READ rpcListenPort WRITE setRpcListenPort NOTIFY rpcListenPortChanged)
    Q_PROPERTY(QString rpcSecret READ rpcSecret WRITE setRpcSecret NOTIFY rpcSecretChanged)
    Q_PROPERTY(bool rpcAllowOriginAll READ rpcAllowOriginAll WRITE setRpcAllowOriginAll NOTIFY rpcAllowOriginAllChanged)
    Q_PROPERTY(bool rpcListenAll READ rpcListenAll WRITE setRpcListenAll NOTIFY rpcListenAllChanged)
    Q_PROPERTY(int rpcMaxRequestSize READ rpcMaxRequestSize WRITE setRpcMaxRequestSize NOTIFY rpcMaxRequestSizeChanged)
    Q_PROPERTY(bool pauseMetadata READ pauseMetadata WRITE setPauseMetadata NOTIFY pauseMetadataChanged)
    Q_PROPERTY(bool keepUnfinishedDownloadResult READ keepUnfinishedDownloadResult WRITE setKeepUnfinishedDownloadResult NOTIFY keepUnfinishedDownloadResultChanged)
    Q_PROPERTY(bool stopWithProcess READ stopWithProcess WRITE setStopWithProcess NOTIFY stopWithProcessChanged)

    int rpcListenPort() const { return m_rpcListenPort; }
    void setRpcListenPort(int v);
    QString rpcSecret() const { return m_rpcSecret; }
    void setRpcSecret(const QString &v);
    bool rpcAllowOriginAll() const { return m_rpcAllowOriginAll; }
    void setRpcAllowOriginAll(bool v);
    bool rpcListenAll() const { return m_rpcListenAll; }
    void setRpcListenAll(bool v);
    int rpcMaxRequestSize() const { return m_rpcMaxRequestSize; }
    void setRpcMaxRequestSize(int v);
    bool pauseMetadata() const { return m_pauseMetadata; }
    void setPauseMetadata(bool v);
    bool keepUnfinishedDownloadResult() const { return m_keepUnfinishedDownloadResult; }
    void setKeepUnfinishedDownloadResult(bool v);
    bool stopWithProcess() const { return m_stopWithProcess; }
    void setStopWithProcess(bool v);

    // =============================================================== advanced
    Q_PROPERTY(bool enableRpcConsole READ enableRpcConsole WRITE setEnableRpcConsole NOTIFY enableRpcConsoleChanged)
    Q_PROPERTY(bool enableEngineLog READ enableEngineLog WRITE setEnableEngineLog NOTIFY enableEngineLogChanged)
    Q_PROPERTY(bool advancedUser READ advancedUser WRITE setAdvancedUser NOTIFY advancedUserChanged)
    Q_PROPERTY(bool enableSqliteHistory READ enableSqliteHistory WRITE setEnableSqliteHistory NOTIFY enableSqliteHistoryChanged)
    Q_PROPERTY(QString sqliteDbPath READ sqliteDbPath WRITE setSqliteDbPath NOTIFY sqliteDbPathChanged)
    Q_PROPERTY(int historyKeepEntries READ historyKeepEntries WRITE setHistoryKeepEntries NOTIFY historyKeepEntriesChanged)
    Q_PROPERTY(QString extraAria2Args READ extraAria2Args WRITE setExtraAria2Args NOTIFY extraAria2ArgsChanged)
    Q_PROPERTY(QString configFilePath READ configFilePath WRITE setConfigFilePath NOTIFY configFilePathChanged)
    Q_PROPERTY(QString aria2Executable READ aria2Executable WRITE setAria2Executable NOTIFY aria2ExecutableChanged)
    Q_PROPERTY(bool autoRestartEngine READ autoRestartEngine WRITE setAutoRestartEngine NOTIFY autoRestartEngineChanged)

    bool enableRpcConsole() const { return m_enableRpcConsole; }
    void setEnableRpcConsole(bool v);
    bool enableEngineLog() const { return m_enableEngineLog; }
    void setEnableEngineLog(bool v);
    bool advancedUser() const { return m_advancedUser; }
    void setAdvancedUser(bool v);
    bool enableSqliteHistory() const { return m_enableSqliteHistory; }
    void setEnableSqliteHistory(bool v);
    QString sqliteDbPath() const { return m_sqliteDbPath; }
    void setSqliteDbPath(const QString &v);
    int historyKeepEntries() const { return m_historyKeepEntries; }
    void setHistoryKeepEntries(int v);
    QString extraAria2Args() const { return m_extraAria2Args; }
    void setExtraAria2Args(const QString &v);
    QString configFilePath() const { return m_configFilePath; }
    void setConfigFilePath(const QString &v);
    QString aria2Executable() const { return m_aria2Executable; }
    void setAria2Executable(const QString &v);
    bool autoRestartEngine() const { return m_autoRestartEngine; }
    void setAutoRestartEngine(bool v);

    // ============================================================= scheduling
    Q_PROPERTY(bool schedulerEnabled READ schedulerEnabled WRITE setSchedulerEnabled NOTIFY schedulerEnabledChanged)
    Q_PROPERTY(QString scheduleStart READ scheduleStart WRITE setScheduleStart NOTIFY scheduleStartChanged)
    Q_PROPERTY(QString scheduleStop READ scheduleStop WRITE setScheduleStop NOTIFY scheduleStopChanged)
    Q_PROPERTY(int scheduledDownloadLimitKB READ scheduledDownloadLimitKB WRITE setScheduledDownloadLimitKB NOTIFY scheduledDownloadLimitKBChanged)
    Q_PROPERTY(int scheduledUploadLimitKB READ scheduledUploadLimitKB WRITE setScheduledUploadLimitKB NOTIFY scheduledUploadLimitKBChanged)

    bool schedulerEnabled() const { return m_schedulerEnabled; }
    void setSchedulerEnabled(bool v);
    QString scheduleStart() const { return m_scheduleStart; }
    void setScheduleStart(const QString &v);
    QString scheduleStop() const { return m_scheduleStop; }
    void setScheduleStop(const QString &v);
    int scheduledDownloadLimitKB() const { return m_scheduledDownloadLimitKB; }
    void setScheduledDownloadLimitKB(int v);
    int scheduledUploadLimitKB() const { return m_scheduledUploadLimitKB; }
    void setScheduledUploadLimitKB(int v);

signals:
    void autoStartChanged();
    void startMinimizedChanged();
    void minimizeToTrayChanged();
    void closeToTrayChanged();
    void downloadDirChanged();
    void languageChanged();
    void uiFontFamilyChanged();
    void uiFontSizeChanged();
    void themeChanged();
    void accentColorChanged();
    void useMicaChanged();
    void enableAnimationsChanged();
    void enableCompleteNotificationChanged();
    void enableErrorNotificationChanged();
    void notifyOnStartChanged();
    void autoPasteClipboardChanged();
    void clipboardMonitorChanged();
    void showTraySpeedChanged();
    void browserIntegrationChanged();
    void browserPortChanged();
    void confirmOnExitChanged();
    void showDetailsPanelChanged();
    void checkForUpdatesChanged();
    void updateIncludePrereleaseChanged();
    void lastNotifiedVersionChanged();

    void userAgentChanged();
    void refererChanged();
    void alwaysResumeChanged();
    void continueDownloadChanged();
    void remoteTimeChanged();
    void autoRenameChanged();
    void allowOverwriteChanged();
    void contentDispositionDefaultUtf8Changed();
    void fileAllocationChanged();
    void inputFileChanged();
    void checkIntegrityChanged();
    void realtimeChunkChecksumChanged();
    void hashCheckOnlyChanged();
    void removeControlFileChanged();
    void autoSaveSessionChanged();
    void saveSessionIntervalChanged();
    void sessionFileChanged();

    void maxConcurrentDownloadsChanged();
    void splitChanged();
    void maxConnectionPerServerChanged();
    void minSplitSizeChanged();
    void maxDownloadLimitChanged();
    void maxUploadLimitChanged();
    void maxOverallDownloadLimitKBChanged();
    void maxOverallUploadLimitKBChanged();
    void optimizeConcurrentDownloadsChanged();
    void optimizePieceLengthChanged();
    void diskCacheChanged();
    void enableHttpKeepAliveChanged();
    void enableHttpPipeliningChanged();
    void noWantDigestHeaderChanged();
    void conditionalGetChanged();
    void useHeadChanged();
    void streamPieceSelectorChanged();

    void connectTimeoutChanged();
    void socketTimeoutChanged();
    void timeoutChanged();
    void maxTriesChanged();
    void retryWaitChanged();
    void lowestSpeedLimitChanged();
    void autoFileRenamingChanged();
    void parameterizedUriChanged();
    void noProxyChanged();
    void checkCertificateChanged();
    void caCertificateChanged();
    void certificateChanged();
    void privateKeyChanged();
    void minTlsVersionChanged();

    void proxyModeChanged();
    void allProxyChanged();
    void httpProxyChanged();
    void httpsProxyChanged();
    void ftpProxyChanged();
    void allProxyUserChanged();
    void allProxyPasswdChanged();
    void noProxyListChanged();

    void btListenPortChanged();
    void dhtListenPortChanged();
    void enableDhtChanged();
    void enableDht6Changed();
    void enableLpdChanged();
    void btEnableHookAfterCheckChanged();
    void btRequireCryptoChanged();
    void btSaveMetadataChanged();
    void btLoadSavedMetadataChanged();
    void btDetachSeedOnlyChanged();
    void btRemoveUnselectedFileChanged();
    void followTorrentChanged();
    void seedUnverifiedChanged();
    void btMaxPeersChanged();
    void btMaxOpenFilesChanged();
    void btRequestTimeoutChanged();
    void btStopTimeoutChanged();
    void btMetadataTimeoutChanged();
    void btTrackerIntervalChanged();
    void btTrackerTimeoutChanged();
    void btTimeoutChanged();
    void dhtEntryPointIntervalChanged();
    void dhtMessageTimeoutChanged();
    void btExternalIpChanged();
    void btTrackerChanged();
    void dhtEntryPointChanged();
    void dhtEntryPoint6Changed();
    void dhtFilePathChanged();
    void btSaveMetadataFileChanged();

    void seedRatioChanged();
    void seedTimeChanged();

    void rpcListenPortChanged();
    void rpcSecretChanged();
    void rpcAllowOriginAllChanged();
    void rpcListenAllChanged();
    void rpcMaxRequestSizeChanged();
    void pauseMetadataChanged();
    void keepUnfinishedDownloadResultChanged();
    void stopWithProcessChanged();

    void enableRpcConsoleChanged();
    void enableEngineLogChanged();
    void advancedUserChanged();
    void enableSqliteHistoryChanged();
    void sqliteDbPathChanged();
    void historyKeepEntriesChanged();
    void extraAria2ArgsChanged();
    void configFilePathChanged();
    void aria2ExecutableChanged();
    void autoRestartEngineChanged();

    void schedulerEnabledChanged();
    void scheduleStartChanged();
    void scheduleStopChanged();
    void scheduledDownloadLimitKBChanged();
    void scheduledUploadLimitKBChanged();

    /// Any value consumed by aria2 changed (may require a restart).
    void aria2SettingsChanged();
    /// Only the runtime-changeable subset changed - no restart needed.
    void runtimeOptionsChanged();
    /// The generic "something changed" signal used by the QML theme.
    void settingsRevisionChanged();

private:
    void loadSettings();
    void getRuntimeOptionsReady();
    void applyAutoStart(bool enable);
    void touch(const char *key, bool runtimeOnly = false);
    void bumpRevision();

    QSettings m_settings;
    int m_settingsRevision = 0;

    bool m_autoStart = false;
    bool m_startMinimized = false;
    bool m_minimizeToTray = true;
    bool m_closeToTray = true;
    QString m_downloadDir;
    QString m_language;
    QString m_uiFontFamily;
    int m_uiFontSize = 14;
    QString m_theme = QStringLiteral("dark");
    QString m_accentColor;
    bool m_useMica = true;
    bool m_enableAnimations = true;
    bool m_enableCompleteNotification = true;
    bool m_enableErrorNotification = true;
    bool m_notifyOnStart = false;
    bool m_autoPasteClipboard = true;
    bool m_clipboardMonitor = false;
    bool m_showTraySpeed = true;
    bool m_browserIntegration = true;
    int m_browserPort = 8899;
    bool m_confirmOnExit = false;
    bool m_showDetailsPanel = true;
    bool m_checkForUpdates = true;
    bool m_updateIncludePrerelease = false;
    QString m_lastNotifiedVersion;

    QString m_userAgent;
    QString m_referer;
    bool m_alwaysResume = false;
    bool m_continueDownload = true;
    bool m_remoteTime = false;
    bool m_autoRename = true;
    bool m_allowOverwrite = true;
    bool m_contentDispositionDefaultUtf8 = true;
    QString m_fileAllocation = QStringLiteral("prealloc");
    QString m_inputFile;
    bool m_checkIntegrity = false;
    bool m_realtimeChunkChecksum = true;
    bool m_hashCheckOnly = false;
    bool m_removeControlFile = true;
    bool m_autoSaveSession = true;
    int m_saveSessionInterval = 60;
    QString m_sessionFile;

    int m_maxConcurrentDownloads = 5;
    int m_split = 16;
    int m_maxConnectionPerServer = 16;
    QString m_minSplitSize = QStringLiteral("1M");
    QString m_maxDownloadLimit;
    QString m_maxUploadLimit;
    int m_maxOverallDownloadLimitKB = 0;
    int m_maxOverallUploadLimitKB = 0;
    bool m_optimizeConcurrentDownloads = true;
    bool m_optimizePieceLength = true;
    QString m_diskCache = QStringLiteral("64M");
    bool m_enableHttpKeepAlive = true;
    bool m_enableHttpPipelining = false;
    bool m_noWantDigestHeader = true;
    bool m_conditionalGet = false;
    bool m_useHead = false;
    bool m_streamPieceSelector = true;

    int m_connectTimeout = 30;
    int m_socketTimeout = 60;
    int m_timeout = 60;
    int m_maxTries = 5;
    int m_retryWait = 3;
    int m_lowestSpeedLimit = 0;
    bool m_autoFileRenaming = true;
    bool m_parameterizedUri = true;
    bool m_noProxy = true;
    bool m_checkCertificate = true;
    QString m_caCertificate;
    QString m_certificate;
    QString m_privateKey;
    QString m_minTlsVersion = QStringLiteral("TLSv1.2");

    QString m_proxyMode = QStringLiteral("system");
    QString m_allProxy;
    QString m_httpProxy;
    QString m_httpsProxy;
    QString m_ftpProxy;
    QString m_allProxyUser;
    QString m_allProxyPasswd;
    QString m_noProxyList = QStringLiteral("localhost,127.0.0.1");

    int m_btListenPort = 6881;
    int m_dhtListenPort = 6881;
    bool m_enableDht = true;
    bool m_enableDht6 = false;
    bool m_enableLpd = true;
    bool m_btEnableHookAfterCheck = false;
    bool m_btRequireCrypto = false;
    bool m_btSaveMetadata = true;
    bool m_btLoadSavedMetadata = true;
    bool m_btDetachSeedOnly = false;
    bool m_btRemoveUnselectedFile = false;
    bool m_followTorrent = true;
    bool m_seedUnverified = false;
    int m_btMaxPeers = 55;
    int m_btMaxOpenFiles = 100;
    int m_btRequestTimeout = 60;
    int m_btStopTimeout = 0;
    int m_btMetadataTimeout = 60;
    int m_btTrackerInterval = 0;
    int m_btTrackerTimeout = 60;
    int m_btTimeout = 0;
    int m_dhtEntryPointInterval = 0;
    int m_dhtMessageTimeout = 10;
    QString m_btExternalIp;
    QString m_btTracker;
    QString m_dhtEntryPoint;
    QString m_dhtEntryPoint6;
    QString m_dhtFilePath;
    QString m_btSaveMetadataFile;

    double m_seedRatio = 1.0;
    int m_seedTime = 0;

    int m_rpcListenPort = 6800;
    QString m_rpcSecret;
    bool m_rpcAllowOriginAll = true;
    bool m_rpcListenAll = false;
    int m_rpcMaxRequestSize = 32 * 1024 * 1024;
    bool m_pauseMetadata = false;
    bool m_keepUnfinishedDownloadResult = false;
    bool m_stopWithProcess = false;

    bool m_enableRpcConsole = false;
    bool m_enableEngineLog = false;
    bool m_advancedUser = false;
    bool m_enableSqliteHistory = true;
    QString m_sqliteDbPath;
    int m_historyKeepEntries = 2000;
    QString m_extraAria2Args;
    QString m_configFilePath;
    QString m_aria2Executable;
    bool m_autoRestartEngine = true;

    bool m_schedulerEnabled = false;
    QString m_scheduleStart = QStringLiteral("09:00");
    QString m_scheduleStop = QStringLiteral("23:00");
    int m_scheduledDownloadLimitKB = 0;
    int m_scheduledUploadLimitKB = 0;
};

#endif // SETTINGSMANAGER_H
