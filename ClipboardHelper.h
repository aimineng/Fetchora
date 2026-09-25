// ClipboardHelper.h
#ifndef CLIPBOARDHELPER_H
#define CLIPBOARDHELPER_H

#include <QObject>
#include <QString>

class ClipboardHelper : public QObject
{
    Q_OBJECT
public:
    explicit ClipboardHelper(QObject *parent = nullptr);
    Q_INVOKABLE QString getClipboardText() const;
};

#endif // CLIPBOARDHELPER_H