#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>

namespace {

const char *kOwner = "aimineng";
const char *kRepo = "Fetchora";

/// Splits "v0.1.4-beta.1" into {0,1,4} and "beta.1".
void splitVersion(const QString &raw, QList<int> *numbers, QString *suffix)
{
    QString text = raw.trimmed();
    if (text.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        text.remove(0, 1);
    // A build could carry its own "+meta" or "-suffix"; neither is part of the
    // ordering except that its presence makes the version a pre-release.
    const int dash = text.indexOf(QRegularExpression(QStringLiteral("[-+]")));
    if (dash >= 0) {
        *suffix = text.mid(dash + 1);
        text = text.left(dash);
    }
    const QStringList parts = text.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        // "4rc1" -> 4, and anything non-numeric counts as 0 rather than throwing
        // the whole comparison away.
        static const QRegularExpression leadingDigits(QStringLiteral("^(\\d+)"));
        const QRegularExpressionMatch match = leadingDigits.match(part);
        numbers->append(match.hasMatch() ? match.captured(1).toInt() : 0);
    }
}

/// Which asset a user on this platform should be offered.
///
/// Ranked rather than just matched: a release carries both the installer and the
/// portable zip on Windows, and which one comes back first from the API is not a
/// promise. 0 is the first choice, -1 means "not for this platform" - so a
/// release that only has the zip still offers something instead of nothing.
int assetPreference(const QString &name)
{
#if defined(Q_OS_WIN)
    if (name.endsWith(QLatin1String("-windows-x64-setup.exe"), Qt::CaseInsensitive))
        return 0;
    if (name.endsWith(QLatin1String("-windows-x64.zip"), Qt::CaseInsensitive))
        return 1;
#elif defined(Q_OS_MACOS)
    if (name.endsWith(QLatin1String(".dmg"), Qt::CaseInsensitive))
        return 0;
#else
    if (name.endsWith(QLatin1String(".tar.gz"), Qt::CaseInsensitive))
        return 0;
#endif
    return -1;
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

UpdateChecker::~UpdateChecker()
{
    if (m_reply)
        m_reply->abort();
    if (m_download)
        m_download->abort();
}

QString UpdateChecker::currentVersion()
{
    return QCoreApplication::applicationVersion();
}

QString UpdateChecker::repository()
{
    return QStringLiteral("%1/%2").arg(QString::fromLatin1(kOwner), QString::fromLatin1(kRepo));
}

QUrl UpdateChecker::releasesApiUrl()
{
    // The list endpoint rather than /releases/latest: it is the only one that
    // can also answer "is there a pre-release newer than what I have", and it
    // gives the asset list in the same round trip.
    QUrl url(QStringLiteral("https://api.github.com/repos/%1/releases").arg(repository()));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("per_page"), QStringLiteral("15"));
    url.setQuery(query);
    return url;
}

QUrl UpdateChecker::releasesPageUrl()
{
    return QUrl(QStringLiteral("https://github.com/%1/releases").arg(repository()));
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    QList<int> left;
    QList<int> right;
    QString leftSuffix;
    QString rightSuffix;
    splitVersion(a, &left, &leftSuffix);
    splitVersion(b, &right, &rightSuffix);

    const int count = qMax(left.size(), right.size());
    for (int i = 0; i < count; ++i) {
        const int l = i < left.size() ? left.at(i) : 0;
        const int r = i < right.size() ? right.at(i) : 0;
        if (l != r)
            return l < r ? -1 : 1;
    }
    // Same numbers: a pre-release sorts below the release it precedes, which is
    // why 0.2.0-beta is not offered to somebody running 0.2.0.
    if (leftSuffix.isEmpty() != rightSuffix.isEmpty())
        return leftSuffix.isEmpty() ? 1 : -1;
    return QString::compare(leftSuffix, rightSuffix);
}

QUrl UpdateChecker::Release::preferredAsset() const
{
    QUrl best;
    int bestRank = -1;
    for (const QPair<QString, QUrl> &asset : assets) {
        const int rank = assetPreference(asset.first);
        if (rank < 0)
            continue;
        if (bestRank < 0 || rank < bestRank) {
            bestRank = rank;
            best = asset.second;
        }
    }
    return best;
}

UpdateChecker::Release UpdateChecker::parseRelease(const QJsonObject &object)
{
    Release release;
    release.tagName = object.value(QStringLiteral("tag_name")).toString();
    release.name = object.value(QStringLiteral("name")).toString();
    release.notes = object.value(QStringLiteral("body")).toString();
    release.pageUrl = object.value(QStringLiteral("html_url")).toString();
    release.prerelease = object.value(QStringLiteral("prerelease")).toBool();
    release.publishedAt = QDateTime::fromString(
        object.value(QStringLiteral("published_at")).toString(), Qt::ISODate);

    release.version = release.tagName;
    if (release.version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        release.version.remove(0, 1);

    const QJsonArray assets = object.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        const QString url = asset.value(QStringLiteral("browser_download_url")).toString();
        if (!name.isEmpty() && !url.isEmpty())
            release.assets.append({name, QUrl(url)});
    }
    return release;
}

void UpdateChecker::check(bool includePrerelease)
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_includePrerelease = includePrerelease;

    QNetworkRequest request(releasesApiUrl());
    // GitHub rejects requests without a User-Agent outright.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Fetchora/%1").arg(currentVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // A hung connection must not leave the UI saying "checking" forever.
    request.setTransferTimeout(15000);

    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::onRepliesFinished);
}

void UpdateChecker::onRepliesFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    if (reply == m_reply)
        m_reply = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString reason = reply->errorString();
        if (status == 403) {
            reason = tr("GitHub 拒绝了这次请求（可能是匿名调用次数用尽），稍后再试。");
        } else if (status == 404) {
            reason = tr("找不到仓库 %1 的发布列表。").arg(repository());
        }
        emit checkFailed(reason);
        return;
    }

    const QByteArray payload = reply->readAll();
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        emit checkFailed(tr("无法解析 GitHub 返回的数据：%1").arg(parseError.errorString()));
        return;
    }

    Release best;
    const QJsonArray releases = document.array();
    for (const QJsonValue &value : releases) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("draft")).toBool())
            continue;
        const Release candidate = parseRelease(object);
        if (!candidate.isValid())
            continue;
        // A pre-release is only a candidate when the user asked for one. It is
        // skipped rather than treated as older, so a 0.2.0-beta never hides the
        // 0.1.4 someone is actually running.
        if (candidate.prerelease && !m_includePrerelease)
            continue;
        if (!best.isValid() || compareVersions(candidate.version, best.version) > 0)
            best = candidate;
    }

    m_latest = best;
    const bool available = best.isValid() && compareVersions(best.version, currentVersion()) > 0;
    emit checkFinished(best, available);
}

// ---------------------------------------------------------------------------
//  Downloading the installer
// ---------------------------------------------------------------------------
void UpdateChecker::download(const QUrl &asset, const QString &fileName)
{
    if (m_download) {
        m_download->abort();
        m_download->deleteLater();
        m_download = nullptr;
    }
    if (m_downloadFile) {
        m_downloadFile->close();
        m_downloadFile->deleteLater();
        m_downloadFile = nullptr;
    }

    // A dedicated directory, not the user's Downloads folder: this is an
    // installer we are about to run, and it should be obvious where it came
    // from and easy to clean up.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/Fetchora/updates");
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + fileName;

    m_downloadFile = new QFile(path, this);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString reason = m_downloadFile->errorString();
        m_downloadFile->deleteLater();
        m_downloadFile = nullptr;
        emit downloadFailed(tr("无法写入 %1：%2").arg(path, reason));
        return;
    }
    m_downloadName = path;

    QNetworkRequest request(asset);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Fetchora/%1").arg(currentVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);

    m_download = m_network->get(request);
    connect(m_download, &QNetworkReply::readyRead, this, [this]() {
        if (m_download && m_downloadFile)
            m_downloadFile->write(m_download->readAll());
    });
    connect(m_download, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { emit downloadProgress(received, total); });
    connect(m_download, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_download;
        if (!reply)
            return;
        m_download = nullptr;
        reply->deleteLater();

        const QString path = m_downloadName;
        if (m_downloadFile) {
            m_downloadFile->write(reply->readAll());
            m_downloadFile->close();
            m_downloadFile->deleteLater();
            m_downloadFile = nullptr;
        }

        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(path);
            emit downloadFailed(reply->errorString());
            return;
        }
        // An installer is tens of megabytes; a few kilobytes means we were
        // handed an error page, not the file.
        if (QFileInfo(path).size() < 1024 * 1024) {
            QFile::remove(path);
            emit downloadFailed(tr("下载到的文件不完整（只有 %1 字节）。")
                                    .arg(QFileInfo(path).size()));
            return;
        }
        emit downloadFinished(path);
    });
}

bool UpdateChecker::launchInstaller(const QString &path)
{
    if (!QFile::exists(path))
        return false;
#if defined(Q_OS_WIN)
    // The installer is an Inno Setup package: it closes the running instance,
    // replaces the files and offers to restart the app itself, so there is
    // nothing useful for us to do afterwards but get out of the way.
    return QProcess::startDetached(path, QStringList());
#elif defined(Q_OS_MACOS)
    // A .dmg is mounted, not executed; let the Finder do what the user expects.
    return QProcess::startDetached(QStringLiteral("/usr/bin/open"), {path});
#else
    Q_UNUSED(path)
    return false; // the Linux tarball is unpacked by hand; open the page instead
#endif
}

bool UpdateChecker::openInBrowser(const QUrl &url)
{
    return QDesktopServices::openUrl(url);
}
