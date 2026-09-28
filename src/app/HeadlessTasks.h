#ifndef HEADLESSTASKS_H
#define HEADLESSTASKS_H

#include <QString>
#include <QStringList>

class SettingsManager;

/**
 * The modes that never open a window: the self-tests, the tracker sync, the torrent
 * tools and the update check. main.cpp is the window and the command line; everything
 * that can run without one lives here so it can be read (and tested) on its own.
 *
 * Each returns a process exit code: 0 on success.
 */
namespace Headless {

/// --self-test: update logic, torrent decoding, settings and the aria2 command line.
int selfTest(SettingsManager &settings);
/// --sync-trackers: fetch every subscription source, apply it and exit.
int syncTrackers(SettingsManager &settings);
/// --make-torrent <path>: build a .torrent from a file or directory.
int makeTorrent(const QStringList &args);
/// --inspect-torrent <file>: print what a .torrent contains.
int inspectTorrent(const QString &path);
/// --check-update: ask GitHub for the newest release and report.
int checkUpdate(bool includePrerelease);

} // namespace Headless

#endif // HEADLESSTASKS_H
