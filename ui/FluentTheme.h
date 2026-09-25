#ifndef FLUENTTHEME_H
#define FLUENTTHEME_H

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>
#include <QStringList>

class SettingsManager;

/**
 * FluentTheme - the single source of design tokens and generated stylesheets.
 *
 * This is the C++/QWidget counterpart of the old QML Theme singleton. It
 * follows the WinUI 3 / Fluent 2 language: layered surfaces, 1px strokes that
 * read as a lift rather than a hard border, 4/8px corner radii, a Segoe UI
 * Variable type ramp and an accent colour taken from Windows.
 *
 * Structure (layouts) lives in the .ui files; everything visual is produced
 * here as a Qt style sheet, because colours, radii and states have to change at
 * runtime when the user switches theme.
 *
 * Usage:
 *     FluentTheme *t = FluentTheme::instance();
 *     t->attach(settingsManager);        // drives theme switching
 *     widget->setStyleSheet(t->applicationStyleSheet());
 *     connect(t, &FluentTheme::changed, this, &MyWidget::restyle);
 */
class FluentTheme : public QObject
{
    Q_OBJECT

public:
    /// WinUI 3 corner radii.
    enum Radius { RadiusSmall = 4, RadiusMedium = 6, RadiusLarge = 8, RadiusXLarge = 12 };

    static FluentTheme *instance();

    // ------------------------------------------------------------------ mode
    /// "dark" | "light" | "system"
    QString mode() const;
    bool isDark() const;
    void setMode(const QString &mode);

    /// Re-read the accent colour from Windows and rebuild the stylesheet.
    void refresh();
    /// Start following a SettingsManager (theme, accent, fonts, animations).
    void attach(SettingsManager *settings);

    // --------------------------------------------------------------- palette
    QColor background() const;
    QColor backgroundAlt() const;
    QColor layer() const;
    QColor card() const;
    QColor cardSecondary() const;
    QColor cardTertiary() const;
    QColor stroke() const;
    QColor strokeSubtle() const;
    QColor divider() const;
    QColor textPrimary() const;
    QColor textSecondary() const;
    QColor textTertiary() const;
    QColor textDisabled() const;
    QColor controlFill() const;
    QColor controlFillHover() const;
    QColor controlFillPressed() const;
    QColor controlStroke() const;
    QColor subtleHover() const;
    QColor subtlePressed() const;
    QColor trackFill() const;

    QColor accent() const;
    QColor accentLight1() const;
    QColor accentDark1() const;
    QColor accentDark2() const;
    QColor onAccent() const;

    QColor success() const;
    QColor caution() const;
    QColor critical() const;
    QColor info() const;
    QColor successBg() const;
    QColor cautionBg() const;
    QColor criticalBg() const;
    QColor infoBg() const;

    /// Translucent page wash used on top of a Mica backdrop.
    QColor acrylicTint() const;

    // ------------------------------------------------------------ typography
    /// The icon family actually present on this machine ("" if none).
    static QString iconFont();
    /// Monospace family for paths, hashes and the option editor.
    static QString monoFont();
    static QFont monoFont(int pixelSize);
    /// Resolved UI family ("" = the platform's own UI font).
    static QString uiFontFamily();
    /// Override the UI family; an empty string restores the platform default.
    static void setUiFontFamily(const QString &family);
    /// Base UI size in px (default 14).
    static int uiFontSize();
    static void setUiFontSize(int pixelSize);
    static QFont uiFont(int pixelSize, QFont::Weight weight = QFont::Normal);
    static QFont iconFont(int pixelSize);

    /*
        NOTE on where the type ramp lives.

        It is declared in the generated style sheet, not applied with setFont().

        Qt rebuilds a style-sheet rule's font from QApplication::font() plus
        whatever font properties the rule names, and re-applies it on every
        polish - so a `QLabel { font-size: 14px }` rule silently overwrote every
        setFont() call in the code base, and a rule naming only some properties
        (say font-weight) reset the rest to the application default. The sheet is
        therefore the single source of truth, each rule states every property it
        needs, and ensureFontsResolved() pins QApplication::font() to the family
        and base size the sheet is built against.
    */

    /// Points the application QPalette at the active theme.
    static void applyApplicationPalette();
    /// Resolves the UI/mono families and installs the base application font.
    static void ensureFontsResolved();

    // ----------------------------------------------------------------- shape
    static int spacingXS() { return 4; }
    static int spacingS() { return 8; }
    static int spacingM() { return 12; }
    static int spacingL() { return 16; }
    static int spacingXL() { return 24; }
    static int captionHeight() { return 48; }
    static int navWidth() { return 236; }
    static int controlHeight() { return 32; }

    // ---------------------------------------------------------------- glyphs
    // Segoe Fluent Icons code points. Every one of these has been verified to
    // exist in the shipped font - see the note in iconFont().
    struct Glyph {
        static const QChar Download;      static const QChar Magnet;
        static const QChar History;       static const QChar Settings;
        static const QChar Add;           static const QChar AddFile;
        static const QChar Play;          static const QChar Pause;
        static const QChar Stop;          static const QChar Delete;
        static const QChar Folder;        static const QChar OpenFile;
        static const QChar Copy;          static const QChar Link;
        static const QChar Refresh;       static const QChar Search;
        static const QChar ChevronRight;  static const QChar ChevronDown;
        static const QChar ChevronUp;     static const QChar Up;
        static const QChar Down;          static const QChar Info;
        static const QChar Warning;       static const QChar Error;
        static const QChar Success;       static const QChar Speed;
        static const QChar Globe;         static const QChar Shield;
        static const QChar Disk;          static const QChar Clock;
        static const QChar Peer;          static const QChar Server;
        static const QChar File;          static const QChar Filter;
        static const QChar More;          static const QChar Close;
        static const QChar Minimize;      static const QChar Maximize;
        static const QChar Restore;       static const QChar Back;
        static const QChar Save;          static const QChar Edit;
        static const QChar Palette;       static const QChar Rocket;
        static const QChar Console;       static const QChar Check;
        static const QChar Power;         static const QChar Torrent;
        static const QChar Upload;        static const QChar Lightbulb;
        static const QChar Home;          static const QChar Tune;
        static const QChar Media;         static const QChar Music;
        static const QChar Archive;       static const QChar App;
        static const QChar Pdf;           static const QChar Image;
        static const QChar Reveal;        static const QChar Hide;
        static const QChar Indeterminate;
    };

    /// Small badge glyph chosen from a file extension.
    static QChar fileGlyph(const QString &fileName);

    // ------------------------------------------------------------- helpers
    static QString formatSize(double bytes, int precision = -1);
    static QString formatSpeed(double bytesPerSecond);
    static QString formatDuration(double seconds);
    static QString formatEta(double remainingBytes, double speed);
    static QString statusLabel(const QString &status);
    QColor statusColor(const QString &status) const;
    static QChar statusGlyph(const QString &status);
    /// Shorten a raw hex info hash for display.
    static QString prettyInfoHash(const QString &hash);

    // ---------------------------------------------------------- stylesheets
    /// The whole-application sheet (widgets, scroll bars, dialogs, menus).
    QString applicationStyleSheet();
    /// Extra rules for one object name, e.g. styleSheetFor("accentButton").
    QString styleSheetFor(const QString &className);

signals:
    /// Anything visual changed; widgets should re-apply their stylesheet.
    void changed();

private:
    explicit FluentTheme(QObject *parent = nullptr);

    void readSystemAccent();
    void readSystemDarkMode();
    QString buildStyleSheet() const;

    static QString resolveFamily(const QStringList &candidates, const QString &fallback);

public:
    /// Picks the first installed family from `candidates`, or `fallback`.
    static QString pickFamily(const QStringList &candidates, const QString &fallback)
    {
        return resolveFamily(candidates, fallback);
    }

private:

    QString m_mode = QStringLiteral("dark");
    bool m_systemDark = true;
    QColor m_systemAccent;

    friend class FluentThemePrivate;
};

#endif // FLUENTTHEME_H
