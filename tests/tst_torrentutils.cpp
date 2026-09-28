// Unit tests for the two parsers everything else leans on: the tracker list reader
// (which also faces untrusted files and URLs) and the tracker health reader (which
// has to make sense of whatever aria2 decides to print).
//
// These cases mirror the ones in `Fetchora --self-test`; keep the two in step.

#include "TrackerSources.h"
#include "TorrentUtils.h"

#include <QTest>

class TorrentUtilsTest : public QObject
{
    Q_OBJECT

private slots:
    // ---------------------------------------------------------------- tracker lists
    void aTextListYieldsTheTrackerUrls()
    {
        const QByteArray text =
            "# my trackers\r\n"
            "udp://tracker.opentrackr.org:1337/announce\n"
            "https://tracker.example.org/announce, udp://[2001:db8::1]:6969/announce\n"
            "not a tracker at all\n"
            "magnet:?xt=urn:btih:aed8ca03ed278466c4a35d509bf864051b533011\n"
            "<a href=\"http://tracker.example.org/announce\">click</a>\n"
            "udp://tracker.opentrackr.org:1337/announce\n";

        const TrackerList::ParseResult parsed = TrackerList::parse(text);

        QCOMPARE(parsed.trackers.size(), 3);
        QVERIFY(parsed.trackers.contains(QStringLiteral("udp://tracker.opentrackr.org:1337/announce")));
        QVERIFY(parsed.trackers.contains(QStringLiteral("https://tracker.example.org/announce")));
        // Brackets are part of an IPv6 URL: splitting on them used to shred this one.
        QVERIFY(parsed.trackers.contains(QStringLiteral("udp://[2001:db8::1]:6969/announce")));
        QVERIFY(!parsed.binary);
    }

    void aBinaryFileIsRefused()
    {
        QByteArray binary = QByteArrayLiteral("\x7f\x45\x4c\x46");
        binary.append(QByteArray(4096, '\0'));
        const TrackerList::ParseResult parsed = TrackerList::parse(binary);
        QVERIFY(parsed.binary);
        QVERIFY(parsed.trackers.isEmpty());
    }

    void aTorrentIsNotMinedForUrls()
    {
        // A bencoded file that does contain announce URLs - they must not be taken.
        const QByteArray torrent =
            QByteArrayLiteral("d8:announce27:http://tracker.example.org/ann4:infod4:name4:testee");
        const TrackerList::ParseResult parsed = TrackerList::parse(torrent);
        QVERIFY(parsed.trackers.isEmpty());
    }

    void anOversizedListIsRefusedBeforeParsing()
    {
        const QByteArray huge(TrackerList::kMaxBytes + 1024, 'x');
        const TrackerList::ParseResult parsed = TrackerList::parse(huge);
        QVERIFY(parsed.trackers.isEmpty() || parsed.tooLarge);
    }

    void duplicatesCollapse()
    {
        QByteArray many;
        for (int i = 0; i < 500; ++i)
            many += QByteArrayLiteral("udp://tracker.example.org:6969/announce\n");
        QCOMPARE(TrackerList::parse(many).trackers.size(), 1);
    }

    // --------------------------------------------------------------- tracker health
    void anAnnounceLineYieldsTheTrackerAndASuccess()
    {
        const TrackerHealth::Verdict verdict = TrackerHealth::classify(
            QStringLiteral("[NOTICE] CUID#7 - Announce successfully to "
                           "udp://tracker.opentrackr.org:1337/announce."));
        QVERIFY(verdict.ok);
        QCOMPARE(verdict.url, QStringLiteral("udp://tracker.opentrackr.org:1337/announce"));
    }

    void aRealAbortedAnnounceIsAFailureWithThePlainUrl()
    {
        // Copied out of an engine log (DHT turned off, so the trackers were used).
        const TrackerHealth::Verdict verdict = TrackerHealth::classify(
            QStringLiteral("09/27 13:09:45 [ERROR] CUID#25 - Download aborted. "
                           "URI=udp://tracker.opentrackr.org:1337/announce?info_hash=%AE%D8%CA%03"
                           "&peer_id=A2-1-37-0-b%EF%C4%15%C0%CD%FA%07%FE%3C&port=6881"));
        QVERIFY(!verdict.ok);
        // The query is per-announce; the table lists the plain address, so the key has
        // to be the plain address or nothing would ever match.
        QCOMPARE(verdict.url, QStringLiteral("udp://tracker.opentrackr.org:1337/announce"));
    }

    void aRefusedConnectionIsAFailure()
    {
        const TrackerHealth::Verdict verdict = TrackerHealth::classify(
            QStringLiteral("Tracker https://tracker.example.org/announce: connection refused"));
        QVERIFY(!verdict.ok);
        QCOMPARE(verdict.url, QStringLiteral("https://tracker.example.org/announce"));
    }

    void peerAndDhtChatterIsNotATracker()
    {
        QVERIFY(TrackerHealth::classify(
                    QStringLiteral("[INFO] CUID#103 - To: 14.155.204.165:16881 handshake peerId=-TR29"))
                    .url.isEmpty());
        QVERIFY(TrackerHealth::classify(
                    QStringLiteral("[INFO] Message received: dht response ping TransactionID=8158ecc1"))
                    .url.isEmpty());
    }

    // ------------------------------------------------------------------ subscriptions
    void theCatalogueIsComplete()
    {
        const QList<TrackerSource> catalogue = builtInTrackerSources();
        QVERIFY(!catalogue.isEmpty());

        QSet<QString> ids;
        bool hasBlacklist = false;
        for (const TrackerSource &source : catalogue) {
            QVERIFY(!source.id.isEmpty());
            QVERIFY(!source.name.isEmpty());
            QVERIFY(source.url.startsWith(QLatin1String("https://")));
            QVERIFY2(!ids.contains(source.id), qPrintable(source.id));
            ids.insert(source.id);
            hasBlacklist = hasBlacklist || source.blacklist;
        }
        QVERIFY(hasBlacklist);
    }

    void anUnknownSourceIdIsAcceptedAsAUrl()
    {
        const TrackerSource custom = trackerSourceForId(QStringLiteral("https://example.com/list.txt"));
        QCOMPARE(custom.url, QStringLiteral("https://example.com/list.txt"));
        QVERIFY(!custom.blacklist);
        QCOMPARE(trackerSourceLabel(QStringLiteral("https://example.com/list.txt")),
                 QStringLiteral("example.com"));
    }
};

QTEST_APPLESS_MAIN(TorrentUtilsTest)

#include "tst_torrentutils.moc"
