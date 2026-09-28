#ifndef NOTIFICATIONMANAGER_H
#define NOTIFICATIONMANAGER_H

#include <QObject>
#include <QString>
#include <QSystemTrayIcon>

class NotificationManager : public QObject
{
    Q_OBJECT
public:
    explicit NotificationManager(QObject *parent = nullptr);
    void setTrayIcon(QSystemTrayIcon *tray);

    Q_INVOKABLE void showNotification(const QString &title, const QString &message);

private:
    QSystemTrayIcon *m_tray = nullptr;
};

#endif // NOTIFICATIONMANAGER_H