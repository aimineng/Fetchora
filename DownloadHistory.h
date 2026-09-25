#ifndef DOWNLOADHISTORY_H
#define DOWNLOADHISTORY_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <QSqlDatabase>

/**
 * DownloadHistory - persistent record of every download this app has handled.
 *
 * Backed by SQLite so that filtering/searching stays fast even with tens of
 * thousands of rows. Falls back to an in-memory list if the SQL driver is
 * unavailable for any reason.
 */
class DownloadHistory : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    explicit DownloadHistory(QObject *parent = nullptr);
    ~DownloadHistory() override;

    /// Point the store at a database file (empty = default AppDataLocation path).
    void setDatabasePath(const QString &path);
    QString databasePath() const { return m_dbPath; }

    bool isReady() const { return m_ready; }
    int count() const { return m_count; }

    /// Insert or update the row identified by gid.
    Q_INVOKABLE void record(const QVariantMap &entry);

    /// Fetch rows, newest first.
    /// @param filter  "all" | "complete" | "error" | "removed" | "active"
    /// @param search  substring matched against name / uri / dir
    Q_INVOKABLE QVariantList fetch(const QString &filter = QStringLiteral("all"),
                                   const QString &search = QString(),
                                   int limit = 500) const;

    Q_INVOKABLE bool remove(const QString &gid);
    Q_INVOKABLE void clear();
    Q_INVOKABLE int prune(int keepEntries);
    Q_INVOKABLE QVariantMap statistics() const;
    /// Force pending writes out to the database file.
    void flush();

signals:
    void changed();

private:
    void open();
    void recount();

    QSqlDatabase m_db;
    QString m_dbPath;
    QString m_connectionName;
    bool m_ready = false;
    int m_count = 0;
    QVariantList m_memoryFallback;
};

#endif // DOWNLOADHISTORY_H
