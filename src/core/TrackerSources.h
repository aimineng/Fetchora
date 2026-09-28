#ifndef TRACKERSOURCES_H
#define TRACKERSOURCES_H

#include <QList>
#include <QString>

/**
 * A published tracker list the Tracker page can subscribe to.
 *
 * The two collections below are the ones everyone uses (ngosang/trackerslist and
 * XIU2/TrackersListCollection). They are plain text files on GitHub, one URL per
 * line, which is exactly what TrackerList::parse() already understands - so a
 * subscription and a hand-picked file take the same path.
 */
struct TrackerSource
{
    QString id;              ///< stable key stored in the settings
    QString name;            ///< shown on the chip and in the 来源 column
    QString url;             ///< raw list address
    bool cdn = false;        ///< IP-only variant, reachable behind a CDN
    bool blacklist = false;  ///< a list of trackers to avoid
};

/// The lists that ship with the app.
QList<TrackerSource> builtInTrackerSources();

/// Looks a source up by id; a custom entry (a bare URL) comes back with the URL as
/// both id and name so the table has something to show.
TrackerSource trackerSourceForId(const QString &id);

/// Short human name for a source id, for the 来源 column.
QString trackerSourceLabel(const QString &id);

#endif // TRACKERSOURCES_H
