#ifndef LANGUAGEMANAGER_H
#define LANGUAGEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class QTranslator;

/**
 * LanguageManager - runtime UI language switching.
 *
 * All user-facing strings in this project are written in Simplified Chinese and
 * wrapped in tr(), so Chinese is the *source* language and needs no translator.
 * English (and any language added later) is shipped as a compiled .qm next to
 * the executable or embedded under :/i18n/.
 *
 * Installing or removing a QTranslator makes Qt post a QEvent::LanguageChange
 * to every top-level widget, so .ui files only have to call
 * ui->retranslateUi(this) in changeEvent(). Text that is built in C++ (nav
 * items, task cards, stat cards ...) is rebuilt from the languageChanged()
 * signal instead.
 */
class LanguageManager : public QObject
{
    Q_OBJECT
public:
    static LanguageManager *instance();

    /// One of the codes returned by availableLanguages(), plus "system".
    static QStringList availableLanguages();
    /// Native name of a language code, e.g. "en" -> "English".
    static QString displayName(const QString &code);
    /// Human readable name of the language actually in use ("简体中文" / "English").
    static QString nativeName(const QString &code);

    /// Install the translator for `language` ("system" resolves the OS locale).
    void apply(const QString &language);

    /// The configured value: "system" | "zh" | "en".
    QString requested() const { return m_requested; }
    /// The resolved language actually in effect: "zh" | "en".
    QString current() const { return m_current; }

signals:
    void languageChanged();

private:
    explicit LanguageManager(QObject *parent = nullptr);

    static QString resolve(const QString &language);
    bool installQm(const QString &code);

    QTranslator *m_translator = nullptr;
    QString m_requested = QStringLiteral("system");
    QString m_current;
};

#endif // LANGUAGEMANAGER_H
