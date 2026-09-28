#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;
class QJsonObject;

/**
 * UpdateChecker - asks GitHub whether a newer release exists.
 *
 * Fetchora is distributed through GitHub Releases, so the release list *is* the
 * update feed: no server of our own, and the same place a user would go to
 * download it by hand.
 *
 * What this deliberately does NOT do:
 *
 *   * It does not install anything by itself. On Windows it can download the
 *     installer the release carries and hand it to the OS, but only after the
 *     user asks, and the installer then shows its own progress and its own
 *     questions.
 *   * It does not verify a signature, because the releases are not signed. What
 *     HTTPS to api.github.com buys is that the answer came from GitHub, not that
 *     the binary was built by us; there is no key here that could prove more, so
 *     nothing pretends otherwise. The README says the same thing in the same
 *     words.
 *
 * The class is framework-agnostic on purpose - it knows nothing about widgets,
 * so the About page, a startup check and the `--check-updates` CLI mode all use
 * the same implementation.
 */
class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    /// One release as GitHub describes it.
    struct Release {
        QString tagName;        ///< "v0.1.5"
        QString version;        ///< "0.1.5" - the tag without its leading v
        QString name;           ///< the release's title
        QString notes;          ///< the markdown body
        QString pageUrl;        ///< html_url
        QDateTime publishedAt;
        bool prerelease = false;
        /// asset file name -> download URL
        QList<QPair<QString, QUrl>> assets;

        bool isValid() const { return !version.isEmpty(); }
        /// The asset this platform would install, or an empty URL.
        QUrl preferredAsset() const;
    };

    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    /// The running version, as the application reports it.
    static QString currentVersion();

    /// Repository the releases come from.
    static QString repository();
    static QUrl releasesApiUrl();
    static QUrl releasesPageUrl();

    /**
     * Compare two dotted versions. Returns <0, 0 or >0.
     *
     * Tolerant on purpose: tags arrive as "v0.1.4", "0.1.4" or "0.1.4-beta.1",
     * and a build could carry a suffix of its own. A trailing "-something" sorts
     * *below* the same version without it, which is what every other tool does.
     */
    static int compareVersions(const QString &a, const QString &b);

    /// Fetch the release list and work out whether there is something newer.
    /// `includePrerelease` also considers GitHub's pre-releases.
    void check(bool includePrerelease = false);

    bool isBusy() const { return m_reply != nullptr || m_download != nullptr; }
    Release latest() const { return m_latest; }

    /// Download an asset to a temporary file; emits downloadProgress().
    void download(const QUrl &asset, const QString &fileName);
    /// Run a downloaded installer and let it take over.
    static bool launchInstaller(const QString &path);
    /// Open a URL in the user's browser (the release page, a download link).
    static bool openInBrowser(const QUrl &url);

signals:
    /// Sent for every completed check, whether or not something is available.
    void checkFinished(const UpdateChecker::Release &release, bool updateAvailable);
    void checkFailed(const QString &reason);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString &path);
    void downloadFailed(const QString &reason);

private:
    void onRepliesFinished();
    static Release parseRelease(const QJsonObject &object);

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QNetworkReply *m_download = nullptr;
    QFile *m_downloadFile = nullptr;
    Release m_latest;
    bool m_includePrerelease = false;
    QString m_downloadName;
};

#endif // UPDATECHECKER_H
