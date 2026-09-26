#include "Aria2Manager.h"
#include "ClipboardHelper.h"
#include "Logger.h"
#include "NotificationManager.h"
#include "SettingsManager.h"
#include "TorrentUtils.h"
#include "UpdateChecker.h"

#include "ui/FluentButton.h"
#include "ui/FluentInputs.h"
#include "ui/FluentMainWindow.h"
#include "ui/FluentNavigationView.h"
#include "ui/FluentTheme.h"
#include "ui/FluentTitleBar.h"
#include "ui/FluentWidgets.h"
#include "ui/LanguageManager.h"
#include "ui/pages/AboutPage.h"
#include "ui/pages/BitTorrentPage.h"
#include "ui/pages/CreateTorrentPage.h"
#include "ui/pages/DownloadsPage.h"
#include "ui/pages/HistoryPage.h"
#include "ui/pages/NewTaskDialog.h"
#include "ui/pages/SettingsPage.h"
#include "ui/pages/TaskDetailsPanel.h"

#include <QActionGroup>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSharedPointer>
#include <QSplitter>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

namespace {

/// Base name of the single-instance IPC endpoint. The key actually used is the
/// per-user one built by singleInstanceKey(): QLocalServer's endpoint lives in a
/// directory every user can reach (on Unix that is QDir::tempPath()), so a fixed
/// name would let a second user's launch collide with the first user's running
/// instance - and its URLs would then be forwarded into someone else's session.
const char *kSingleInstanceKey = "Fetchora.SingleInstance.v1";
const char *kAppName = "Fetchora";

/// Stable per-user, per-platform endpoint name. The user name keeps two
/// accounts on one machine apart; the short hash of the home directory keeps
/// the name unique when the user name alone is ambiguous and keeps the socket
/// path well under the ~100 character sun_path limit on macOS/BSD. It is
/// stable across restarts, which is what the single-instance handshake needs.
QString singleInstanceKey()
{
    const QString user = qEnvironmentVariable("USER");
    const QString name = qEnvironmentVariable("USERNAME");
    const QString who = !user.isEmpty() ? user : (!name.isEmpty() ? name : QStringLiteral("unknown"));
    const quint32 hash = qHash(QDir::homePath(), 0x5f3a91u);
    return QStringLiteral("%1.%2.%3")
        .arg(QString::fromLatin1(kSingleInstanceKey), who)
        .arg(hash, 8, 16, QLatin1Char('0'));
}

/// Draw the application icon at runtime so the build carries no image assets
/// beyond the ones make-icons.ps1 generates for the executable.
QPixmap makeIconPixmap(int size, const QColor &fg, const QColor &base)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient gradient(0, 0, 0, size);
    gradient.setColorAt(0.0, base.lighter(135));
    gradient.setColorAt(1.0, base.darker(115));
    p.setBrush(gradient);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(0, 0, size, size), size * 0.24, size * 0.24);

    QPainterPath arrow;
    const qreal cx = size / 2.0;
    const qreal stemBottom = size * 0.52;
    const qreal stemHalf = size * 0.075;
    arrow.moveTo(cx - stemHalf, size * 0.22);
    arrow.lineTo(cx + stemHalf, size * 0.22);
    arrow.lineTo(cx + stemHalf, stemBottom);
    arrow.lineTo(cx + size * 0.22, stemBottom);
    arrow.lineTo(cx, size * 0.74);
    arrow.lineTo(cx - size * 0.22, stemBottom);
    arrow.lineTo(cx - stemHalf, stemBottom);
    arrow.closeSubpath();
    p.setBrush(fg);
    p.drawPath(arrow);
    p.drawRoundedRect(QRectF(size * 0.24, size * 0.80, size * 0.52, size * 0.075),
                      size * 0.037, size * 0.037);
    p.end();
    return pixmap;
}

QIcon makeAppIcon()
{
    QIcon icon;
    for (int size : {16, 20, 24, 32, 48, 64, 128, 256})
        icon.addPixmap(makeIconPixmap(size, QColor(Qt::white), QColor(0x0F, 0x6C, 0xBD)));
    return icon;
}

bool notifyRunningInstance(const QStringList &urls, const QString &workingDir)
{
    QLocalSocket socket;
    socket.connectToServer(singleInstanceKey());
    if (!socket.waitForConnected(300))
        return false;

    QByteArray payload;
    for (const QString &u : urls)
        payload += u.toUtf8() + '\n';
    if (!workingDir.isEmpty())
        payload += "cwd:" + workingDir.toUtf8() + '\n';

    socket.write("WAKE\n");
    socket.write(payload);
    socket.flush();
    socket.waitForBytesWritten(300);
    socket.disconnectFromServer();
    return true;
}

// ---------------------------------------------------------------------------
//  Headless command line tools
//
//  These exist so `build.ps1 -Test` can validate the two things that are easy
//  to get silently wrong: an aria2c switch that the bundled engine rejects (the
//  engine then exits with code 28 and never comes up) and a malformed bencode
//  output from the torrent maker (unreadable by every client). They print to
//  stdout and never create a window.
// ---------------------------------------------------------------------------
QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

/// Every long option the bundled aria2c understands, taken from --help=#all.
QSet<QString> aria2KnownOptions(const QString &executable)
{
    QSet<QString> known;
    QProcess process;
    process.start(executable, {QStringLiteral("--help=#all")});
    if (!process.waitForFinished(20000))
        return known;
    const QString help = QString::fromUtf8(process.readAllStandardOutput());
    static const QRegularExpression optionPattern(QStringLiteral("--([a-zA-Z0-9][a-zA-Z0-9-]*)"));
    auto it = optionPattern.globalMatch(help);
    while (it.hasNext())
        known.insert(it.next().captured(1));
    return known;
}

/**
 * The update feature's deterministic half: how versions are ordered, which asset
 * a platform is offered, and what counts as an update at all.
 *
 * No network here on purpose - `--check-updates` is the mode that talks to
 * GitHub. Keeping the logic testable without a server is what lets every CI job
 * on every platform cover it, including the platforms that ship no installer.
 *
 * Exit code 0 when every check passed, 5 otherwise.
 */
int runUpdateSelfTest(SettingsManager &settings)
{
    out() << "update self-test\n";
    int failures = 0;
    // The exact magnitude of compareVersions() is not part of its contract, only
    // the sign, so the checks below compare signs.
    const auto check = [&failures](const QString &what, bool ok, const QString &detail) {
        out() << (ok ? "  ok   " : "  FAIL ") << what;
        if (!detail.isEmpty())
            out() << "  (" << detail << ')';
        out() << '\n';
        if (!ok)
            ++failures;
    };

    // ---- version ordering -------------------------------------------------
    struct Comparison {
        const char *left;
        const char *right;
        int sign; ///< -1 older, 0 same, 1 newer
    };
    // The cases a real release list produces: tags with and without a leading v,
    // pre-releases, and versions where lexicographic ordering would be wrong.
    const Comparison comparisons[] = {
        {"0.1.4", "0.1.4", 0},
        {"v0.1.4", "0.1.4", 0},
        {"V0.1.4", "v0.1.4", 0},
        {"0.1.5", "0.1.4", 1},
        {"0.1.4", "0.1.5", -1},
        {"0.1.10", "0.1.9", 1},   // as text "0.1.10" < "0.1.9"; as a version it is newer
        {"1.0", "0.99.99", 1},
        {"0.1.4", "0.2", -1},
        {"0.1.4-beta.1", "0.1.4", -1}, // a pre-release is older than its release
        {"0.1.4", "0.1.4-rc1", 1},
        {"0.1.5-beta.1", "0.1.4", 1},  // ...but newer than the previous release
        {"", "0.1.4", -1},             // an empty tag never wins
    };
    for (const Comparison &comparison : comparisons) {
        const QString left = QString::fromLatin1(comparison.left);
        const QString right = QString::fromLatin1(comparison.right);
        const int got = UpdateChecker::compareVersions(left, right);
        const int sign = got < 0 ? -1 : (got > 0 ? 1 : 0);
        check(QStringLiteral("compareVersions(\"%1\", \"%2\")").arg(left, right),
              sign == comparison.sign,
              QStringLiteral("got %1, wanted %2")
                  .arg(sign == 0 ? QStringLiteral("=")
                                 : (sign > 0 ? QStringLiteral(">") : QStringLiteral("<")),
                       comparison.sign == 0 ? QStringLiteral("=")
                                            : (comparison.sign > 0 ? QStringLiteral(">")
                                                                   : QStringLiteral("<"))));
    }

    // ---- the asset this platform is offered -------------------------------
    // The names are the ones .github/workflows/release.yml uploads.
#if defined(Q_OS_WIN)
    // Windows gets two builds and the installer wins over the portable package,
    // whatever order the API happens to list them in.
    const QString wanted = QStringLiteral("Fetchora-0.1.5-windows-x64-setup.exe");
    const QString fallback = QStringLiteral("Fetchora-0.1.5-windows-x64.zip");
    const QString foreign = QStringLiteral("Fetchora-macos-arm64.dmg");
    const QStringList preference = {wanted, fallback};
#elif defined(Q_OS_MACOS)
    const QString wanted = QStringLiteral("Fetchora-macos-arm64.dmg");
    const QString foreign = QStringLiteral("Fetchora-0.1.5-windows-x64-setup.exe");
    const QStringList preference = {wanted};
#else
    const QString wanted = QStringLiteral("Fetchora-linux-x86_64.tar.gz");
    const QString foreign = QStringLiteral("Fetchora-0.1.5-windows-x64-setup.exe");
    const QStringList preference = {wanted};
#endif
    const auto urlFor = [](const QString &name) {
        return QUrl(QStringLiteral("https://example.invalid/") + name);
    };
    const auto assetName = [](const UpdateChecker::Release &release) {
        const QUrl asset = release.preferredAsset();
        return asset.isValid() ? asset.fileName() : QStringLiteral("(none)");
    };

    // Each round drops this platform's first choice, so the last round is the
    // worst package that is still usable here. The assets are listed worst-first
    // and a foreign build is always appended last: anything that takes the first
    // match, or ignores the platform, fails here.
    for (int i = 0; i < preference.size(); ++i) {
        UpdateChecker::Release sample;
        sample.version = QStringLiteral("0.1.5");
        for (int j = preference.size() - 1; j >= i; --j)
            sample.assets.append({preference.at(j), urlFor(preference.at(j))});
        sample.assets.append({foreign, urlFor(foreign)});
        check(QStringLiteral("preferredAsset() picks %1").arg(preference.at(i)),
              assetName(sample) == preference.at(i), assetName(sample));
    }

    UpdateChecker::Release tagged;
    tagged.tagName = QStringLiteral("v0.1.5");
    tagged.version = tagged.tagName.mid(1);
    check(QStringLiteral("Release::isValid() for a parsed tag"), tagged.isValid(), QString());

    // Nothing for this platform: the About page opens the release page instead of
    // downloading a foreign binary.
    UpdateChecker::Release foreignOnly;
    foreignOnly.version = QStringLiteral("0.1.5");
    foreignOnly.assets = {{foreign, urlFor(foreign)}};
    check(QStringLiteral("a release with no build for this platform offers nothing"),
          assetName(foreignOnly) == QLatin1String("(none)"), assetName(foreignOnly));

    // ---- the settings the update UI edits ---------------------------------
    // The Settings page edits every row by property *name*, and falls back to a
    // raw QSettings write when no such property exists - so a renamed or missing
    // property makes the switch silently stop reaching the running app. These are
    // the three rows the update feature owns. Read-only: a self-test must not
    // write to the settings it is inspecting.
    const QStringList keys = {QStringLiteral("checkForUpdates"),
                              QStringLiteral("updateIncludePrerelease"),
                              QStringLiteral("lastNotifiedVersion")};
    const QMetaObject *meta = settings.metaObject();
    for (const QString &key : keys) {
        const int index = meta->indexOfProperty(key.toUtf8().constData());
        const QMetaProperty property = index >= 0 ? meta->property(index) : QMetaProperty();
        check(QStringLiteral("SettingsManager::%1 is a readable, writable property").arg(key),
              property.isValid() && property.isReadable() && property.isWritable(),
              property.isValid() ? QString::fromLatin1(property.typeName())
                                 : QStringLiteral("no such property"));
    }

    if (failures > 0) {
        out() << "RESULT: " << failures << " update check(s) failed\n";
        return 5;
    }
    out() << "RESULT: update logic OK\n";
    return 0;
}

/**
 * The bencode decoder's own checks.
 *
 * It parses files nobody vouches for - a .torrent from a website, a magnet's
 * metadata, whatever the browser extension forwards - so "reject" has to mean
 * "return nothing", never "recurse until the stack runs out". A crafted file
 * used to do exactly that: an instant crash with no message anywhere.
 *
 * Exit code 0 when every check passed, 6 otherwise.
 */
int runTorrentSelfTest()
{
    out() << "torrent self-test\n";
    int failures = 0;
    const auto check = [&failures](const QString &what, bool ok, const QString &detail) {
        out() << (ok ? "  ok   " : "  FAIL ") << what;
        if (!detail.isEmpty())
            out() << "  (" << detail << ')';
        out() << '\n';
        if (!ok)
            ++failures;
    };

    // Far deeper than any real torrent nests (four or five levels).
    QByteArray deep;
    deep.reserve(5000);
    for (int i = 0; i < 5000; ++i)
        deep += 'l';
    const QVariant nested = Bencode::decode(deep);
    check(QStringLiteral("a 5000-level list is refused, not followed"),
          !nested.isValid() || nested.isNull(), QStringLiteral("depth limit"));

    // An absurd length prefix used to wrap the end offset into a negative
    // position, which the next at() then read from.
    check(QStringLiteral("a string claiming 9999999999999999999 bytes is refused"),
          !Bencode::decode(QByteArrayLiteral("9999999999999999999:abc")).isValid(),
          QStringLiteral("length overflow"));

    // The ordinary path still has to work: encode -> decode -> same values.
    QVariantMap sample;
    sample.insert(QStringLiteral("name"), QByteArrayLiteral("fetchora"));
    sample.insert(QStringLiteral("length"), qint64(1234));
    QVariantList files;
    files << QByteArrayLiteral("a.txt");
    sample.insert(QStringLiteral("files"), files);
    const QVariant roundTrip = Bencode::decode(Bencode::encode(sample));
    check(QStringLiteral("a normal dictionary survives encode/decode"),
          roundTrip.toMap().value(QStringLiteral("name")).toByteArray()
                  == QByteArrayLiteral("fetchora")
              && roundTrip.toMap().value(QStringLiteral("length")).toLongLong() == 1234,
          QStringLiteral("round trip"));

    // And the public entry point answers instead of dying.
    TorrentUtils torrents;
    const QVariantMap info = torrents.inspectData(deep);
    check(QStringLiteral("inspectData() reports the crafted file as unusable"),
          !info.value(QStringLiteral("ok")).toBool(), QStringLiteral("ok = false"));

    if (failures > 0) {
        out() << "RESULT: " << failures << " torrent check(s) failed\n";
        return 6;
    }
    out() << "RESULT: torrent decoding OK\n";
    return 0;
}

int runSelfTest(SettingsManager &settings)
{
    // Deterministic and engine-free, so it runs first - and on every platform,
    // including the ones that bundle no aria2c.
    if (const int updates = runUpdateSelfTest(settings); updates != 0)
        return updates;
    if (const int torrents = runTorrentSelfTest(); torrents != 0)
        return torrents;

    out() << "aria2c self-test\n";
    const QString executable = Aria2Process::locateAria2(settings.aria2Executable());
    if (executable.isEmpty()) {
        out() << "RESULT: aria2c not found\n";
        return 2;
    }
    out() << "engine: " << executable << '\n';

    const QSet<QString> known = aria2KnownOptions(executable);
    if (known.isEmpty()) {
        out() << "RESULT: could not read aria2c --help=#all\n";
        return 3;
    }
    out() << "engine understands " << known.size() << " options\n";

    const QStringList args = settings.buildAria2Arguments();
    QStringList bad;
    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (!arg.startsWith(QLatin1String("--")))
            continue;
        const QString name = arg.mid(2).section(QLatin1Char('='), 0, 0);
        // A switch without '=' consumes the next argv entry, so report it too.
        const bool takesValue = !arg.contains(QLatin1Char('=')) && i + 1 < args.size()
            && !args.at(i + 1).startsWith(QLatin1String("--"));
        if (!known.contains(name)) {
            bad << arg;
        } else if (takesValue) {
            out() << "  " << name << " = " << args.at(i + 1) << '\n';
        }
    }

    if (!bad.isEmpty()) {
        out() << "RESULT: rejected switches: " << bad.join(QStringLiteral(", ")) << '\n';
        return 4;
    }
    out() << "RESULT: all switches accepted\n";
    return 0;
}

int runMakeTorrent(const QStringList &args)
{
    if (args.size() < 2) {
        out() << "usage: --make-torrent <source> --output <file.torrent> [--tracker url,url]\n";
        return 2;
    }
    const QString source = args.at(0);
    const QString output = args.at(1);
    const QString trackers = args.value(2);

    TorrentUtils torrents;
    QObject::connect(&torrents, &TorrentUtils::progress, [](int percent, const QString &message) {
        out() << "  " << percent << "% " << message << '\n';
        out().flush();
    });
    if (!torrents.create(source, output, trackers)) {
        out() << "RESULT: torrent creation failed\n";
        return 3;
    }

    // Re-read what we just wrote so the printed hash is the file's real one.
    const QVariantMap info = torrents.inspect(output);
    if (!info.value(QStringLiteral("ok")).toBool()) {
        out() << "RESULT: the created torrent could not be re-read\n";
        return 4;
    }
    out() << "created: " << output << '\n';
    out() << "infoHash: " << info.value(QStringLiteral("infoHash")).toString() << '\n';
    out() << "pieceLength: " << info.value(QStringLiteral("pieceLength")).toInt() << '\n';
    out() << "pieces: " << info.value(QStringLiteral("pieces")).toInt() << '\n';
    out() << "RESULT: torrent created\n";
    return 0;
}

int runInspectTorrent(const QString &path){
    TorrentUtils torrents;
    const QVariantMap info = torrents.inspect(path);
    if (!info.value(QStringLiteral("ok")).toBool()) {
        out() << "RESULT: not a readable torrent\n";
        return 2;
    }
    const QVariantList files = info.value(QStringLiteral("files")).toList();
    const QStringList trackers = info.value(QStringLiteral("announceList")).toStringList();

    out() << "name: " << info.value(QStringLiteral("name")).toString() << '\n';
    out() << "infoHash: " << info.value(QStringLiteral("infoHash")).toString() << '\n';
    out() << "totalLength: " << info.value(QStringLiteral("totalLength")).toLongLong() << '\n';
    out() << "pieceLength: " << info.value(QStringLiteral("pieceLength")).toInt() << '\n';
    out() << "pieces: " << info.value(QStringLiteral("pieces")).toInt() << '\n';
    out() << "isMultiFile: " << (info.value(QStringLiteral("isMultiFile")).toBool() ? "yes" : "no")
          << '\n';
    out() << "isPrivate: " << (info.value(QStringLiteral("isPrivate")).toBool() ? "yes" : "no")
          << '\n';
    out() << "trackers (" << trackers.size() << ")\n";
    for (const QString &tracker : trackers)
        out() << "  " << tracker << '\n';
    out() << "files (" << files.size() << ")\n";
    for (const QVariant &v : files) {
        const QVariantMap file = v.toMap();
        out() << "  " << file.value(QStringLiteral("path")).toString() << "  "
              << file.value(QStringLiteral("length")).toLongLong() << '\n';
    }
    out() << "RESULT: torrent inspected\n";
    return 0;
}

/**
 * Ask GitHub for the newest release and print what was found.
 *
 * Uses the same UpdateChecker the About page drives, so the behaviour a
 * maintainer can reproduce from a terminal is the behaviour users get. Exit
 * codes: 0 = ran (whether or not an update exists), 3 = the check failed.
 */
int runUpdateCheck(bool includePrerelease)
{
    QApplication *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!application)
        return 3;

    UpdateChecker checker;
    QObject::connect(&checker, &UpdateChecker::checkFinished, application,
                     [application, includePrerelease](const UpdateChecker::Release &release,
                                                      bool available) {
                         out() << "current: " << UpdateChecker::currentVersion() << '\n';
                         out() << "channel: " << (includePrerelease ? "stable+prerelease" : "stable")
                               << '\n';
                         if (!release.isValid()) {
                             out() << "latest:  (no releases yet)\n";
                             out() << "RESULT: no releases published\n";
                             QCoreApplication::exit(0);
                             return;
                         }
                         out() << "latest:  " << release.version << "  (tag " << release.tagName << ")\n";
                         out() << "published: " << release.publishedAt.toString(Qt::ISODate) << '\n';
                         out() << "prerelease: " << (release.prerelease ? "yes" : "no") << '\n';
                         const QUrl asset = release.preferredAsset();
                         out() << "asset for this platform: "
                               << (asset.isValid() ? asset.fileName() : QStringLiteral("(none)"))
                               << '\n';
                         out() << "update available: " << (available ? "yes" : "no") << '\n';
                         out() << "RESULT: " << (available ? "update available" : "up to date") << '\n';
                         out().flush();
                         QCoreApplication::exit(0);
                     });
    QObject::connect(&checker, &UpdateChecker::checkFailed, application,
                     [application](const QString &reason) {
                         out() << "RESULT: check failed: " << reason << '\n';
                         out().flush();
                         QCoreApplication::exit(3);
                     });

    out() << "checking " << UpdateChecker::releasesApiUrl().toString() << " ...\n";
    out().flush();
    checker.check(includePrerelease);
    return QApplication::exec();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QString::fromLatin1(kAppName));
    app.setApplicationDisplayName(QString::fromLatin1(kAppName));
    app.setOrganizationName(QString::fromLatin1(kAppName));
    app.setApplicationVersion(QStringLiteral("0.1.4"));
    app.setWindowIcon(makeAppIcon());
    // A download manager keeps running in the tray when its window closes.
    app.setQuitOnLastWindowClosed(false);

    // From here on everything - Qt's own warnings, the engine's output, the
    // crash handler - goes to the log file as well as to the terminal.
    Logger::install();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Fetchora - a fluent, aria2 powered download manager"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption minimizedOption({QStringLiteral("m"), QStringLiteral("minimized")},
                                       QStringLiteral("Start hidden in the system tray."));
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                        QStringLiteral("Render the window to a PNG and exit (UI checks)."),
                                        QStringLiteral("file"));
    QCommandLineOption maximizedOption(QStringLiteral("maximized"),
                                       QStringLiteral("Start maximized."));
    QCommandLineOption screenshotDelayOption(QStringLiteral("screenshot-delay"),
                                             QStringLiteral("Milliseconds to wait before --screenshot "
                                                            "(lets downloads populate the list)."),
                                             QStringLiteral("ms"));
    QCommandLineOption pageOption(QStringLiteral("page"),
                                  QStringLiteral("Open on this page (download, bittorrent, ...)."),
                                  QStringLiteral("key"));
    QCommandLineOption newInstanceOption(QStringLiteral("new-instance"),
                                         QStringLiteral("Do not forward to a running instance."));
    QCommandLineOption selfTestOption(QStringLiteral("self-test"),
                                      QStringLiteral("Run the built-in self-tests "
                                                     "(update logic, aria2c command line) and exit."));
    QCommandLineOption makeTorrentOption(QStringLiteral("make-torrent"),
                                         QStringLiteral("Create a .torrent from a file or folder (headless)."),
                                         QStringLiteral("source"));
    QCommandLineOption outputOption(QStringLiteral("output"),
                                    QStringLiteral("Output path for --make-torrent."),
                                    QStringLiteral("file"));
    QCommandLineOption trackerOption(QStringLiteral("tracker"),
                                     QStringLiteral("Comma separated tracker list for --make-torrent."),
                                     QStringLiteral("urls"));
    QCommandLineOption inspectTorrentOption(QStringLiteral("inspect-torrent"),
                                            QStringLiteral("Print the contents of a .torrent and exit."),
                                            QStringLiteral("file"));
    QCommandLineOption checkUpdatesOption(QStringLiteral("check-updates"),
                                          QStringLiteral("Ask GitHub for the newest release and exit."));
    QCommandLineOption prereleaseOption(QStringLiteral("prerelease"),
                                        QStringLiteral("Include pre-releases in --check-updates."));
    parser.addOption(minimizedOption);
    parser.addOption(newInstanceOption);
    parser.addOption(screenshotOption);
    parser.addOption(maximizedOption);
    parser.addOption(screenshotDelayOption);
    parser.addOption(pageOption);
    parser.addOption(selfTestOption);
    parser.addOption(makeTorrentOption);
    parser.addOption(outputOption);
    parser.addOption(trackerOption);
    parser.addOption(inspectTorrentOption);
    parser.addOption(checkUpdatesOption);
    parser.addOption(prereleaseOption);
    parser.addPositionalArgument(QStringLiteral("urls"),
                                 QStringLiteral("Links, magnet URIs or .torrent files to download."));
    parser.process(app);

    const QStringList positional = parser.positionalArguments();

    // --------------------------------------------------- headless entry points
    // Handled before anything else so the self-tests never touch the settings
    // file, start an engine or create a window.
    if (parser.isSet(selfTestOption)) {
        SettingsManager probe;
        return runSelfTest(probe);
    }
    if (parser.isSet(makeTorrentOption)) {
        return runMakeTorrent({parser.value(makeTorrentOption), parser.value(outputOption),
                               parser.value(trackerOption)});
    }
    if (parser.isSet(inspectTorrentOption))
        return runInspectTorrent(parser.value(inspectTorrentOption));

    // Update check as a headless mode. It is the same UpdateChecker the About
    // page drives, which is the point: what CI or a maintainer can exercise from
    // a terminal is exactly what the button does, not a second implementation.
    if (parser.isSet(checkUpdatesOption))
        return runUpdateCheck(parser.isSet(prereleaseOption));

    if (!parser.isSet(newInstanceOption) && notifyRunningInstance(positional, QDir::currentPath())) {
        // A second launch hands its links over and exits on purpose. Saying so in
        // the log is what turns "the app closed itself instantly" into a fact the
        // user can check.
        Logger::line(QStringLiteral("app"),
                     QStringLiteral("another instance is already running; handed over the request "
                                    "and exiting (use --new-instance to force a second window)"));
        return 0;
    }

    SettingsManager settings(&app);
    FluentTheme::instance()->attach(&settings);
    LanguageManager::instance()->apply(settings.language());

    // Font family/size are user settings; the theme owns the rest of the ramp.
    FluentTheme::setUiFontFamily(settings.uiFontFamily());
    FluentTheme::setUiFontSize(settings.uiFontSize());
    QObject::connect(&settings, &SettingsManager::uiFontFamilyChanged, &app, [&settings]() {
        FluentTheme::setUiFontFamily(settings.uiFontFamily());
    });
    QObject::connect(&settings, &SettingsManager::uiFontSizeChanged, &app, [&settings]() {
        FluentTheme::setUiFontSize(settings.uiFontSize());
    });

    NotificationManager notifications;
    ClipboardHelper clipboard;
    TorrentUtils torrents;
    Aria2Manager aria2(&settings);

    // The paths a bug report always needs, in the order they matter.
    Logger::line(QStringLiteral("app"),
                 QStringLiteral("downloads: %1").arg(settings.downloadDir()));
    Logger::line(QStringLiteral("app"),
                 QStringLiteral("engine: %1 (configured: %2)")
                     .arg(Aria2Process::locateAria2(settings.aria2Executable()),
                          settings.aria2Executable().isEmpty() ? QStringLiteral("auto")
                                                               : settings.aria2Executable()));
    // Retention is a setting; a shorter window is applied right away, so the
    // "clean up my logs" knob does something the moment it is turned.
    Logger::setRetentionDays(settings.logRetentionDays());
    QObject::connect(&settings, &SettingsManager::logRetentionDaysChanged, &app, [&settings]() {
        Logger::setRetentionDays(settings.logRetentionDays());
        Logger::prune();
    });

    // The application-wide style sheet is generated from the active theme and
    // re-applied whenever it changes; every widget restyles at once.
    auto applyTheme = [&app]() {
        // The palette first: it is what unstyled widgets (scroll area viewports,
        // item views, menus, splitters) paint from, and leaving it on the system
        // theme is what made the gaps between history rows come out black on a
        // dark Windows install while the window itself was light.
        FluentTheme::applyApplicationPalette();
        FluentTheme::ensureFontsResolved();
        app.setStyleSheet(FluentTheme::instance()->applicationStyleSheet());
    };
    applyTheme();
    QObject::connect(FluentTheme::instance(), &FluentTheme::changed, &app, applyTheme);

    FluentMainWindow window;
    window.setBackdrop(settings.useMica() ? FluentMainWindow::BackdropMica
                                          : FluentMainWindow::BackdropNone);
    window.setCaptionHeight(FluentTheme::captionHeight());
    window.setWindowTitle(QString::fromLatin1(kAppName));
    window.resize(1240, 800);

    // ------------------------------------------------------------- shell
    // Title bar on top, navigation pane + page stack below.
    auto *root = new QWidget(&window);
    root->setObjectName(QStringLiteral("pageRoot"));
    auto *outer = new QVBoxLayout(root);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *titleBar = new FluentTitleBar(root);
    titleBar->setTitle(QString::fromLatin1(kAppName));
    outer->addWidget(titleBar);

    auto *divider = new QFrame(root);
    divider->setProperty("fluentRole", "divider");
    divider->setFixedHeight(1);
    outer->addWidget(divider);

    auto *body = new QWidget(root);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    auto *nav = new FluentNavigationView(body);
    bodyLayout->addWidget(nav);

    auto *pageArea = new QWidget(body);
    pageArea->setObjectName(QStringLiteral("contentRoot"));
    auto *pageLayout = new QVBoxLayout(pageArea);
    pageLayout->setContentsMargins(FluentTheme::spacingXL(), FluentTheme::spacingL(),
                                   FluentTheme::spacingXL(), FluentTheme::spacingL());
    pageLayout->setSpacing(FluentTheme::spacingM());

    // ---------------------------------------------------------------- pages
    // A QStackedWidget keeps every page alive so scroll positions and form
    // state survive navigation.
    auto *pageStack = new QStackedWidget(pageArea);
    pageLayout->addWidget(pageStack, 1);

    auto *downloadsPage = new DownloadsPage(&aria2, pageStack);
    auto *detailsPanel = new TaskDetailsPanel(&aria2, pageStack);
    auto *btPage = new BitTorrentPage(&aria2, pageStack);
    auto *historyPage = new HistoryPage(&aria2, pageStack);
    auto *createTorrentPage = new CreateTorrentPage(&torrents, pageStack);
    auto *settingsPage = new SettingsPage(&settings, &aria2, pageStack);
    auto *aboutPage = new AboutPage(&aria2, pageStack, &settings);

    // The download list and the inspector share one splitter, so the details
    // pane can be toggled without losing the list's scroll position.
    auto *downloadsHost = new QSplitter(Qt::Horizontal, pageStack);
    downloadsHost->setChildrenCollapsible(false);
    downloadsHost->addWidget(downloadsPage);
    downloadsHost->addWidget(detailsPanel);
    downloadsHost->setStretchFactor(0, 5);
    downloadsHost->setStretchFactor(1, 4);
    detailsPanel->setVisible(settings.showDetailsPanel());

    // One page for every task, with the status chips and the search above it. A
    // separate "queue" page used to show the same rows filtered to waiting ones,
    // which is what the 等待中 chip already does.
    pageStack->addWidget(downloadsHost);      // 0
    pageStack->addWidget(btPage);             // 1
    pageStack->addWidget(historyPage);        // 2
    pageStack->addWidget(createTorrentPage);  // 3
    pageStack->addWidget(settingsPage);       // 4
    pageStack->addWidget(aboutPage);          // 5

    const QHash<QString, int> pageIndex = {
        {QStringLiteral("download"), 0},     {QStringLiteral("bittorrent"), 1},
        {QStringLiteral("history"), 2},      {QStringLiteral("createtorrent"), 3},
        {QStringLiteral("settings"), 4},     {QStringLiteral("about"), 5},
    };

    auto showPage = [nav, pageStack, pageIndex](const QString &key) {
        pageStack->setCurrentIndex(pageIndex.value(key, 0));
        nav->setCurrentKey(key);
    };

    // The navigation captions are re-applied on every language change.
    auto retranslateNav = [nav]() {
        nav->setItemTitle(QStringLiteral("download"), QObject::tr("下载任务"));
        nav->setItemTitle(QStringLiteral("bittorrent"), QObject::tr("BitTorrent"));
        nav->setItemTitle(QStringLiteral("history"), QObject::tr("下载历史"));
        nav->setItemTitle(QStringLiteral("createtorrent"), QObject::tr("制作种子"));
        nav->setItemTitle(QStringLiteral("settings"), QObject::tr("设置"));
        nav->setItemTitle(QStringLiteral("about"), QObject::tr("关于"));
        nav->retranslate();
    };
    nav->addItem(QStringLiteral("download"), FluentTheme::Glyph::Download, QObject::tr("下载任务"));
    nav->addItem(QStringLiteral("bittorrent"), FluentTheme::Glyph::Torrent, QObject::tr("BitTorrent"));
    nav->addItem(QStringLiteral("history"), FluentTheme::Glyph::History, QObject::tr("下载历史"));
    nav->addItem(QStringLiteral("createtorrent"), FluentTheme::Glyph::Add, QObject::tr("制作种子"));
    nav->addSpacer(8);
    nav->addItem(QStringLiteral("settings"), FluentTheme::Glyph::Settings, QObject::tr("设置"));
    nav->addItem(QStringLiteral("about"), FluentTheme::Glyph::Info, QObject::tr("关于"));
    nav->setCurrentKey(QStringLiteral("download"));

    bodyLayout->addWidget(pageArea, 1);
    outer->addWidget(body, 1);
    window.setCentralWidget(root);

    // The pages were built inside the (unparented) stack, so walk the finished
    // shell once to lay the type ramp over every label, input and button.
    applyTheme();

    // Toast host floats over the page area.
    auto *toasts = new ToastHost(pageArea);
    toasts->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // ---------------------------------------------------------- page wiring
    auto openTorrentPicker = [&window, &aria2]() {
        const QString path = QFileDialog::getOpenFileName(
            &window, QObject::tr("打开种子文件"), QString(),
            QObject::tr("种子文件 (*.torrent *.metalink);;所有文件 (*)"));
        if (!path.isEmpty())
            aria2.addTorrentFile(path);
    };
    auto openNewTaskDialog = [&window, &aria2]() {
        NewTaskDialog dialog(&aria2, &window);
        dialog.exec();
    };

    QObject::connect(downloadsPage, &DownloadsPage::newTaskRequested, &window, openNewTaskDialog);
    QObject::connect(downloadsPage, &DownloadsPage::torrentPickerRequested, &window, openTorrentPicker);
    QObject::connect(downloadsPage, &DownloadsPage::toast, &window,
                     [toasts](const QString &text, bool isError) {
                         toasts->push(text, isError ? ToastHost::Error : ToastHost::Success);
                     });
    QObject::connect(downloadsPage, &DownloadsPage::detailsToggleRequested, &window,
                     [detailsPanel]() {
                         detailsPanel->setVisible(!detailsPanel->isVisible());
                     });
    QObject::connect(detailsPanel, &TaskDetailsPanel::closeRequested, &window,
                     [detailsPanel]() { detailsPanel->setVisible(false); });
    QObject::connect(detailsPanel, &TaskDetailsPanel::toast, &window,
                     [toasts](const QString &text, bool isError) {
                         toasts->push(text, isError ? ToastHost::Error : ToastHost::Success);
                     });
    QObject::connect(btPage, &BitTorrentPage::torrentPickerRequested, &window, openTorrentPicker);
    QObject::connect(btPage, &BitTorrentPage::toast, &window,
                     [toasts](const QString &text, bool isError) {
                         toasts->push(text, isError ? ToastHost::Error : ToastHost::Success);
                     });
    QObject::connect(createTorrentPage, &CreateTorrentPage::toast, &window,
                     [toasts](const QString &text, bool isError) {
                         toasts->push(text, isError ? ToastHost::Error : ToastHost::Success);
                     });
    QObject::connect(settingsPage, &SettingsPage::toast, &window,
                     [toasts](const QString &text, bool isError) {
                         toasts->push(text, isError ? ToastHost::Error : ToastHost::Success);
                     });
    QObject::connect(aboutPage, &AboutPage::toast, &window,
                     [toasts](const QString &text, bool isError) {
                         toasts->push(text, isError ? ToastHost::Error : ToastHost::Success);
                     });
    QObject::connect(downloadsPage, &DownloadsPage::selectionChanged, &window,
                     [&aria2, detailsPanel](const QString &gid) {
                         aria2.setDetailGid(gid);
                         if (!gid.isEmpty())
                             detailsPanel->setVisible(true);
                     });
    QObject::connect(&aria2, &Aria2Manager::toast, &window,
                     [toasts](const QString &message, bool isError) {
                         toasts->push(message, isError ? ToastHost::Error : ToastHost::Success);
                     });
    QObject::connect(historyPage, &HistoryPage::redownloadRequested, &window,
                     [&aria2](const QString &uri) { aria2.addUri(uri); });
    QObject::connect(historyPage, &HistoryPage::toast, &window,
                     [toasts](const QString &text, bool isError) {
                         toasts->push(text, isError ? ToastHost::Error : ToastHost::Success);
                     });

    // ------------------------------------------------------ shell wiring
    QObject::connect(titleBar, &FluentTitleBar::minimizeRequested, &window, &QWidget::showMinimized);
    QObject::connect(titleBar, &FluentTitleBar::maximizeRequested, &window, &FluentMainWindow::toggleMaximized);
    QObject::connect(titleBar, &FluentTitleBar::closeRequested, &window, &QWidget::close);
    QObject::connect(nav, &FluentNavigationView::pageRequested, &window,
                     [pageStack, pageIndex](const QString &key) {
                         pageStack->setCurrentIndex(pageIndex.value(key, 0));
                     });
    QObject::connect(titleBar, &FluentTitleBar::searchEdited, &window,
                     [&aria2, showPage](const QString &text) {
                         aria2.setSearchText(text);
                         // Typing in the title bar filters the download list, so
                         // that is where the results are: showing them means going
                         // there instead of leaving the user on another page
                         // wondering what the box did.
                         if (!text.isEmpty())
                             showPage(QStringLiteral("download"));
                     });

    // The inspector is visible by default and starts out empty, which reads as a
    // broken pane. Select the first task once, as soon as the list has content;
    // after that the user is in charge.
    auto autoSelectedOnce = std::make_shared<bool>(false);
    QObject::connect(&aria2, &Aria2Manager::tasksChanged, &window,
                     [&aria2, downloadsPage, detailsPanel, autoSelectedOnce]() {
                         if (*autoSelectedOnce || !detailsPanel->isVisible())
                             return;
                         if (!aria2.detailGid().isEmpty())
                             return;
                         const QVariantList tasks = aria2.tasks();
                         if (tasks.isEmpty())
                             return;
                         const QString gid =
                             tasks.first().toMap().value(QStringLiteral("gid")).toString();
                         if (gid.isEmpty())
                             return;
                         *autoSelectedOnce = true;
                         aria2.setDetailGid(gid);
                         downloadsPage->selectTask(gid);
                     });

    // Engine state and speed, mirrored into the shell chrome.
    auto updateChrome = [&aria2, nav, titleBar]() {
        const QVariantMap stats = aria2.statistics();
        const bool ready = aria2.engineReady();
        nav->setEngineState(ready, !ready && aria2.engineRunning());
        titleBar->setSubtitle(ready ? QStringLiteral("aria2 ") + aria2.engineVersion()
                                    : QObject::tr("引擎启动中…"));
        titleBar->setPrimaryBadge(
            QStringLiteral("↓ ") + FluentTheme::formatSpeed(stats.value(QStringLiteral("downloadSpeed")).toDouble()));
        const int active = stats.value(QStringLiteral("activeCount")).toInt();
        titleBar->setSecondaryBadge(active > 0 ? QObject::tr("%1 个活动").arg(active) : QString());
        // One badge for the whole list: active plus queued, since the page shows
        // both (the chips filter, the badge counts).
        const int queued = stats.value(QStringLiteral("waitingCount")).toInt();
        const int total = active + queued;
        nav->setItemBadge(QStringLiteral("download"), total > 0 ? QString::number(total) : QString());
    };
    QObject::connect(&aria2, &Aria2Manager::statisticsChanged, &window, updateChrome);
    QObject::connect(&aria2, &Aria2Manager::engineReadyChanged, &window, updateChrome);
    updateChrome();

    QObject::connect(FluentTheme::instance(), &FluentTheme::changed, &window,
                     [&window, &settings]() {
                         window.setBackdrop(settings.useMica() ? FluentMainWindow::BackdropMica
                                                               : FluentMainWindow::BackdropNone);
                         window.refreshNativeEffects();
                     });

    // ------------------------------------------------------------- tray menu
    // A tray icon is not guaranteed to exist. Linux desktops only provide one
    // when a StatusNotifier host (or the legacy XEmbed tray) is running; a bare
    // WM, a minimal Wayland session or a misconfigured desktop without
    // libappindicator gives isSystemTrayAvailable() == false. In that case a
    // "minimise to tray" app would hide its window with no way to bring it
    // back, so the whole tray feature is disabled and closing the window quits
    // the application instead (see the closeRequested() handler further down).
    const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
    if (!trayAvailable) {
        qWarning("No system tray available on this desktop; "
                 "the window will not be minimised to the tray.");
    }

    QSystemTrayIcon *tray = new QSystemTrayIcon(app.windowIcon(), &app);
    tray->setToolTip(QString::fromLatin1(kAppName));
    // Notifications are delivered through the tray balloon, so the manager only
    // gets a tray when one exists - otherwise every notification would log a
    // "Tray icon not available" warning and be dropped.
    if (trayAvailable)
        notifications.setTrayIcon(tray);

    QMenu *trayMenu = new QMenu();
    QAction *showAction = trayMenu->addAction(QObject::tr("显示主窗口"));
    QAction *pauseAction = trayMenu->addAction(QObject::tr("全部暂停"));
    QAction *resumeAction = trayMenu->addAction(QObject::tr("全部开始"));
    QMenu *languageMenu = trayMenu->addMenu(QObject::tr("语言"));
    auto *languageGroup = new QActionGroup(languageMenu);
    QHash<QString, QAction *> languageActions;
    for (const QString &code : LanguageManager::availableLanguages()) {
        QAction *action = languageMenu->addAction(LanguageManager::displayName(code));
        action->setCheckable(true);
        action->setChecked(code == settings.language());
        languageGroup->addAction(action);
        languageActions.insert(code, action);
        QObject::connect(action, &QAction::triggered, &window, [&settings, code]() {
            settings.setLanguage(code);
        });
    }
    trayMenu->addSeparator();
    QAction *quitAction = trayMenu->addAction(QObject::tr("退出"));

    QObject::connect(showAction, &QAction::triggered, &window, [&window]() {
        window.showNormal();
        window.raise();
        window.activateWindow();
    });
    QObject::connect(pauseAction, &QAction::triggered, &aria2, &Aria2Manager::pauseAll);
    QObject::connect(resumeAction, &QAction::triggered, &aria2, &Aria2Manager::resumeAll);
    QObject::connect(quitAction, &QAction::triggered, &app, [&window]() {
        window.forceClose();
        QCoreApplication::quit();
    });
    tray->setContextMenu(trayMenu);
    QObject::connect(tray, &QSystemTrayIcon::activated, &window,
                     [&window](QSystemTrayIcon::ActivationReason reason) {
                         if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick)
                             return;
                         if (window.isVisible() && !window.isMinimized()) {
                             window.hide();
                         } else {
                             window.showNormal();
                             window.raise();
                             window.activateWindow();
                         }
                     });
    QObject::connect(&aria2, &Aria2Manager::notification, &notifications,
                     [&notifications](const QString &title, const QString &message, bool) {
                         notifications.showNotification(title, message);
                     });
    // Shown only where a tray actually exists - showing an icon on a desktop
    // without a tray host produces a stray window or nothing at all.
    if (trayAvailable)
        tray->show();

    // Speed in the tray tooltip, mirroring the title bar badge.
    auto updateTrayTooltip = [tray, &aria2, &settings]() {
        if (!settings.showTraySpeed()) {
            tray->setToolTip(QString::fromLatin1(kAppName));
            return;
        }
        const QVariantMap stats = aria2.statistics();
        const QString down =
            FluentTheme::formatSpeed(stats.value(QStringLiteral("downloadSpeed")).toDouble());
        const QString up =
            FluentTheme::formatSpeed(stats.value(QStringLiteral("uploadSpeed")).toDouble());
        tray->setToolTip(QStringLiteral("%1\n↓ %2   ↑ %3")
                             .arg(QString::fromLatin1(kAppName), down, up));
    };
    QObject::connect(&aria2, &Aria2Manager::statisticsChanged, tray, updateTrayTooltip);
    updateTrayTooltip();

    // ------------------------------------------------------------ update check
    // Quietly, once, a few seconds after startup: late enough not to compete with
    // the engine coming up, and it blocks nothing. The result is a toast and a
    // notification, never a dialog - an update is news, not an obstacle.
    //
    // Each version is announced once. Without remembering the last one the toast
    // would reappear on every launch until the user gave in, which is nagging,
    // and the About page already offers a check whenever they want one.
    auto *updates = new UpdateChecker(&app);
    QObject::connect(updates, &UpdateChecker::checkFinished, &window,
                     [&settings, toasts, &notifications](const UpdateChecker::Release &release,
                                                         bool available) {
                         if (!available || release.version.isEmpty())
                             return;
                         if (settings.lastNotifiedVersion() == release.version)
                             return;
                         settings.setLastNotifiedVersion(release.version);
                         const QString message =
                             QObject::tr("发现新版本 %1，当前版本 %2。在「关于」页可以查看并安装。")
                                 .arg(release.version, UpdateChecker::currentVersion());
                         toasts->push(message, ToastHost::Info, 9000);
                         notifications.showNotification(QObject::tr("Fetchora 有新版本"), message);
                     });
    QObject::connect(updates, &UpdateChecker::checkFailed, &window, [](const QString &) {
        // Deliberately silent. The user did not ask for this check, so a toast
        // about a DNS hiccup or an exhausted anonymous rate limit on startup is
        // noise; the About page reports failures when the check is explicit.
    });
    if (settings.checkForUpdates()) {
        QTimer::singleShot(8000, updates,
                           [updates, &settings]() { updates->check(settings.updateIncludePrerelease()); });
    }

    // Closing to the tray is the friendly default for a download manager - but
    // only when there is a tray to restore the window from. Without one, hiding
    // would strand the running instance with no visible window and no way back,
    // so close means quit (the engine is stopped by the app shutting down).
    QObject::connect(&window, &FluentMainWindow::closeRequested, &window,
                     [&window, &settings, trayAvailable]() {
                         if (trayAvailable && settings.closeToTray()) {
                             Logger::line(QStringLiteral("app"),
                                          QStringLiteral("window closed: hidden to the tray, "
                                                         "still running"));
                             window.hide();
                         } else {
                             Logger::line(QStringLiteral("app"),
                                          QStringLiteral("window closed: quitting"));
                             window.forceClose();
                         }
                     });

    // ------------------------------------------------- language re-translation
    auto retranslateShell = [&, titleBar, nav, trayMenu, showAction, pauseAction, resumeAction,
                             quitAction, languageMenu, retranslateNav, languageActions]() {
        const QString code = LanguageManager::instance()->requested();
        if (QAction *action = languageActions.value(code))
            action->setChecked(true);
        languageMenu->setTitle(QObject::tr("语言"));
        showAction->setText(QObject::tr("显示主窗口"));
        pauseAction->setText(QObject::tr("全部暂停"));
        resumeAction->setText(QObject::tr("全部开始"));
        quitAction->setText(QObject::tr("退出"));
        retranslateNav();
        titleBar->setTitle(QString::fromLatin1(kAppName));
        const QVariantMap stats = aria2.statistics();
        const int active = stats.value(QStringLiteral("activeCount")).toInt();
        titleBar->setSecondaryBadge(active > 0 ? QObject::tr("%1 个活动").arg(active) : QString());
        if (!aria2.engineReady())
            titleBar->setSubtitle(QObject::tr("引擎启动中…"));
        Q_UNUSED(nav)
    };
    QObject::connect(LanguageManager::instance(), &LanguageManager::languageChanged, &window,
                     retranslateShell);
    QObject::connect(&settings, &SettingsManager::languageChanged, &window, [&settings]() {
        LanguageManager::instance()->apply(settings.language());
    });
    QObject::connect(LanguageManager::instance(), &LanguageManager::languageChanged, &window,
                     [&app]() { app.setStyleSheet(FluentTheme::instance()->applicationStyleSheet()); });

    // App-wide keyboard shortcuts.
    auto addShortcut = [&window](const QKeySequence &keys, const std::function<void()> &fn) {
        auto *action = new QAction(&window);
        action->setShortcut(keys);
        action->setShortcutContext(Qt::ApplicationShortcut);
        QObject::connect(action, &QAction::triggered, &window, fn);
        window.addAction(action);
        return action;
    };
    addShortcut(QKeySequence(QStringLiteral("Ctrl+N")), openNewTaskDialog);
    addShortcut(QKeySequence(QStringLiteral("Ctrl+O")), openTorrentPicker);
    addShortcut(QKeySequence(QStringLiteral("F5")), [&aria2]() { aria2.refreshNow(); });
    addShortcut(QKeySequence(QStringLiteral("Ctrl+,")),
                [showPage]() { showPage(QStringLiteral("settings")); });
    addShortcut(QKeySequence(QStringLiteral("Ctrl+Q")), [&window]() {
        window.forceClose();
        QCoreApplication::quit();
    });

    // Incoming second-instance payload: hand the URLs to aria2. The endpoint
    // name is per user, so two accounts on one machine each keep their own
    // running instance instead of forwarding into each other's session.
    const QString serverKey = singleInstanceKey();
    QLocalServer::removeServer(serverKey);
    auto *server = new QLocalServer(&app);
    if (server->listen(serverKey)) {
        QObject::connect(server, &QLocalServer::newConnection, &app, [server, &aria2, &window]() {
            QLocalSocket *socket = server->nextPendingConnection();
            if (!socket)
                return;
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, &aria2]() {
                const QString text = QString::fromUtf8(socket->readAll());
                for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
                    if (line.startsWith(QLatin1String("cwd:")))
                        continue;
                    if (line == QLatin1String("WAKE"))
                        continue;
                    aria2.addFromText(line);
                }
            });
            window.showNormal();
            window.raise();
            window.activateWindow();
        });
    }

    if (!positional.isEmpty())
        aria2.addFromText(positional.join(QLatin1Char('\n')));

    const QString startPage = parser.value(pageOption);
    if (!startPage.isEmpty())
        showPage(startPage);

    if (parser.isSet(maximizedOption))
        window.showMaximized();
    else
        window.show();
    window.refreshNativeEffects();

    if (parser.isSet(minimizedOption) || settings.startMinimized()) {
        Logger::line(QStringLiteral("app"),
                     QStringLiteral("starting minimised to the tray (no window is shown)"));
        window.hide();
    }

    // ---------------------------------------------------------- ui screenshot
    // Renders the real window through the real widget tree into a PNG. Used to
    // verify the UI actually draws without needing to eyeball a live window.
    if (parser.isSet(screenshotOption)) {
        const QString path = parser.value(screenshotOption);
        const int delay = qMax(400, parser.value(screenshotDelayOption).toInt());
        QTimer::singleShot(delay, &app, [&window, path]() {
            const QPixmap shot = window.grab();
            QCoreApplication::exit(shot.save(path) ? 0 : 1);
        });
    }

    const int exitCode = QApplication::exec();
    // The last line of a normal run: with this in the log, "it just disappeared"
    // can be told apart from a crash, which ends in the crash handler instead.
    Logger::line(QStringLiteral("app"),
                 QStringLiteral("event loop finished (exit code %1)").arg(exitCode));
    return exitCode;
}
