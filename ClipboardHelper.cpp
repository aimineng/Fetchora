// ClipboardHelper.cpp
#include "ClipboardHelper.h"
#include <QGuiApplication>
#include <QClipboard>

ClipboardHelper::ClipboardHelper(QObject *parent) : QObject(parent) {}

QString ClipboardHelper::getClipboardText() const
{
    if (QGuiApplication::clipboard())
        return QGuiApplication::clipboard()->text();
    return QString();
}