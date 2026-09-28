#include "ui/FluentTheme.h"

#include "SettingsManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QPalette>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleHints>
#include <QWidget>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#endif

namespace {

/// Cache the font families Qt knows about - Qt.fontFamilies() is cheap but this
/// is called from several places.
const QStringList &availableFamilies()
{
    static const QStringList families = []() {
        QFontDatabase db;
        return db.families();
    }();
    return families;
}

QString hex(const QColor &c)
{
    // Include the alpha channel only when it is not opaque so the generated
    // sheet stays readable in a debugger.
    if (c.alpha() == 255)
        return c.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(QString::number(c.alphaF(), 'f', 3));
}

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t,
                            a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF() * (1 - t) + b.blueF() * t,
                            a.alphaF() * (1 - t) + b.alphaF() * t);
}

QColor lighten(const QColor &c, qreal amount) { return mix(c, QColor(Qt::white), amount); }
QColor darken(const QColor &c, qreal amount) { return mix(c, QColor(Qt::black), amount); }

/// WCAG relative luminance, used to pick a readable foreground for an accent.
qreal luminance(const QColor &c)
{
    return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
}

// ---------------------------------------------------------------------------
//  Typography state
//
//  The families and sizes live here rather than in the style sheet. A Qt style
//  sheet rule that declares `font-size` REPLACES the widget font, so every
//  setFont() call in the code base was silently ignored and every label ended up
//  at 14 px - the type ramp collapsed and widgets reserved the wrong height.
//  Sizes are applied in C++ by applyTypography(); the sheet only carries the
//  family, which is the one thing that has to be inherited.
// ---------------------------------------------------------------------------
QString g_uiFamily;      ///< resolved family ("" = platform default)
QString g_monoFamily;
int g_uiSize = 14;       ///< base UI size in px

/// The family the UI font is resolved from, best first. An empty entry means
/// "keep the platform's own UI font", which is the right answer on macOS.
QStringList uiFamilyCandidates()
{
#if defined(Q_OS_WIN)
    // Microsoft YaHei UI renders Simplified Chinese and Latin in the same
    // weight; Segoe UI Variable is the Windows 11 Latin face but falls back to
    // YaHei for every CJK glyph, which mixes two typefaces in one line.
    return {QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Microsoft YaHei"),
            QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")};
#elif defined(Q_OS_MACOS)
    return {QStringLiteral("PingFang SC"), QStringLiteral("Helvetica Neue"), QString()};
#else
    return {QStringLiteral("Noto Sans CJK SC"), QStringLiteral("Source Han Sans SC"),
            QStringLiteral("Noto Sans SC"), QStringLiteral("WenQuanYi Micro Hei"),
            QStringLiteral("DejaVu Sans"), QString()};
#endif
}

QStringList monoFamilyCandidates()
{
#if defined(Q_OS_WIN)
    return {QStringLiteral("Cascadia Mono"), QStringLiteral("Cascadia Code"),
            QStringLiteral("Consolas"), QStringLiteral("Courier New")};
#elif defined(Q_OS_MACOS)
    return {QStringLiteral("SF Mono"), QStringLiteral("Menlo"), QStringLiteral("Monaco")};
#else
    return {QStringLiteral("JetBrains Mono"), QStringLiteral("Noto Sans Mono"),
            QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Liberation Mono")};
#endif
}

} // namespace

// ============================================================================
//  Glyphs - every code point below was verified against SegoeIcons.ttf
// ============================================================================
const QChar FluentTheme::Glyph::Download      = QChar(0xE896);
const QChar FluentTheme::Glyph::Magnet        = QChar(0xE8B1);
const QChar FluentTheme::Glyph::History       = QChar(0xE81C);
const QChar FluentTheme::Glyph::Settings      = QChar(0xE713);
const QChar FluentTheme::Glyph::Add           = QChar(0xE710);
const QChar FluentTheme::Glyph::AddFile       = QChar(0xE8E5);
const QChar FluentTheme::Glyph::Play          = QChar(0xE768);
const QChar FluentTheme::Glyph::Pause         = QChar(0xE769);
const QChar FluentTheme::Glyph::Stop          = QChar(0xE71A);
const QChar FluentTheme::Glyph::Delete        = QChar(0xE74D);
const QChar FluentTheme::Glyph::Folder        = QChar(0xE8B7);
const QChar FluentTheme::Glyph::OpenFile      = QChar(0xE8E5);
const QChar FluentTheme::Glyph::Copy          = QChar(0xE8C8);
const QChar FluentTheme::Glyph::Link          = QChar(0xE71B);
const QChar FluentTheme::Glyph::Refresh       = QChar(0xE72C);
const QChar FluentTheme::Glyph::Search        = QChar(0xE721);
const QChar FluentTheme::Glyph::ChevronRight  = QChar(0xE76C);
const QChar FluentTheme::Glyph::ChevronDown   = QChar(0xE70D);
const QChar FluentTheme::Glyph::ChevronUp     = QChar(0xE70E);
const QChar FluentTheme::Glyph::Up            = QChar(0xE74A);
const QChar FluentTheme::Glyph::Down          = QChar(0xE74B);
const QChar FluentTheme::Glyph::Info          = QChar(0xE946);
const QChar FluentTheme::Glyph::Warning       = QChar(0xE7BA);
const QChar FluentTheme::Glyph::Error         = QChar(0xEA39);
const QChar FluentTheme::Glyph::Success       = QChar(0xE930);
const QChar FluentTheme::Glyph::Speed         = QChar(0xE9D9);
const QChar FluentTheme::Glyph::Globe         = QChar(0xE774);
const QChar FluentTheme::Glyph::Shield        = QChar(0xEA18);
const QChar FluentTheme::Glyph::Disk          = QChar(0xEDA2);
const QChar FluentTheme::Glyph::Clock         = QChar(0xE823);
const QChar FluentTheme::Glyph::Peer          = QChar(0xE716);
const QChar FluentTheme::Glyph::Server        = QChar(0xE968);
const QChar FluentTheme::Glyph::File          = QChar(0xE7C3);
const QChar FluentTheme::Glyph::Filter        = QChar(0xE71C);
const QChar FluentTheme::Glyph::More          = QChar(0xE712);
const QChar FluentTheme::Glyph::Close         = QChar(0xE711);
const QChar FluentTheme::Glyph::Minimize      = QChar(0xE921);
const QChar FluentTheme::Glyph::Maximize      = QChar(0xE922);
const QChar FluentTheme::Glyph::Restore       = QChar(0xE923);
const QChar FluentTheme::Glyph::Back          = QChar(0xE72B);
const QChar FluentTheme::Glyph::Save          = QChar(0xE74E);
const QChar FluentTheme::Glyph::Edit          = QChar(0xE70F);
const QChar FluentTheme::Glyph::Palette       = QChar(0xE790);
const QChar FluentTheme::Glyph::Rocket        = QChar(0xE945);
const QChar FluentTheme::Glyph::Console       = QChar(0xE756);
const QChar FluentTheme::Glyph::Check         = QChar(0xE73E);
const QChar FluentTheme::Glyph::Power         = QChar(0xE7E8);
const QChar FluentTheme::Glyph::Torrent       = QChar(0xE958);
const QChar FluentTheme::Glyph::Upload        = QChar(0xE898);
const QChar FluentTheme::Glyph::Lightbulb     = QChar(0xEA80);
const QChar FluentTheme::Glyph::Home          = QChar(0xE80F);
const QChar FluentTheme::Glyph::Tune          = QChar(0xE9E9);
const QChar FluentTheme::Glyph::Media         = QChar(0xE714);
const QChar FluentTheme::Glyph::Music         = QChar(0xE8D6);
const QChar FluentTheme::Glyph::Archive       = QChar(0xF012);
const QChar FluentTheme::Glyph::App           = QChar(0xE7EF);
const QChar FluentTheme::Glyph::Pdf           = QChar(0xE8A5);
const QChar FluentTheme::Glyph::Image         = QChar(0xEB9F);
const QChar FluentTheme::Glyph::Reveal        = QChar(0xE7B3);
const QChar FluentTheme::Glyph::Hide          = QChar(0xED1A);
const QChar FluentTheme::Glyph::Indeterminate = QChar(0xE738);

// ============================================================================
//  Singleton
// ============================================================================
FluentTheme *FluentTheme::instance()
{
    static FluentTheme *theme = new FluentTheme(qApp);
    return theme;
}

FluentTheme::FluentTheme(QObject *parent)
    : QObject(parent)
    , m_systemAccent(0x0F, 0x6C, 0xBD) // Fluent default blue
{
    readSystemAccent();
    readSystemDarkMode();

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QGuiApplication::styleHints()) {
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) {
                    readSystemDarkMode();
                    refresh();
                });
    }
#endif
}

void FluentTheme::attach(SettingsManager *settings)
{
    if (!settings)
        return;
    m_mode = settings->theme();
    connect(settings, &SettingsManager::themeChanged, this, [this, settings]() {
        m_mode = settings->theme();
        refresh();
    });
    connect(settings, &SettingsManager::accentColorChanged, this, &FluentTheme::refresh);
    connect(settings, &SettingsManager::settingsRevisionChanged, this, [this, settings]() {
        const QString mode = settings->theme();
        if (mode != m_mode) {
            m_mode = mode;
            refresh();
        }
    });
    refresh();
}

QString FluentTheme::mode() const { return m_mode; }

void FluentTheme::setMode(const QString &mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    refresh();
}

bool FluentTheme::isDark() const
{
    if (m_mode == QLatin1String("system"))
        return m_systemDark;
    return m_mode != QLatin1String("light");
}

void FluentTheme::refresh()
{
    readSystemAccent();
    readSystemDarkMode();
    emit changed();
}

void FluentTheme::readSystemDarkMode()
{
#ifdef Q_OS_WIN
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                  QSettings::NativeFormat);
    const QVariant v = reg.value(QStringLiteral("AppsUseLightTheme"));
    if (v.isValid())
        m_systemDark = (v.toInt() == 0);
#else
    m_systemDark = true;
#endif
}

void FluentTheme::readSystemAccent()
{
#ifdef Q_OS_WIN
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM"),
                  QSettings::NativeFormat);
    const QVariant v = reg.value(QStringLiteral("AccentColor"));
    if (v.isValid()) {
        bool ok = false;
        const uint abgr = v.toUInt(&ok);
        if (ok) {
            // Stored as 0xAABBGGRR.
            const QColor c(int(abgr & 0xFF), int((abgr >> 8) & 0xFF), int((abgr >> 16) & 0xFF));
            if (c.isValid())
                m_systemAccent = c;
        }
    }
#endif
}

// ============================================================================
//  Palette
// ============================================================================
QColor FluentTheme::background() const { return isDark() ? QColor(0x20, 0x20, 0x20) : QColor(0xF3, 0xF3, 0xF3); }
QColor FluentTheme::backgroundAlt() const { return isDark() ? QColor(0x1C, 0x1C, 0x1C) : QColor(0xEE, 0xEE, 0xEE); }
QColor FluentTheme::layer() const { return isDark() ? QColor(0x27, 0x27, 0x27) : QColor(0xFF, 0xFF, 0xFF); }
QColor FluentTheme::card() const { return isDark() ? QColor(0x2B, 0x2B, 0x2B) : QColor(0xFF, 0xFF, 0xFF); }
QColor FluentTheme::cardSecondary() const { return isDark() ? QColor(0x32, 0x32, 0x32) : QColor(0xF9, 0xF9, 0xF9); }
QColor FluentTheme::cardTertiary() const { return isDark() ? QColor(0x38, 0x38, 0x38) : QColor(0xF0, 0xF0, 0xF0); }
QColor FluentTheme::stroke() const { return isDark() ? QColor(0x3A, 0x3A, 0x3A) : QColor(0xE1, 0xE1, 0xE1); }
QColor FluentTheme::strokeSubtle() const { return isDark() ? QColor(0x2E, 0x2E, 0x2E) : QColor(0xEA, 0xEA, 0xEA); }
QColor FluentTheme::divider() const { return isDark() ? QColor(0x2A, 0x2A, 0x2A) : QColor(0xEB, 0xEB, 0xEB); }
QColor FluentTheme::textPrimary() const { return isDark() ? QColor(0xFF, 0xFF, 0xFF) : QColor(0x1A, 0x1A, 0x1A); }
QColor FluentTheme::textSecondary() const { return isDark() ? QColor(0xC5, 0xC5, 0xC5) : QColor(0x5C, 0x5C, 0x5C); }
QColor FluentTheme::textTertiary() const { return isDark() ? QColor(0x8B, 0x8B, 0x8B) : QColor(0x8A, 0x8A, 0x8A); }
QColor FluentTheme::textDisabled() const { return isDark() ? QColor(0x5D, 0x5D, 0x5D) : QColor(0xB0, 0xB0, 0xB0); }
QColor FluentTheme::controlFill() const { return isDark() ? QColor(0x2D, 0x2D, 0x2D) : QColor(0xFD, 0xFD, 0xFD); }
QColor FluentTheme::controlFillHover() const { return isDark() ? QColor(0x38, 0x38, 0x38) : QColor(0xF5, 0xF5, 0xF5); }
QColor FluentTheme::controlFillPressed() const { return isDark() ? QColor(0x41, 0x41, 0x41) : QColor(0xED, 0xED, 0xED); }
QColor FluentTheme::controlStroke() const { return isDark() ? QColor(0x4A, 0x4A, 0x4A) : QColor(0xD6, 0xD6, 0xD6); }
QColor FluentTheme::subtleHover() const { return isDark() ? QColor(0x2E, 0x2E, 0x2E) : QColor(0xF0, 0xF0, 0xF0); }
QColor FluentTheme::subtlePressed() const { return isDark() ? QColor(0x38, 0x38, 0x38) : QColor(0xE6, 0xE6, 0xE6); }
QColor FluentTheme::trackFill() const { return isDark() ? QColor(0x3A, 0x3A, 0x3A) : QColor(0xDB, 0xDB, 0xDB); }

QColor FluentTheme::accent() const
{
    // A very light or very dark system accent is unusable as a Fluent accent.
    QColor c = m_systemAccent;
    if (!c.isValid())
        c = QColor(0x0F, 0x6C, 0xBD);
    if (luminance(c) > 0.85)
        c = darken(c, 0.35);
    return c;
}

QColor FluentTheme::accentLight1() const { return lighten(accent(), 0.15); }
QColor FluentTheme::accentDark1() const { return darken(accent(), 0.18); }
QColor FluentTheme::accentDark2() const { return darken(accent(), 0.35); }
QColor FluentTheme::onAccent() const { return luminance(accent()) > 0.62 ? QColor(0x1A, 0x1A, 0x1A) : QColor(Qt::white); }

QColor FluentTheme::success() const { return isDark() ? QColor(0x6C, 0xCB, 0x5F) : QColor(0x0F, 0x7B, 0x0F); }
QColor FluentTheme::caution() const { return isDark() ? QColor(0xFC, 0xE1, 0x00) : QColor(0x9D, 0x5D, 0x00); }
QColor FluentTheme::critical() const { return isDark() ? QColor(0xFF, 0x99, 0xA4) : QColor(0xC4, 0x2B, 0x1C); }
QColor FluentTheme::info() const { return isDark() ? QColor(0x60, 0xCD, 0xFF) : QColor(0x0F, 0x6C, 0xBD); }
QColor FluentTheme::successBg() const { return isDark() ? QColor(0x39, 0x3D, 0x1B) : QColor(0xDF, 0xF6, 0xDD); }
QColor FluentTheme::cautionBg() const { return isDark() ? QColor(0x43, 0x35, 0x19) : QColor(0xFF, 0xF4, 0xCE); }
QColor FluentTheme::criticalBg() const { return isDark() ? QColor(0x44, 0x27, 0x26) : QColor(0xFD, 0xE7, 0xE9); }
QColor FluentTheme::infoBg() const { return isDark() ? QColor(0x1B, 0x3A, 0x4B) : QColor(0xE5, 0xF1, 0xFB); }

QColor FluentTheme::acrylicTint() const
{
    return isDark() ? QColor(33, 33, 33, 153) : QColor(247, 247, 247, 153);
}

// ============================================================================
//  Typography
// ============================================================================
QString FluentTheme::resolveFamily(const QStringList &candidates, const QString &fallback)
{
    const QStringList &available = availableFamilies();
    for (const QString &candidate : candidates) {
        if (available.contains(candidate, Qt::CaseInsensitive))
            return candidate;
    }
    return fallback;
}

QString FluentTheme::iconFont()
{
    /*
        A single family name is mandatory here. Qt's font matching does not
        understand CSS-style comma separated fallback lists: given
        "Segoe Fluent Icons, Segoe MDL2 Assets" it looks up that whole string as
        one family, fails, and silently falls back to the UI font - which has no
        glyphs in the private use area. Every icon then renders as nothing at
        all, with no warning of any kind.
    */
    static const QString family = resolveFamily(
        {QStringLiteral("Segoe Fluent Icons"), QStringLiteral("Segoe MDL2 Assets")}, QString());
    return family;
}

QString FluentTheme::monoFont()
{
    if (!g_monoFamily.isEmpty())
        return g_monoFamily;
    g_monoFamily = resolveFamily(monoFamilyCandidates(), QStringLiteral("monospace"));
    return g_monoFamily;
}

QString FluentTheme::uiFontFamily() { return g_uiFamily; }

/// Resolves the UI family once, the first time anything asks for a font.
static void ensureUiFamily()
{
    static bool resolved = false;
    if (resolved)
        return;
    resolved = true;
    g_uiFamily = FluentTheme::pickFamily(uiFamilyCandidates(), QString());
}

QFont FluentTheme::monoFont(int pixelSize)
{
    QFont f(monoFont());
    f.setPixelSize(pixelSize);
    f.setHintingPreference(QFont::PreferDefaultHinting);
    return f;
}

void FluentTheme::setUiFontFamily(const QString &family)
{
    const QString resolved = family.isEmpty() ? resolveFamily(uiFamilyCandidates(), QString())
                                              : family;
    if (resolved == g_uiFamily)
        return;
    g_uiFamily = resolved;
    emit instance()->changed();
}

int FluentTheme::uiFontSize() { return g_uiSize; }

void FluentTheme::setUiFontSize(int pixelSize)
{
    const int size = qBound(11, pixelSize, 20);
    if (size == g_uiSize)
        return;
    g_uiSize = size;
    emit instance()->changed();
}

QFont FluentTheme::uiFont(int pixelSize, QFont::Weight weight)
{
    ensureUiFamily();
    // Start from the platform font so an unresolved family still gives a native
    // result (this is what macOS wants: the system UI font has no stable name).
    QFont f = qApp ? qApp->font() : QFont();
    if (!g_uiFamily.isEmpty())
        f.setFamily(g_uiFamily);
    f.setPixelSize(pixelSize);
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferDefaultHinting);
    return f;
}

QFont FluentTheme::iconFont(int pixelSize)
{
    QFont f(FluentTheme::iconFont());
    f.setPixelSize(pixelSize);
    f.setHintingPreference(QFont::PreferDefaultHinting);
    return f;
}

// ============================================================================
//  Helpers
// ============================================================================
QChar FluentTheme::fileGlyph(const QString &fileName)
{
    const QString n = fileName.toLower();
    auto ends = [&n](std::initializer_list<const char *> exts) {
        for (const char *e : exts) {
            if (n.endsWith(QLatin1String(e)))
                return true;
        }
        return false;
    };
    if (ends({".torrent"})) return Glyph::Torrent;
    if (ends({".iso", ".img", ".dmg"})) return Glyph::Disk;
    if (ends({".mp4", ".mkv", ".avi", ".mov", ".webm", ".flv", ".ts"})) return Glyph::Media;
    if (ends({".mp3", ".flac", ".wav", ".m4a", ".aac", ".ogg"})) return Glyph::Music;
    if (ends({".zip", ".rar", ".7z", ".tar", ".gz", ".xz"})) return Glyph::Archive;
    if (ends({".exe", ".msi", ".bat", ".cmd"})) return Glyph::App;
    if (ends({".pdf"})) return Glyph::Pdf;
    if (ends({".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".svg"})) return Glyph::Image;
    return Glyph::File;
}

QString FluentTheme::formatSize(double bytes, int precision)
{
    if (!(bytes > 0))
        return QStringLiteral("0 B");
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int unit = 0;
    while (bytes >= 1024.0 && unit < 5) {
        bytes /= 1024.0;
        ++unit;
    }
    const int digits = precision >= 0 ? precision : (unit == 0 ? 0 : 2);
    return QStringLiteral("%1 %2").arg(QString::number(bytes, 'f', digits), units[unit]);
}

QString FluentTheme::formatSpeed(double bytesPerSecond)
{
    if (!(bytesPerSecond > 0))
        return QStringLiteral("--");
    return formatSize(bytesPerSecond, bytesPerSecond < 1024 * 1024 ? 1 : 2) + QStringLiteral("/s");
}

QString FluentTheme::formatDuration(double seconds)
{
    if (!(seconds >= 0))
        return QStringLiteral("--");
    const qint64 s = qint64(seconds + 0.5);
    const qint64 h = s / 3600;
    const qint64 m = (s % 3600) / 60;
    const qint64 sec = s % 60;
    if (h > 0)
        return QStringLiteral("%1h %2m").arg(h).arg(m, 2, 10, QLatin1Char('0'));
    if (m > 0)
        return QStringLiteral("%1m %2s").arg(m).arg(sec, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1s").arg(sec);
}

QString FluentTheme::formatEta(double remainingBytes, double speed)
{
    if (!(speed > 0) || !(remainingBytes > 0))
        return QStringLiteral("--");
    return formatDuration(remainingBytes / speed);
}

QString FluentTheme::statusLabel(const QString &status)
{
    // Kept in sync with Aria2Manager::statusLabel().
    if (status == QLatin1String("active")) return QObject::tr("下载中");
    if (status == QLatin1String("waiting")) return QObject::tr("等待中");
    if (status == QLatin1String("paused")) return QObject::tr("已暂停");
    if (status == QLatin1String("complete")) return QObject::tr("已完成");
    if (status == QLatin1String("error")) return QObject::tr("失败");
    if (status == QLatin1String("removed")) return QObject::tr("已移除");
    return status;
}

QColor FluentTheme::statusColor(const QString &status) const
{
    if (status == QLatin1String("active")) return accent();
    if (status == QLatin1String("waiting")) return textSecondary();
    if (status == QLatin1String("paused")) return caution();
    if (status == QLatin1String("complete")) return success();
    if (status == QLatin1String("error")) return critical();
    return textTertiary();
}

QChar FluentTheme::statusGlyph(const QString &status)
{
    if (status == QLatin1String("active")) return Glyph::Download;
    if (status == QLatin1String("waiting")) return Glyph::Clock;
    if (status == QLatin1String("paused")) return Glyph::Pause;
    if (status == QLatin1String("complete")) return Glyph::Success;
    if (status == QLatin1String("error")) return Glyph::Error;
    return Glyph::Info;
}

QString FluentTheme::prettyInfoHash(const QString &hash)
{
    if (hash.size() < 16)
        return hash;
    return hash.left(8).toUpper() + QStringLiteral("...") + hash.right(8).toUpper();
}

// ============================================================================
//  Typography + palette application
// ============================================================================
void FluentTheme::ensureFontsResolved()
{
    ensureUiFamily();
    monoFont();
    if (qApp)
        qApp->setFont(uiFont(g_uiSize));
}

void FluentTheme::applyApplicationPalette()
{
    /*
        Style sheets only cover widgets a rule names. Everything else - scroll
        area viewports, item views, splitters, menus, dialogs, message boxes -
        paints from the QPalette, which Qt initialises from the *system* theme.
        On a dark Windows install that meant the gaps between history rows and
        the empty space behind a scroll area came out black while the rest of the
        window was light. Setting the palette keeps the two in step.
    */
    if (!qApp)
        return;
    const FluentTheme *t = instance();

    QPalette p;
    p.setColor(QPalette::Window, t->background());
    p.setColor(QPalette::WindowText, t->textPrimary());
    p.setColor(QPalette::Base, t->backgroundAlt());
    p.setColor(QPalette::AlternateBase, t->card());
    p.setColor(QPalette::Text, t->textPrimary());
    p.setColor(QPalette::Button, t->controlFill());
    p.setColor(QPalette::ButtonText, t->textPrimary());
    p.setColor(QPalette::BrightText, t->critical());
    p.setColor(QPalette::Highlight, t->accent());
    p.setColor(QPalette::HighlightedText, t->onAccent());
    p.setColor(QPalette::ToolTipBase, t->cardSecondary());
    p.setColor(QPalette::ToolTipText, t->textPrimary());
    p.setColor(QPalette::PlaceholderText, t->textTertiary());
    p.setColor(QPalette::Link, t->accent());
    p.setColor(QPalette::LinkVisited, t->accentDark1());
    p.setColor(QPalette::Light, t->divider());
    p.setColor(QPalette::Midlight, t->divider());
    p.setColor(QPalette::Mid, t->stroke());
    p.setColor(QPalette::Dark, t->stroke());
    p.setColor(QPalette::Shadow, t->isDark() ? QColor(0, 0, 0) : QColor(0x40, 0x40, 0x40));

    p.setColor(QPalette::Disabled, QPalette::Text, t->textDisabled());
    p.setColor(QPalette::Disabled, QPalette::WindowText, t->textDisabled());
    p.setColor(QPalette::Disabled, QPalette::ButtonText, t->textDisabled());
    p.setColor(QPalette::Disabled, QPalette::Base, t->backgroundAlt());
    p.setColor(QPalette::Disabled, QPalette::Button, t->backgroundAlt());

    qApp->setPalette(p);
}

QString FluentTheme::styleSheetFor(const QString &className)
{
    const QString a = hex(accent());
    const QString aHover = hex(accentLight1());
    const QString aPress = hex(accentDark1());
    const QString onA = hex(onAccent());

    if (className == QLatin1String("accentButton")) {
        return QStringLiteral(
                   "QPushButton { background: %1; color: %2; border: 1px solid %3;"
                   "              border-radius: %4px; padding: 0 16px; }"
                   "QPushButton:hover { background: %5; }"
                   "QPushButton:pressed { background: %6; }"
                   "QPushButton:disabled { background: %7; color: %8; border-color: transparent; }")
            .arg(a, onA, hex(lighten(accent(), 0.10)))
            .arg(RadiusMedium)
            .arg(aHover, aPress)
            .arg(hex(isDark() ? QColor(0x33, 0x33, 0x33) : QColor(0xE8, 0xE8, 0xE8)), hex(textDisabled()));
    }
    return QString();
}

QString FluentTheme::buildStyleSheet() const
{
    const QString bg = hex(background());
    const QString bgAlt = hex(backgroundAlt());
    const QString cardCol = hex(card());
    const QString cardSec = hex(cardSecondary());
    const QString cardTer = hex(cardTertiary());
    const QString strokeCol = hex(stroke());
    const QString strokeSub = hex(strokeSubtle());
    const QString divCol = hex(divider());
    const QString tPrim = hex(textPrimary());
    const QString tSec = hex(textSecondary());
    const QString tTer = hex(textTertiary());
    const QString tDis = hex(textDisabled());
    const QString fill = hex(controlFill());
    const QString fillHover = hex(controlFillHover());
    const QString fillPress = hex(controlFillPressed());
    const QString ctlStroke = hex(controlStroke());
    const QString subHover = hex(subtleHover());
    const QString subPress = hex(subtlePressed());
    const QString track = hex(trackFill());

    const QString a = hex(accent());
    const QString aHover = hex(accentLight1());
    const QString aPress = hex(accentDark1());
    const QString onA = hex(onAccent());
    const QString ok = hex(success());
    const QString warn = hex(caution());
    const QString bad = hex(critical());
    const QString acc = hex(accent());

    const QString uiFamily = uiFontFamily();
    const QString mono = monoFont();

    // Font families are declared in the sheet on purpose: once a widget has a
    // stylesheet, Qt resolves its font through the sheet, so anything not
    // mentioned would silently revert to the platform default.
    const QString fontRule = uiFamily.isEmpty()
                                 ? QString()
                                 : QStringLiteral("font-family: \"%1\";").arg(uiFamily);
    const QString monoRule = QStringLiteral("font-family: \"%1\";").arg(mono);

    QString qss;
    qss.reserve(24000);

    qss += QStringLiteral(R"(
/* ===================== base ===================== */
QWidget { %FONT% font-size: 14px; color: %TPRIM%; }
QMainWindow, QDialog { background: %BG%; }
QWidget#pageRoot, QWidget#contentRoot { background: transparent; }

QToolTip {
    background: %CARDSEC%; color: %TPRIM%;
    border: 1px solid %STROKE%; border-radius: %RMED%px; padding: 6px 9px; font-size: 12px;
}

/* ===================== buttons ===================== */
/* FluentButton paints itself so the glyph and the text can be laid out
   together; only the plain QPushButton rules below apply to stock buttons. */
QPushButton {
    background: %FILL%; color: %TPRIM%;
    border: 1px solid %STROKESUB%; border-radius: %RMED%px;
    padding: 0 16px; min-height: 32px; %FONT% font-size: 14px;
}
QPushButton:hover { background: %FILLHOVER%; }
QPushButton:pressed { background: %FILLPRESS%; }
QPushButton:disabled { background: %FILL%; color: %TDIS%; border-color: %STROKESUB%; }
QPushButton:focus { outline: none; }
QPushButton[fluentRole] { background: transparent; border: none; padding: 0; }

QPushButton[fluentRole="accent"] {
    background: %A%; color: %ONA%; border: 1px solid %AHOVER%;
}
QPushButton[fluentRole="accent"]:hover { background: %AHOVER%; }
QPushButton[fluentRole="accent"]:pressed { background: %APRESS%; }
QPushButton[fluentRole="accent"]:disabled { background: %FILLPRESS%; color: %TDIS%; border-color: transparent; }

QPushButton[fluentRole="subtle"], QPushButton[fluentRole="hyperlink"] {
    background: transparent; border-color: transparent;
}
QPushButton[fluentRole="subtle"]:hover, QPushButton[fluentRole="hyperlink"]:hover { background: %SUBHOVER%; }
QPushButton[fluentRole="subtle"]:pressed, QPushButton[fluentRole="hyperlink"]:pressed { background: %SUBPRESS%; }
QPushButton[fluentRole="hyperlink"] { color: %A%; }
QPushButton[fluentRole="outline"] { background: transparent; border-color: %STROKE%; }
QPushButton[fluentRole="outline"]:hover { background: %FILLHOVER%; }
QPushButton[fluentRole="danger"] { background: %BAD%; color: #FFFFFF; border-color: transparent; }
QPushButton[fluentRole="danger"]:hover { background: %BADHOVER%; }

QPushButton[fluentCompact="true"] { min-height: 26px; padding: 0 9px; font-size: 12px; }

/* ===================== inputs ===================== */
QLineEdit, QPlainTextEdit, QTextEdit {
    background: %FILL%; color: %TPRIM%;
    border: 1px solid %STROKESUB%; border-bottom: 1px solid %STROKE%;
    border-radius: %RSMALL%px; padding: 0 10px; min-height: 32px;
    selection-background-color: %A%; selection-color: %ONA%;
    %FONT% font-size: 14px;
}
QPlainTextEdit, QTextEdit { padding: 8px 10px; }
QLineEdit:hover, QPlainTextEdit:hover, QTextEdit:hover { background: %FILLHOVER%; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus {
    border: 1px solid %CTLSTROKE%; border-bottom: 2px solid %A%; background: %FILL%;
}
QLineEdit:disabled, QPlainTextEdit:disabled, QTextEdit:disabled { color: %TDIS%; background: %BGALT%; }
QLineEdit[fluentMono="true"], QPlainTextEdit[fluentMono="true"] { %MONO% font-size: 12px; }

QSpinBox, QDoubleSpinBox {
    background: %FILL%; color: %TPRIM%;
    border: 1px solid %STROKESUB%; border-bottom: 1px solid %STROKE%;
    border-radius: %RSMALL%px; padding: 0 4px 0 10px; min-height: 32px;
    selection-background-color: %A%; %FONT% font-size: 14px;
}
QSpinBox:focus, QDoubleSpinBox:focus { border-bottom: 2px solid %A%; }
QSpinBox::up-button, QDoubleSpinBox::up-button {
    subcontrol-origin: border; subcontrol-position: top right;
    width: 26px; height: 15px; border: none; border-radius: 3px; background: transparent;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
    subcontrol-origin: border; subcontrol-position: bottom right;
    width: 26px; height: 15px; border: none; border-radius: 3px; background: transparent;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background: %SUBHOVER%; }
QSpinBox::up-button:pressed, QDoubleSpinBox::up-button:pressed,
QSpinBox::down-button:pressed, QDoubleSpinBox::down-button:pressed { background: %SUBPRESS%; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
    image: none; width: 0; height: 0;
    border-left: 4px solid transparent; border-right: 4px solid transparent;
    border-bottom: 5px solid %TSEC%;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
    image: none; width: 0; height: 0;
    border-left: 4px solid transparent; border-right: 4px solid transparent;
    border-top: 5px solid %TSEC%;
}

QComboBox {
    background: %FILL%; color: %TPRIM%;
    border: 1px solid %STROKESUB%; border-radius: %RSMALL%px;
    padding: 0 28px 0 10px; min-height: 32px; %FONT% font-size: 14px;
}
QComboBox:hover { background: %FILLHOVER%; }
QComboBox:on { background: %FILLPRESS%; border-color: %CTLSTROKE%; }
QComboBox:disabled { color: %TDIS%; }
QComboBox::drop-down { border: none; width: 26px; }
QComboBox::down-arrow {
    image: none; width: 0; height: 0;
    border-left: 4px solid transparent; border-right: 4px solid transparent;
    border-top: 5px solid %TSEC%; margin-right: 8px;
}
QComboBox QAbstractItemView {
    background: %CARDSEC%; color: %TPRIM%;
    border: 1px solid %STROKE%; border-radius: %RLARGE%px;
    padding: 4px; outline: none;
    selection-background-color: %SUBHOVER%; selection-color: %TPRIM%;
}
QComboBox QAbstractItemView::item { min-height: 30px; padding-left: 8px; border-radius: %RSMALL%px; }

/* ===================== check / radio ===================== */
/* The check mark and the sliding switch thumb are painted by
   FluentCheckBox / FluentSwitch, because Qt style sheets cannot express a
   glyph and the Fluent switch is not a plain box. */
QCheckBox, QRadioButton { color: %TPRIM%; spacing: 9px; %FONT% font-size: 14px; min-height: 32px; }
QCheckBox:disabled, QRadioButton:disabled { color: %TDIS%; }
QCheckBox::indicator, QRadioButton::indicator { width: 19px; height: 19px; }
QCheckBox::indicator { border: 1px solid %CTLSTROKE%; border-radius: %RSMALL%px; background: %FILL%; }
QCheckBox::indicator:hover { background: %FILLHOVER%; }
QCheckBox::indicator:checked, QCheckBox::indicator:indeterminate {
    background: %A%; border-color: %A%;
}
QRadioButton::indicator { border: 1px solid %CTLSTROKE%; border-radius: 10px; background: %FILL%; }
QRadioButton::indicator:hover { background: %FILLHOVER%; }
QRadioButton::indicator:checked { border: 6px solid %A%; background: %FILL%; }

/* ===================== lists / trees / tables ===================== */
QListView, QTreeView, QTableView {
    background: transparent; border: none; outline: none;
    selection-background-color: %SUBHOVER%; selection-color: %TPRIM%;
    %FONT% font-size: 14px;
}
QListView::item, QTreeView::item { min-height: 30px; border-radius: %RSMALL%px; }
QListView::item:hover, QTreeView::item:hover { background: %SUBHOVER%; }
QListView::item:selected, QTreeView::item:selected { background: %SUBPRESS%; color: %TPRIM%; }

QHeaderView::section {
    background: transparent; color: %TSEC%; border: none;
    border-bottom: 1px solid %DIV%; padding: 6px 10px; %FONT%
}

/* ===================== scroll bars ===================== */
QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
    background: %CTLSTROKE%; border-radius: 3px;
}
QScrollBar::handle:vertical { min-height: 28px; width: 6px; margin-left: 3px; }
QScrollBar::handle:horizontal { min-width: 28px; height: 6px; margin-top: 3px; }
QScrollBar::handle:hover { background: %TSEC%; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ===================== menus ===================== */
QMenu {
    background: %CARDSEC%; color: %TPRIM%;
    border: 1px solid %STROKE%; border-radius: %RLARGE%px; padding: 4px;
}
QMenu::item { padding: 7px 26px 7px 14px; border-radius: %RSMALL%px; }
QMenu::item:selected { background: %SUBHOVER%; }
QMenu::item:disabled { color: %TDIS%; }
QMenu::separator { height: 1px; background: %DIV%; margin: 4px 8px; }

/* ===================== tabs & stack ===================== */
QTabWidget::pane { border: none; background: transparent; }
QTabBar::tab {
    background: transparent; color: %TSEC%; border: none;
    padding: 6px 14px; margin-right: 4px; border-radius: %RMED%px;
    %FONT% font-size: 14px;
}
QTabBar::tab:hover { background: %SUBHOVER%; }
QTabBar::tab:selected { background: %ACCTINT%; color: %A%; }

/* ===================== misc ===================== */
/*
   Sizes deliberately do NOT appear here - see the note on applyTypography().
   The min-heights stay so a label keeps a sane floor when it is placed in a row
   that competes for vertical space.
*/
QLabel { background: transparent; color: %TPRIM%; %FONT% font-size: 14px; }
QLabel[fluentRole="caption"] { color: %TSEC%; font-size: 12px; min-height: 18px; }
QLabel[fluentRole="tertiary"] { color: %TTER%; font-size: 12px; min-height: 18px; }
QLabel[fluentRole="title"] { font-size: 28px; font-weight: 600; min-height: 40px; }
QLabel[fluentRole="subtitle"] { font-size: 18px; font-weight: 600; min-height: 28px; }
QLabel[fluentRole="body"] { font-size: 14px; min-height: 20px; }
QLabel[fluentRole="mono"] { %MONO% font-size: 12px; color: %TSEC%; min-height: 18px; }
QLabel[fluentRole="strong"] { font-size: 14px; font-weight: 600; min-height: 20px; }
QLabel[fluentRole="stat"] { font-size: 26px; font-weight: 600; min-height: 34px; }
QLabel[fluentRole="statHint"] { color: %TTER%; font-size: 11px; min-height: 16px; }
QLabel[fluentRole="icon"] { color: %TPRIM%; }
QLabel[fluentRole="iconAccent"] { color: %A%; }
QLabel[fluentRole="iconSecondary"] { color: %TSEC%; }
QLabel[fluentRole="iconTertiary"] { color: %TTER%; }

QFrame[fluentRole="divider"] { background: %DIV%; border: none; max-height: 1px; }
QFrame[fluentRole="vdivider"] { background: %DIV%; border: none; max-width: 1px; }

QProgressBar {
    background: %TRACK%; border: none; border-radius: 2px; height: 4px; text-align: center;
}
QProgressBar::chunk { background: %A%; border-radius: 2px; }

QSplitter::handle { background: transparent; }
QSplitter::handle:hover { background: %DIV%; }

QGroupBox {
    border: 1px solid %STROKESUB%; border-radius: %RLARGE%px;
    margin-top: 10px; padding-top: 10px; %FONT%
}
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; color: %TSEC%; }

QStatusBar { background: %BGALT%; color: %TSEC%; border-top: 1px solid %DIV%; }
)");

    // Colour tokens.
    qss.replace(QStringLiteral("%BG%"), bg);
    qss.replace(QStringLiteral("%BGALT%"), bgAlt);
    qss.replace(QStringLiteral("%CARD%"), cardCol);
    qss.replace(QStringLiteral("%CARDSEC%"), cardSec);
    qss.replace(QStringLiteral("%CARDTER%"), cardTer);
    qss.replace(QStringLiteral("%STROKE%"), strokeCol);
    qss.replace(QStringLiteral("%STROKESUB%"), strokeSub);
    qss.replace(QStringLiteral("%DIV%"), divCol);
    qss.replace(QStringLiteral("%TPRIM%"), tPrim);
    qss.replace(QStringLiteral("%TSEC%"), tSec);
    qss.replace(QStringLiteral("%TTER%"), tTer);
    qss.replace(QStringLiteral("%TDIS%"), tDis);
    qss.replace(QStringLiteral("%FILL%"), fill);
    qss.replace(QStringLiteral("%FILLHOVER%"), fillHover);
    qss.replace(QStringLiteral("%FILLPRESS%"), fillPress);
    qss.replace(QStringLiteral("%CTLSTROKE%"), ctlStroke);
    qss.replace(QStringLiteral("%SUBHOVER%"), subHover);
    qss.replace(QStringLiteral("%SUBPRESS%"), subPress);
    qss.replace(QStringLiteral("%TRACK%"), track);
    qss.replace(QStringLiteral("%A%"), a);
    qss.replace(QStringLiteral("%AHOVER%"), aHover);
    qss.replace(QStringLiteral("%APRESS%"), aPress);
    qss.replace(QStringLiteral("%ONA%"), onA);
    qss.replace(QStringLiteral("%OK%"), ok);
    qss.replace(QStringLiteral("%WARN%"), warn);
    qss.replace(QStringLiteral("%BAD%"), bad);
    qss.replace(QStringLiteral("%BADHOVER%"), hex(darken(critical(), 0.15)));
    qss.replace(QStringLiteral("%ACC%"), acc);
    qss.replace(QStringLiteral("%ACCTINT%"),
                hex(QColor(accent().red(), accent().green(), accent().blue(), isDark() ? 56 : 36)));

    // Shape tokens.
    qss.replace(QStringLiteral("%RSMALL%"), QString::number(RadiusSmall));
    qss.replace(QStringLiteral("%RMED%"), QString::number(RadiusMedium));
    qss.replace(QStringLiteral("%RLARGE%"), QString::number(RadiusLarge));
    qss.replace(QStringLiteral("%RXLARGE%"), QString::number(RadiusXLarge));

    // Font tokens.
    qss.replace(QStringLiteral("%FONT%"), fontRule);
    qss.replace(QStringLiteral("%MONO%"), monoRule);

    // Leftover placeholders would be a bug; drop any that remain.
    static const QRegularExpression leftover(QStringLiteral("%[A-Z]+%"));
    qss.remove(leftover);

    return qss;
}

QString FluentTheme::applicationStyleSheet()
{
    return buildStyleSheet();
}
