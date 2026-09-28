#include "ui/LanguageManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace {

/// Languages the UI is actually translated into. "zh" is the source language.
const char *kCodes[] = {"system", "zh", "en"};

QString qmBaseName(const QString &code)
{
    return QStringLiteral("fetchora_") + code;
}

} // namespace

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
    , m_current(QStringLiteral("zh"))
{
}

LanguageManager *LanguageManager::instance()
{
    static LanguageManager manager;
    return &manager;
}

QStringList LanguageManager::availableLanguages()
{
    QStringList codes;
    for (const char *c : kCodes)
        codes << QString::fromLatin1(c);
    return codes;
}

QString LanguageManager::displayName(const QString &code)
{
    if (code == QLatin1String("en"))
        return QStringLiteral("English");
    if (code == QLatin1String("zh"))
        return QStringLiteral("简体中文");
    if (code == QLatin1String("system"))
        return QObject::tr("跟随系统");
    return code;
}

QString LanguageManager::nativeName(const QString &code)
{
    return displayName(code);
}

QString LanguageManager::resolve(const QString &language)
{
    if (language == QLatin1String("en"))
        return QStringLiteral("en");
    if (language == QLatin1String("zh"))
        return QStringLiteral("zh");
    // "system" (or anything unknown): Chinese locales get Chinese, everything
    // else falls back to English.
    const QLocale locale = QLocale::system();
    return locale.language() == QLocale::Chinese ? QStringLiteral("zh") : QStringLiteral("en");
}

bool LanguageManager::installQm(const QString &code)
{
    if (code == QLatin1String("zh")) {
        // The source language: nothing to install.
        if (m_translator) {
            QCoreApplication::removeTranslator(m_translator);
            delete m_translator;
            m_translator = nullptr;
        }
        return true;
    }

    auto *candidate = new QTranslator(this);
    const QString base = qmBaseName(code);

    // 1) Compiled into the executable by qt_add_translations (:/i18n/*.qm).
    // 2) Dropped next to the executable in translations/ - lets a translator
    //    test a .qm without rebuilding.
    const QStringList searchDirs = {
        QStringLiteral(":/i18n"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
        QCoreApplication::applicationDirPath(),
    };
    bool loaded = false;
    for (const QString &dir : searchDirs) {
        if (candidate->load(base, dir)) {
            loaded = true;
            break;
        }
    }
    if (!loaded) {
        delete candidate;
        return false;
    }

    if (m_translator) {
        QCoreApplication::removeTranslator(m_translator);
        delete m_translator;
    }
    m_translator = candidate;
    QCoreApplication::installTranslator(m_translator);
    return true;
}

void LanguageManager::apply(const QString &language)
{
    const QString wanted = language.isEmpty() ? QStringLiteral("system") : language;
    const QString resolved = resolve(wanted);

    // Re-install even when the resolved code is unchanged: switching between
    // "system" and an explicit code must still refresh the UI on some locales.
    const bool sameLanguage = (resolved == m_current) && (m_requested == wanted);
    m_requested = wanted;
    if (sameLanguage)
        return;

    installQm(resolved);
    m_current = resolved;
    emit languageChanged();
}
