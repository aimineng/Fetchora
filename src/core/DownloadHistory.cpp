#include "DownloadHistory.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

DownloadHistory::DownloadHistory(QObject *parent)
    : QObject(parent)
{
    m_connectionName = QStringLiteral("download-history-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    setDatabasePath(QString());
}

DownloadHistory::~DownloadHistory()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

void DownloadHistory::setDatabasePath(const QString &path)
{
    QString target = path;
    if (target.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        target = dir + QStringLiteral("/history.db");
    }
    if (m_dbPath == target && m_ready)
        return;
    m_dbPath = target;

    if (m_db.isOpen())
        m_db.close();
    m_ready = false;
    open();
    recount();
    emit changed();
}

void DownloadHistory::open()
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        qWarning() << "QSQLITE driver unavailable - history will not persist.";
        m_ready = false;
        return;
    }

    const bool inMemory = (m_dbPath == QLatin1String(":memory:"));
    if (!inMemory)
        QDir().mkpath(QFileInfo(m_dbPath).absolutePath());

    if (QSqlDatabase::contains(m_connectionName))
        m_db = QSqlDatabase::database(m_connectionName, false);
    else
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);

    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) {
        qWarning() << "Cannot open history database:" << m_db.lastError().text();
        m_ready = false;
        return;
    }

    QSqlQuery q(m_db);
    // Rollback journal rather than WAL: history writes are small and infrequent
    // (a couple of rows per finished download). WAL keeps the newest rows in a
    // side file that is invisible until a checkpoint, which made the history
    // look empty both in the UI and to external tools.
    q.exec(QStringLiteral("PRAGMA journal_mode=DELETE"));
    q.exec(QStringLiteral("PRAGMA synchronous=FULL"));

    const QString ddl = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS downloads ("
        "  gid            TEXT PRIMARY KEY,"
        "  name           TEXT,"
        "  uri            TEXT,"
        "  dir            TEXT,"
        "  total_length   INTEGER DEFAULT 0,"
        "  completed      INTEGER DEFAULT 0,"
        "  uploaded       INTEGER DEFAULT 0,"
        "  status         TEXT,"
        "  action         TEXT,"
        "  error_code     TEXT,"
        "  error_message  TEXT,"
        "  info_hash      TEXT,"
        "  is_torrent     INTEGER DEFAULT 0,"
        "  files          TEXT,"
        "  options        TEXT,"
        "  avg_speed      INTEGER DEFAULT 0,"
        "  elapsed        INTEGER DEFAULT 0,"
        "  created_at     TEXT,"
        "  finished_at    TEXT"
        ")");
    if (!q.exec(ddl)) {
        qWarning() << "Cannot create history table:" << q.lastError().text();
        m_ready = false;
        return;
    }
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_downloads_created ON downloads(created_at DESC)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_downloads_status ON downloads(status)"));
    m_ready = true;
}

void DownloadHistory::recount()
{
    if (!m_ready) {
        m_count = m_memoryFallback.size();
        return;
    }
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM downloads")) && q.next())
        m_count = q.value(0).toInt();
    else
        m_count = 0;
}

void DownloadHistory::record(const QVariantMap &entry)
{
    const QString gid = entry.value(QStringLiteral("gid")).toString();
    if (gid.isEmpty())
        return;

    if (!m_ready) {
        for (int i = 0; i < m_memoryFallback.size(); ++i) {
            if (m_memoryFallback[i].toMap().value(QStringLiteral("gid")).toString() == gid) {
                m_memoryFallback[i] = entry;
                emit changed();
                return;
            }
        }
        m_memoryFallback.append(entry);
        m_count = m_memoryFallback.size();
        emit changed();
        return;
    }

    QVariantMap e = entry;
    if (!e.contains(QStringLiteral("created_at")))
        e[QStringLiteral("created_at")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    if (!e.contains(QStringLiteral("finished_at")))
        e[QStringLiteral("finished_at")] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO downloads (gid,name,uri,dir,total_length,completed,uploaded,status,action,"
        "error_code,error_message,info_hash,is_torrent,files,options,avg_speed,elapsed,created_at,finished_at) "
        "VALUES (:gid,:name,:uri,:dir,:total,:completed,:uploaded,:status,:action,"
        ":ecode,:emsg,:ihash,:istorrent,:files,:options,:avg,:elapsed,"
        "COALESCE((SELECT created_at FROM downloads WHERE gid=:gid2), :created), :finished) "
        "ON CONFLICT(gid) DO UPDATE SET "
        "name=:name, uri=:uri, dir=:dir, total_length=:total, completed=:completed, uploaded=:uploaded,"
        "status=:status, action=:action, error_code=:ecode, error_message=:emsg, info_hash=:ihash,"
        "is_torrent=:istorrent, files=:files, options=:options, avg_speed=:avg, elapsed=:elapsed,"
        "finished_at=:finished"));

    q.bindValue(QStringLiteral(":gid"), gid);
    q.bindValue(QStringLiteral(":gid2"), gid);
    q.bindValue(QStringLiteral(":name"), e.value(QStringLiteral("name")));
    q.bindValue(QStringLiteral(":uri"), e.value(QStringLiteral("uri")));
    q.bindValue(QStringLiteral(":dir"), e.value(QStringLiteral("dir")));
    q.bindValue(QStringLiteral(":total"), e.value(QStringLiteral("total_length")).toLongLong());
    q.bindValue(QStringLiteral(":completed"), e.value(QStringLiteral("completed")).toLongLong());
    q.bindValue(QStringLiteral(":uploaded"), e.value(QStringLiteral("uploaded")).toLongLong());
    q.bindValue(QStringLiteral(":status"), e.value(QStringLiteral("status")));
    q.bindValue(QStringLiteral(":action"), e.value(QStringLiteral("action")));
    q.bindValue(QStringLiteral(":ecode"), e.value(QStringLiteral("error_code")));
    q.bindValue(QStringLiteral(":emsg"), e.value(QStringLiteral("error_message")));
    q.bindValue(QStringLiteral(":ihash"), e.value(QStringLiteral("info_hash")));
    q.bindValue(QStringLiteral(":istorrent"), e.value(QStringLiteral("is_torrent")).toBool() ? 1 : 0);
    q.bindValue(QStringLiteral(":files"), e.value(QStringLiteral("files")));
    q.bindValue(QStringLiteral(":options"), e.value(QStringLiteral("options")));
    q.bindValue(QStringLiteral(":avg"), e.value(QStringLiteral("avg_speed")).toLongLong());
    q.bindValue(QStringLiteral(":elapsed"), e.value(QStringLiteral("elapsed")).toLongLong());
    q.bindValue(QStringLiteral(":created"), e.value(QStringLiteral("created_at")));
    q.bindValue(QStringLiteral(":finished"), e.value(QStringLiteral("finished_at")));

    if (!q.exec())
        qWarning() << "history insert failed:" << q.lastError().text() << q.lastQuery();

    flush();
    recount();
    emit changed();
}

void DownloadHistory::flush()
{
    if (!m_ready)
        return;
    // History rows are written one per finished download, so making each one
    // durable keeps the database file readable at all times (and immediately
    // visible to external tools).
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA wal_checkpoint(FULL)"));
}

QVariantList DownloadHistory::fetch(const QString &filter, const QString &search, int limit) const
{
    QVariantList out;

    auto matches = [&](const QVariantMap &row) {
        if (filter != QLatin1String("all") && !filter.isEmpty()) {
            const QString status = row.value(QStringLiteral("status")).toString();
            const QString action = row.value(QStringLiteral("action")).toString();
            if (filter == QLatin1String("active")) {
                if (status != QLatin1String("active") && status != QLatin1String("waiting")
                    && status != QLatin1String("paused"))
                    return false;
            } else if (filter == QLatin1String("complete")) {
                if (status != QLatin1String("complete"))
                    return false;
            } else if (filter == QLatin1String("error")) {
                if (status != QLatin1String("error"))
                    return false;
            } else if (filter == QLatin1String("removed")) {
                if (action != QLatin1String("removed"))
                    return false;
            }
        }
        if (!search.isEmpty()) {
            const QString needle = search.toLower();
            return row.value(QStringLiteral("name")).toString().toLower().contains(needle)
                || row.value(QStringLiteral("uri")).toString().toLower().contains(needle)
                || row.value(QStringLiteral("dir")).toString().toLower().contains(needle);
        }
        return true;
    };

    if (!m_ready) {
        for (const QVariant &v : m_memoryFallback) {
            const QVariantMap row = v.toMap();
            if (matches(row))
                out.append(row);
            if (out.size() >= limit)
                break;
        }
        return out;
    }

    QString sql = QStringLiteral("SELECT * FROM downloads");
    QStringList conditions;
    QVariantMap binds;
    if (filter == QLatin1String("active")) {
        conditions << QStringLiteral("status IN ('active','waiting','paused')");
    } else if (filter == QLatin1String("complete")) {
        conditions << QStringLiteral("status = 'complete'");
    } else if (filter == QLatin1String("error")) {
        conditions << QStringLiteral("status = 'error'");
    } else if (filter == QLatin1String("removed")) {
        conditions << QStringLiteral("action = 'removed'");
    }
    if (!search.isEmpty()) {
        conditions << QStringLiteral("(name LIKE :search OR uri LIKE :search OR dir LIKE :search)");
        binds[QStringLiteral(":search")] = QStringLiteral("%") + search + QStringLiteral("%");
    }
    if (!conditions.isEmpty())
        sql += QStringLiteral(" WHERE ") + conditions.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY COALESCE(finished_at, created_at) DESC LIMIT :limit");
    binds[QStringLiteral(":limit")] = limit;

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (auto it = binds.constBegin(); it != binds.constEnd(); ++it)
        q.bindValue(it.key(), it.value());
    if (!q.exec()) {
        qWarning() << "history query failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        const QSqlQuery &r = q;
        QVariantMap row;
        row[QStringLiteral("gid")] = r.value(QStringLiteral("gid"));
        row[QStringLiteral("name")] = r.value(QStringLiteral("name"));
        row[QStringLiteral("uri")] = r.value(QStringLiteral("uri"));
        row[QStringLiteral("dir")] = r.value(QStringLiteral("dir"));
        row[QStringLiteral("total_length")] = r.value(QStringLiteral("total_length"));
        row[QStringLiteral("completed")] = r.value(QStringLiteral("completed"));
        row[QStringLiteral("uploaded")] = r.value(QStringLiteral("uploaded"));
        row[QStringLiteral("status")] = r.value(QStringLiteral("status"));
        row[QStringLiteral("action")] = r.value(QStringLiteral("action"));
        row[QStringLiteral("error_code")] = r.value(QStringLiteral("error_code"));
        row[QStringLiteral("error_message")] = r.value(QStringLiteral("error_message"));
        row[QStringLiteral("info_hash")] = r.value(QStringLiteral("info_hash"));
        row[QStringLiteral("is_torrent")] = r.value(QStringLiteral("is_torrent"));
        row[QStringLiteral("files")] = r.value(QStringLiteral("files"));
        row[QStringLiteral("options")] = r.value(QStringLiteral("options"));
        row[QStringLiteral("avg_speed")] = r.value(QStringLiteral("avg_speed"));
        row[QStringLiteral("elapsed")] = r.value(QStringLiteral("elapsed"));
        row[QStringLiteral("created_at")] = r.value(QStringLiteral("created_at"));
        row[QStringLiteral("finished_at")] = r.value(QStringLiteral("finished_at"));
        out.append(row);
    }
    return out;
}

bool DownloadHistory::remove(const QString &gid)
{
    if (gid.isEmpty())
        return false;
    if (!m_ready) {
        for (int i = 0; i < m_memoryFallback.size(); ++i) {
            if (m_memoryFallback[i].toMap().value(QStringLiteral("gid")).toString() == gid) {
                m_memoryFallback.removeAt(i);
                m_count = m_memoryFallback.size();
                emit changed();
                return true;
            }
        }
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM downloads WHERE gid = :gid"));
    q.bindValue(QStringLiteral(":gid"), gid);
    const bool ok = q.exec();
    recount();
    emit changed();
    return ok;
}

int DownloadHistory::removeMany(const QStringList &gids)
{
    if (gids.isEmpty())
        return 0;

    int removed = 0;
    if (!m_ready) {
        for (int i = m_memoryFallback.size() - 1; i >= 0; --i) {
            if (gids.contains(m_memoryFallback.at(i).toMap().value(QStringLiteral("gid")).toString())) {
                m_memoryFallback.removeAt(i);
                ++removed;
            }
        }
    } else {
        // One transaction: deleting a few hundred rows one statement at a time
        // is what makes SQLite crawl (a commit per statement means an fsync each).
        const bool inTransaction = m_db.transaction();
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("DELETE FROM downloads WHERE gid = :gid"));
        for (const QString &gid : gids) {
            if (gid.isEmpty())
                continue;
            q.bindValue(QStringLiteral(":gid"), gid);
            if (q.exec())
                removed += q.numRowsAffected();
            else
                qWarning() << "history batch delete failed:" << q.lastError().text();
        }
        if (inTransaction && !m_db.commit())
            qWarning() << "history batch delete commit failed:" << m_db.lastError().text();
    }

    recount();
    emit changed();
    return removed;
}

void DownloadHistory::clear(){
    if (m_ready) {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("DELETE FROM downloads"));
    }
    m_memoryFallback.clear();
    recount();
    emit changed();
}

int DownloadHistory::prune(int keepEntries)
{
    if (keepEntries <= 0)
        return 0;
    int removed = 0;
    if (m_ready) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "DELETE FROM downloads WHERE gid NOT IN ("
            " SELECT gid FROM downloads ORDER BY COALESCE(finished_at, created_at) DESC LIMIT :keep)"));
        q.bindValue(QStringLiteral(":keep"), keepEntries);
        if (q.exec())
            removed = q.numRowsAffected();
        else
            qWarning() << "history prune failed:" << q.lastError().text();
    } else if (m_memoryFallback.size() > keepEntries) {
        removed = m_memoryFallback.size() - keepEntries;
        while (m_memoryFallback.size() > keepEntries)
            m_memoryFallback.removeFirst();
    }
    recount();
    emit changed();
    return removed;
}

bool DownloadHistory::hasCompletedUri(const QString &uri) const
{
    if (uri.isEmpty())
        return false;
    if (!m_ready) {
        for (const QVariant &v : m_memoryFallback) {
            const QVariantMap row = v.toMap();
            if (row.value(QStringLiteral("uri")).toString() == uri
                && row.value(QStringLiteral("status")).toString() == QLatin1String("complete"))
                return true;
        }
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT 1 FROM downloads WHERE uri = :uri AND status = 'complete' LIMIT 1"));
    q.bindValue(QStringLiteral(":uri"), uri);
    if (!q.exec())
        return false;
    return q.next();
}

QVariantMap DownloadHistory::statistics() const
{
    QVariantMap stats;
    qint64 downloaded = 0;
    qint64 uploaded = 0;
    int completed = 0;
    int failed = 0;

    if (m_ready) {
        QSqlQuery q(m_db);
        if (q.exec(QStringLiteral(
                "SELECT COUNT(*), COALESCE(SUM(completed),0), COALESCE(SUM(uploaded),0) FROM downloads WHERE status='complete'"))) {
            if (q.next()) {
                completed = q.value(0).toInt();
                downloaded = q.value(1).toLongLong();
                uploaded = q.value(2).toLongLong();
            }
        }
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM downloads WHERE status='error'")) && q.next())
            failed = q.value(0).toInt();
    } else {
        for (const QVariant &v : m_memoryFallback) {
            const QVariantMap row = v.toMap();
            if (row.value(QStringLiteral("status")).toString() == QLatin1String("complete")) {
                ++completed;
                downloaded += row.value(QStringLiteral("completed")).toLongLong();
                uploaded += row.value(QStringLiteral("uploaded")).toLongLong();
            } else if (row.value(QStringLiteral("status")).toString() == QLatin1String("error")) {
                ++failed;
            }
        }
    }

    stats[QStringLiteral("total")] = m_count;
    stats[QStringLiteral("completed")] = completed;
    stats[QStringLiteral("failed")] = failed;
    stats[QStringLiteral("downloaded")] = downloaded;
    stats[QStringLiteral("uploaded")] = uploaded;
    return stats;
}
