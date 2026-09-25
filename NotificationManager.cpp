#include "NotificationManager.h"
#include <QDebug>

NotificationManager::NotificationManager(QObject *parent) : QObject(parent) {}

void NotificationManager::setTrayIcon(QSystemTrayIcon *tray)
{
    m_tray = tray;
}

void NotificationManager::showNotification(const QString &title, const QString &message)
{
    if (m_tray) {
        m_tray->showMessage(title, message, QSystemTrayIcon::Information, 3000);
    } else {
        qWarning() << "Tray icon not available for notification";
    }
}