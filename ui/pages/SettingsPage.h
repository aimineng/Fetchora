#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QChar>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <QWidget>

#include "ui/FluentButton.h"

class Aria2Manager;
class FluentComboBox;
class FluentLineEdit;
class FluentSpinBox;
class FluentSwitch;
class InfoBar;
class QHBoxLayout;
class QLabel;
class QPlainTextEdit;
class QVBoxLayout;
class SettingsManager;

namespace Ui {
class SettingsPage;
}

/**
 * SettingsPage - the settings screen, in the standard WinUI shape: a section
 * rail on the left and one scrollable page per section on the right.
 *
 * Structure lives in SettingsPage.ui (the header, the rail host with its search
 * slot, the QStackedWidget host); this class fills them in. Every editable row
 * is created by one of the add*() builders and recorded in m_fields together
 * with the QT_TR_NOOP sources of its captions, which is what makes the search
 * filter and retranslate() possible without a per-field code path.
 */
class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(SettingsManager *settings, Aria2Manager *aria2, QWidget *parent = nullptr);
    ~SettingsPage() override;

signals:
    void toast(const QString &message, bool isError);

protected:
    void changeEvent(QEvent *event) override;

private:
    /// One editable row of a section page.
    struct Field {
        enum Kind { Toggle, Text, Number, Choice };

        Kind kind = Toggle;
        QWidget *widget = nullptr;  ///< the control itself
        QWidget *row = nullptr;     ///< what the search filter shows or hides
        QWidget *group = nullptr;   ///< the card the row lives in
        QLabel *titleLabel = nullptr;  ///< Toggle rows draw their own caption
        QLabel *descLabel = nullptr;
        QString key;                   ///< SettingsManager / QSettings key
        const char *title = nullptr;        ///< QT_TR_NOOP source
        const char *description = nullptr;  ///< QT_TR_NOOP source
        QVector<const char *> labels;       ///< Choice: QT_TR_NOOP captions
        QStringList values;                 ///< Choice: stored values
        /// Choice entries that come from the machine rather than from `values`
        /// (the installed font families): retranslate() must leave the list
        /// alone and only refresh the header.
        bool keepItems = false;
        int section = 0;
    };

    /// One rail entry plus the page it shows.
    struct Section {
        const char *title = nullptr;
        const char *subtitle = nullptr;
        QChar glyph;
        QString rowName;  ///< object name of the tinted rail row
        QWidget *railRow = nullptr;
        FluentButton *railButton = nullptr;
        QLabel *titleLabel = nullptr;
        QLabel *subtitleLabel = nullptr;
    };

    /// A plain caption that retranslate() has to re-apply. `arg` is non-empty
    /// for captions that carry a runtime value, e.g. the version number.
    struct Caption {
        QLabel *label = nullptr;
        const char *source = nullptr;
        QString arg;
    };

    /// An inline row: the column a field goes into plus the slot next to it.
    struct FieldSlot {
        QVBoxLayout *field = nullptr;
        QHBoxLayout *trailing = nullptr;
    };

    // ------------------------------------------------------------ structure
    void buildHeader();
    void buildSections();
    QVBoxLayout *addSectionPage(const char *title, const char *subtitle, const QChar &glyph);
    QVBoxLayout *addGroupCard(QVBoxLayout *page, const char *title);
    QLabel *addCaption(QVBoxLayout *into, const char *source, const char *role = "body",
                       const QString &arg = QString(), bool wrap = false);
    FieldSlot addInlineRow(QVBoxLayout *into);
    QHBoxLayout *addButtonStrip(QVBoxLayout *into, bool rightAligned);
    FluentButton *addAction(QHBoxLayout *strip, const char *text, FluentButton::Role role,
                            const QChar &glyph = QChar());
    FluentButton *addTrailingAction(QHBoxLayout *row, const char *text,
                                    FluentButton::Role role = FluentButton::Standard,
                                    const QChar &glyph = QChar());
    QPlainTextEdit *addConsole(QVBoxLayout *page, const char *title, const char *description);

    // -------------------------------------------------- generic field builders
    FluentSwitch *addSwitch(QVBoxLayout *into, const QString &key, const char *title,
                            const char *description);
    FluentLineEdit *addLineEdit(QVBoxLayout *into, const QString &key, const char *title,
                                const char *description, bool monospace = false);
    FluentSpinBox *addSpinBox(QVBoxLayout *into, const QString &key, const char *title,
                              const char *description, int lo, int hi,
                              const QString &suffix = QString());
    FluentComboBox *addCombo(QVBoxLayout *into, const QString &key, const char *title,
                             const char *const *labels, const QStringList &values);
    void rebuildCombo(FluentComboBox *combo, const Field &field);
    QWidget *groupOf(const QVBoxLayout *into) const;

    // ------------------------------------------------------------- behaviour
    QVariant readValue(const QString &key, const QVariant &fallback = QVariant()) const;
    void writeValue(const QString &key, const QVariant &value);
    void storeValue(const QString &key, const QVariant &value, int section = -1);
    void refreshFields();
    void refreshLogs();
    void refreshBridgeInfo();
    void refreshRestartBar();
    void refreshSubtitle();
    void applyFilter(const QString &needle);
    void setSection(int index);
    void markRestartNeeded();
    void restartEngine();
    void syncConsole(QPlainTextEdit *view, const QString &text);
    void saveConsole(QPlainTextEdit *view);
    bool confirm(const QString &title, const QString &message);
    void retranslate();
    void restyle();

    Ui::SettingsPage *ui = nullptr;
    SettingsManager *m_settings = nullptr;
    Aria2Manager *m_aria2 = nullptr;

    QVector<Field> m_fields;
    QVector<Section> m_sections;
    QVector<QWidget *> m_groups;
    QVector<QPair<FluentButton *, const char *>> m_actionLabels;
    QVector<Caption> m_captions;

    FluentLineEdit *m_search = nullptr;
    FluentButton *m_clearSearch = nullptr;
    FluentButton *m_restartButton = nullptr;
    InfoBar *m_restartBar = nullptr;
    InfoBar *m_bridgeBar = nullptr;
    QPlainTextEdit *m_engineConsole = nullptr;
    QPlainTextEdit *m_rpcConsole = nullptr;

    QString m_filter;
    /// Set while refreshFields() writes values back into the widgets, so the
    /// programmatic update does not look like a user edit.
    bool m_refreshing = false;
    int m_buildSection = 0;
};

#endif // SETTINGSPAGE_H
