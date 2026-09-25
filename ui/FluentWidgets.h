#ifndef FLUENTWIDGETS_H
#define FLUENTWIDGETS_H

#include <QFrame>
#include <QLabel>
#include <QWidget>

#include "ui/FluentTheme.h"

class QVBoxLayout;
class QHBoxLayout;

/**
 * FluentIcon - a single Segoe Fluent Icons glyph.
 *
 * A QLabel with the icon font. The family is resolved once by FluentTheme so
 * the "one family name only" rule is enforced in exactly one place.
 */
class FluentIcon : public QLabel
{
    Q_OBJECT
public:
    explicit FluentIcon(QWidget *parent = nullptr);
    FluentIcon(const QChar &glyph, int size = 16, QWidget *parent = nullptr);

    void setGlyph(const QChar &glyph);
    QChar glyph() const { return m_glyph; }
    void setIconSize(int pixelSize);
    void setIconColor(const QColor &color);
    /// Convenience presets that follow the theme.
    void usePrimaryColor();
    void useSecondaryColor();
    void useTertiaryColor();
    void useAccentColor();

protected:
    void changeEvent(QEvent *event) override;

private:
    void apply();

    QChar m_glyph;
    int m_size = 16;
    QColor m_color;
    bool m_followTheme = true;
    /// Re-entrancy guard: apply() assigns a style sheet, which re-enters
    /// changeEvent() -> apply().
    bool m_applying = false;
};

/**
 * FluentCard - a layered Fluent surface with a 1px lift stroke.
 *
 * variant: Layer | Card | Secondary | Accent | Outline
 */
class FluentCard : public QFrame
{
    Q_OBJECT
public:
    enum Variant { Layer, Card, Secondary, Accent, Outline };

    explicit FluentCard(QWidget *parent = nullptr);
    void setVariant(Variant variant);
    Variant variant() const { return m_variant; }
    void setInteractive(bool interactive);
    void setSelected(bool selected);
    void setAccentTint(const QColor &color);
    /// Exposed so pages can put their generated layout inside the card.
    QVBoxLayout *body();

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    Variant m_variant = Card;
    bool m_interactive = false;
    bool m_selected = false;
    bool m_hovered = false;
    QColor m_tint;
    QVBoxLayout *m_body = nullptr;
};

/**
 * InfoBar - inline message strip (WinUI InfoBar).
 * severity: "info" | "success" | "warning" | "error"
 */
class InfoBar : public QFrame
{
    Q_OBJECT
public:
    enum Severity { Info, Success, Warning, Error };

    explicit InfoBar(QWidget *parent = nullptr);

    void setSeverity(Severity severity);
    void setTitle(const QString &title);
    void setMessage(const QString &message);
    void setActionText(const QString &text);
    void setClosable(bool closable);

signals:
    void actionTriggered();
    void closed();

private:
    void restyle();

    Severity m_severity = Info;
    FluentIcon *m_glyph = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_message = nullptr;
    class FluentButton *m_action = nullptr;
    class FluentButton *m_close = nullptr;
};

/**
 * StatCard - compact metric tile used on the dashboard header.
 * Deliberately static (no pulsing): see the QML history in the README.
 */
class StatCard : public QFrame
{
    Q_OBJECT
public:
    explicit StatCard(QWidget *parent = nullptr);

    void setGlyph(const QChar &glyph);
    void setLabel(const QString &label);
    void setValue(const QString &value);
    void setSecondary(const QString &secondary);
    void setTint(const QColor &tint);
    /// 0..100, or negative to hide the bar.
    void setProgress(double progress);

private:
    void restyle();

    FluentIcon *m_glyph = nullptr;
    QLabel *m_value = nullptr;
    QLabel *m_label = nullptr;
    QLabel *m_secondary = nullptr;
    class FluentProgressBar *m_bar = nullptr;
    QColor m_tint;
    double m_progress = -1;
};

/**
 * FluentProgressBar - WinUI progress bar with optional segmented track
 * (used for multi-file torrents) and an indeterminate mode.
 */
class FluentProgressBar : public QWidget
{
    Q_OBJECT
public:
    explicit FluentProgressBar(QWidget *parent = nullptr);

    void setValue(double value);          ///< 0..100
    double value() const { return m_value; }
    void setIndeterminate(bool indeterminate);
    void setBarColor(const QColor &color);
    void setShowSegments(bool show);
    void setBarHeight(int height);

protected:
    void paintEvent(QPaintEvent *event) override;
    void timerEvent(QTimerEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void updateTimer();

    double m_value = 0;
    bool m_indeterminate = false;
    bool m_segments = false;
    int m_barHeight = 4;
    int m_phase = 0;
    int m_timerId = 0;
    QColor m_color;
};

/**
 * ToastHost - transient notification stack, anchored to the bottom right of
 * its parent. Push with show(); toasts expire on their own.
 */
class ToastHost : public QWidget
{
    Q_OBJECT
public:
    explicit ToastHost(QWidget *parent = nullptr);

    enum Severity { Info, Success, Warning, Error };
    /// Named push() (not show()) so QWidget::show() stays reachable.
    void push(const QString &text, Severity severity = Info, int durationMs = 3600);

protected:
    /// Follows the parent's geometry so the stack stays bottom-right anchored.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void relayout();
    void removeToast(QWidget *toast);

    QVBoxLayout *m_stack = nullptr;
    int m_maxVisible = 4;
};

#endif // FLUENTWIDGETS_H
