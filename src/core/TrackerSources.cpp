#include "TrackerSources.h"

#include <QUrl>

namespace {

const char *const kRawBase = "https://raw.githubusercontent.com/";

} // namespace

QList<TrackerSource> builtInTrackerSources()
{
    const QString ngosang = QString::fromLatin1(kRawBase) + QStringLiteral("ngosang/trackerslist/master/");
    const QString xiu2 = QString::fromLatin1(kRawBase) + QStringLiteral("XIU2/TrackersListCollection/master/");

    return {
        {QStringLiteral("ngosang-best"), QStringLiteral("ngosang/trackerslist (best)"),
         ngosang + QStringLiteral("trackers_best.txt"), false, false},
        {QStringLiteral("ngosang-best-cdn"), QStringLiteral("ngosang/trackerslist (best, CDN)"),
         ngosang + QStringLiteral("trackers_best_ip.txt"), true, false},
        {QStringLiteral("xiu2-best"), QStringLiteral("XIU2/TrackersListCollection (best)"),
         xiu2 + QStringLiteral("best.txt"), false, false},
        {QStringLiteral("xiu2-best-cdn"), QStringLiteral("XIU2/TrackersListCollection (best, CDN)"),
         xiu2 + QStringLiteral("best_ip.txt"), true, false},
        {QStringLiteral("xiu2-blacklist"), QStringLiteral("XIU2/TrackersListCollection (blacklist)"),
         xiu2 + QStringLiteral("blacklist.txt"), false, true},
    };
}

TrackerSource trackerSourceForId(const QString &id)
{
    const QList<TrackerSource> builtIn = builtInTrackerSources();
    for (const TrackerSource &source : builtIn) {
        if (source.id == id)
            return source;
    }
    // Not one of ours: it is a URL (or a path) the user typed.
    TrackerSource custom;
    custom.id = id;
    custom.name = id;
    custom.url = id;
    custom.blacklist = false;
    return custom;
}

QString trackerSourceLabel(const QString &id)
{
    for (const TrackerSource &source : builtInTrackerSources()) {
        if (source.id == id)
            return source.name;
    }
    if (id.isEmpty())
        return {};
    // A URL reads better as its host; anything else is shown as it was typed.
    const QUrl url(id);
    return url.isValid() && !url.host().isEmpty() ? url.host() : id;
}
