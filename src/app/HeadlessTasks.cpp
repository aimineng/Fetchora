#include "HeadlessTasks.h"

#include "Logger.h"
#include "Aria2Process.h"
#include "SettingsManager.h"
#include "TorrentUtils.h"
#include "UpdateChecker.h"
#include "TrackerSources.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSet>
#include <QRegularExpression>
#include <QTextStream>
#include <QUrl>

#include <algorithm>

namespace {

/// The line-based output the headless modes print: plain enough to be piped, and it
/// never mixes the summary into the individual results.
QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

} // namespace

/// Every long option the bundled aria2c understands, taken from --help=#all.
///
/// `--help` alone lists only the common ones, which is how a working command line
/// came to look like it was full of switches the engine rejects.
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
namespace Headless {
static int runUpdateSelfTest(SettingsManager &settings)
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
static int runTorrentSelfTest()
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

    // ---- tracker lists ---------------------------------------------------
    // Importing a tracker list means parsing a file the user picked, and users pick
    // the wrong file. None of these may crash, and none may produce a "tracker"
    // that is really a line out of a video or an executable.
    const TrackerList::ParseResult text = TrackerList::parse(QByteArrayLiteral(
        "# my trackers\r\n"
        "udp://tracker.opentrackr.org:1337/announce\n"
        "https://tracker.example.org/announce, udp://[2001:db8::1]:6969/announce\n"
        "not a tracker at all\n"
        "magnet:?xt=urn:btih:0000000000000000000000000000000000000000\n"
        "<a href=\"http://tracker.example.org/announce\">click</a>\n"
        "udp://tracker.opentrackr.org:1337/announce\n"));
    check(QStringLiteral("a text list yields exactly the tracker URLs"),
          text.trackers.size() == 3
              && text.trackers.at(0) == QLatin1String("udp://tracker.opentrackr.org:1337/announce")
              && text.trackers.at(1) == QLatin1String("https://tracker.example.org/announce")
              && text.trackers.at(2) == QLatin1String("udp://[2001:db8::1]:6969/announce"),
          QStringLiteral("%1 kept, %2 rejected").arg(text.trackers.size()).arg(text.rejected));

    // A magnet URI is not a tracker, and neither is an .exe dressed up as a list.
    QByteArray binary("\x4d\x5a\x90\x00\x03\x00\x00\x00\x04\x00\x00\x00\xff\xff\x00\x00", 16);
    binary.append(QByteArray(4096, '\0'));
    const TrackerList::ParseResult blob = TrackerList::parse(binary);
    check(QStringLiteral("a binary file is refused instead of parsed"),
          blob.binary && blob.trackers.isEmpty(), QStringLiteral("binary = true"));

    const TrackerList::ParseResult torrentFile =
        TrackerList::parse(QByteArrayLiteral("d8:announce35:udp://tracker.example.org:6969/announce4:infod4:name4:testee"));
    check(QStringLiteral("a .torrent is recognised as such, not mined for URLs"),
          torrentFile.binary && torrentFile.trackers.isEmpty(), QStringLiteral("bencode refused"));

    const TrackerList::ParseResult huge =
        TrackerList::parse(QByteArray(TrackerList::kMaxBytes + 1, 'u'));
    check(QStringLiteral("an oversized file is refused before parsing"),
          huge.tooLarge && huge.trackers.isEmpty(), QStringLiteral("size cap"));

    QByteArray many;
    for (int i = 0; i < 2000; ++i)
        many += QByteArrayLiteral("udp://tracker.example.org:6969/announce\n");
    const TrackerList::ParseResult capped = TrackerList::parse(many);
    check(QStringLiteral("thousands of duplicates collapse to one entry"),
          capped.trackers.size() == 1, QStringLiteral("%1 entries").arg(capped.trackers.size()));

    check(QStringLiteral("the built-in tracker list is not empty"),
          !SettingsManager::builtInTrackers().isEmpty()
              && SettingsManager::builtInTrackers().first().startsWith(QLatin1String("udp://")),
          QStringLiteral("%1 trackers").arg(SettingsManager::builtInTrackers().size()));

    // The tracker page subscribes to published lists; a duplicate id there would make
    // two different chips write the same setting, and a missing URL would fetch nothing.
    const QList<TrackerSource> catalogue = builtInTrackerSources();
    QSet<QString> ids;
    bool catalogueOk = !catalogue.isEmpty();
    bool hasBlacklist = false;
    for (const TrackerSource &source : catalogue) {
        if (source.id.isEmpty() || source.name.isEmpty()
            || !source.url.startsWith(QLatin1String("https://")) || ids.contains(source.id))
            catalogueOk = false;
        ids.insert(source.id);
        if (source.blacklist)
            hasBlacklist = true;
    }
    check(QStringLiteral("the subscription catalogue is complete"), catalogueOk,
          QStringLiteral("%1 sources").arg(catalogue.size()));
    // A custom entry is just a URL the user typed: it has to come back usable.
    const TrackerSource custom = trackerSourceForId(QStringLiteral("https://example.com/list.txt"));
    check(QStringLiteral("an unknown source id is accepted as a URL"),
          custom.url == QStringLiteral("https://example.com/list.txt") && !custom.blacklist,
          trackerSourceLabel(QStringLiteral("https://example.com/list.txt")));
    check(QStringLiteral("the catalogue ships a blacklist source"), hasBlacklist,
          QStringLiteral("blacklist sources: %1")
              .arg(std::count_if(catalogue.begin(), catalogue.end(),
                                 [](const TrackerSource &s) { return s.blacklist; })));

    // Tracker health is read out of the engine log, so the reader has to survive the
    // wordings aria2 uses and ignore everything that is not about a tracker.
    {
        const TrackerHealth::Verdict good =
            TrackerHealth::classify(QStringLiteral("[NOTICE] CUID#7 - Announce successfully to "
                                                   "udp://tracker.opentrackr.org:1337/announce."));
        const TrackerHealth::Verdict again = TrackerHealth::classify(
            QStringLiteral("[INFO] Announce to https://tracker.example.org/announce succeeded"));
        const TrackerHealth::Verdict bad = TrackerHealth::classify(
            QStringLiteral("[WARN] CUID#9 - Announce to udp://open.stealth.si:80/announce failed. "
                           "Retrying..."));
        const TrackerHealth::Verdict refused = TrackerHealth::classify(
            QStringLiteral("Tracker https://tracker.example.org/announce: connection refused"));
        const TrackerHealth::Verdict peer = TrackerHealth::classify(
            QStringLiteral("[INFO] CUID#103 - To: 14.155.204.165:16881 handshake peerId=-TR2930-651"));
        const TrackerHealth::Verdict dht = TrackerHealth::classify(
            QStringLiteral("[INFO] Message received: dht response ping TransactionID=8158ecc1"));
        check(QStringLiteral("an announce line yields the tracker and a success"),
              good.ok && good.url == QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
              good.url);
        check(QStringLiteral("a differently worded success is recognised too"),
              again.ok && again.url == QStringLiteral("https://tracker.example.org/announce"),
              again.url);
        check(QStringLiteral("a failed announce is reported as a failure"),
              !bad.ok && bad.url == QStringLiteral("udp://open.stealth.si:80/announce"), bad.url);
        check(QStringLiteral("a refused connection is a failure"),
              !refused.ok && refused.url == QStringLiteral("https://tracker.example.org/announce"),
              refused.url);
        // The wording below is copied out of a real run (DHT turned off, so the engine
        // had to use its trackers) - it is what the reader has to cope with.
        const TrackerHealth::Verdict realFail = TrackerHealth::classify(
            QStringLiteral("09/27 13:09:45 [ERROR] CUID#25 - Download aborted. "
                           "URI=udp://tracker.opentrackr.org:1337/announce?info_hash=%AE%D8%CA%03"
                           "&peer_id=A2-1-37-0-b%EF%C4%15%C0%CD%FA%07%FE%3C&port=6881"));
        check(QStringLiteral("a real aborted announce is a failure with the plain URL"),
              !realFail.ok
                  && realFail.url == QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
              realFail.url);
        check(QStringLiteral("peer and DHT chatter is not mistaken for a tracker"),
              peer.url.isEmpty() && dht.url.isEmpty(), peer.url + dht.url);
    }

    if (failures > 0) {
        out() << "RESULT: " << failures << " torrent check(s) failed\n";
        return 6;
    }
    out() << "RESULT: torrent decoding OK\n";
    return 0;
}

/**
 * Headless tracker sync (-—sync-trackers): fetches every subscription source, merges
 * what came back, drops the blacklist and stores the result. It is what the page's
 * "Sync now" button does, without a window - which is also how it gets tested and
 * how a script can refresh the list before starting a download.
 */
int syncTrackers(SettingsManager &settings)
{
    QStringList ids;
    for (const QString &part : settings.btTrackerSources().split(
             QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) {
        if (!ids.contains(part))
            ids << part;
    }
    if (ids.isEmpty()) {
        out() << "no subscription sources configured; nothing to sync\n";
        return 0;
    }

    QNetworkAccessManager nam;
    QStringList merged;
    int failed = 0;
    for (const QString &id : ids) {
        const TrackerSource source = trackerSourceForId(id);
        QNetworkRequest request{QUrl(source.url)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(20000);
        QNetworkReply *reply = nam.get(request);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        if (reply->error() != QNetworkReply::NoError) {
            out() << "  failed  " << source.name << ": " << reply->errorString() << '\n';
            ++failed;
        } else {
            const TrackerList::ParseResult parsed = TrackerList::parse(reply->readAll());
            int added = 0;
            for (const QString &tracker : parsed.trackers) {
                if (!merged.contains(tracker)) {
                    merged << tracker;
                    ++added;
                }
            }
            out() << "  ok      " << source.name << ": " << parsed.trackers.size()
                  << " trackers (" << added << " new, " << parsed.rejected << " lines skipped)\n";
        }
        reply->deleteLater();
    }

    const QStringList blocked = settings.blacklistTrackers();
    for (const QString &tracker : blocked)
        merged.removeAll(tracker);
    settings.setBtTracker(merged.join(QLatin1Char(',')));

    out() << "RESULT: " << ids.size() << " source(s), " << failed << " failed, " << merged.size()
          << " trackers kept (" << blocked.size() << " blacklisted)\n";
    return failed == 0 ? 0 : 7;
}

int selfTest(SettingsManager &settings)
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

int makeTorrent(const QStringList &args)
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

int inspectTorrent(const QString &path){
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
} // namespace Headless

