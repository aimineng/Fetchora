#ifndef FLUENTINPUTS_H
#define FLUENTINPUTS_H

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QAbstractButton>
#include <QVariant>

#include "ui/FluentTheme.h"

/**
 * FluentLineEdit - WinUI 3 text field.
 *
 * The accent underline that thickens on focus cannot be expressed as a style
 * sheet rule that reacts to focus on a single border edge, so it is painted
 * here; colours come from FluentTheme.
 */
class FluentLineEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit FluentLineEdit(QWidget *parent = nullptr);

    void setHeader(const QString &header);
    QString header() const { return m_header; }
    void setDescription(const QString &description);
    void setFieldGlyph(const QChar &glyph);
    void setMonospace(bool monospace);
    /// Show a reveal (password) toggle on the trailing edge.
    void setRevealButton(bool reveal);
    /// Extra height so the header/description rows fit.
    QSize sizeHint() const override;
    /// A floor that includes the painted header/description, so a tight layout
    /// can never squeeze them on top of the field.
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    /// Pushes the field down by the space the painted header/description needs.
    void updatePadding();
    QRect fieldRect() const;
    QRect glyphRect() const;
    QRect revealRect() const;

    QString m_header;
    QString m_description;
    QChar m_glyph;
    bool m_monospace = false;
    bool m_reveal = false;
    bool m_hovered = false;
    bool m_revealHovered = false;
};

/**
 * FluentSpinBox - numeric input with the Fluent inline stepper.
 * The stepper arrows are painted because Qt's default sub-control arrows
 * cannot be sized or coloured consistently across styles.
 */
class FluentSpinBox : public QSpinBox
{
    Q_OBJECT
public:
    explicit FluentSpinBox(QWidget *parent = nullptr);

    void setHeader(const QString &header);
    void setDescription(const QString &description);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum Zone { None, Up, Down };
    void updatePadding();
    QRect fieldRect() const;
    QRect upRect() const;
    QRect downRect() const;
    Zone zoneAt(const QPoint &pos) const;

    QString m_header;
    QString m_description;
    Zone m_pressed = None;
    Zone m_hovered = None;
};

/**
 * FluentSwitch - WinUI 3 toggle switch with an animated knob.
 * Painted rather than styled: the Fluent switch is not a plain box, and the
 * knob animation is what makes it read as a switch.
 */
class FluentSwitch : public QAbstractButton
{
    Q_OBJECT
    /// Animated knob position: 0 = off, 1 = on. Exposed as a property so it can
    /// be driven by QPropertyAnimation.
    Q_PROPERTY(qreal position READ position WRITE setPosition)
public:
    explicit FluentSwitch(QWidget *parent = nullptr);

    void setHeader(const QString &header);
    void setDescription(const QString &description);

    qreal position() const { return m_position; }
    void setPosition(qreal position);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    qreal m_position = 0.0;
};

/**
 * FluentCheckBox - WinUI 3 checkbox.
 * The check glyph is drawn from the icon font, which a style sheet cannot do.
 */
class FluentCheckBox : public QCheckBox
{
    Q_OBJECT
public:
    explicit FluentCheckBox(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool m_hovered = false;
};

/**
 * FluentRadioButton - WinUI 3 radio button with an animated inner dot.
 */
class FluentRadioButton : public QRadioButton
{
    Q_OBJECT
public:
    explicit FluentRadioButton(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool m_hovered = false;
};

/**
 * FluentSlider - WinUI 3 slider: a thin track, an inner fill and a round thumb
 * that shrinks on press instead of growing.
 */
class FluentSlider : public QSlider
{
    Q_OBJECT
public:
    explicit FluentSlider(QWidget *parent = nullptr);
    /// Draw the current value next to the track.
    void setShowValue(bool show);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool m_hovered = false;
    bool m_showValue = false;
};

/**
 * FluentComboBox - WinUI 3 drop-down.
 * The popup is a styled QListView so the selection pill and the rounded flyout
 * match the rest of the UI.
 */
class FluentComboBox : public QComboBox
{
    Q_OBJECT
public:
    explicit FluentComboBox(QWidget *parent = nullptr);

    void setHeader(const QString &header);
    void setCompact(bool compact);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showPopup() override;

private:
    void updatePadding();
    QString m_header;
    bool m_compact = false;
    bool m_hovered = false;
};

#endif // FLUENTINPUTS_H
