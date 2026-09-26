#ifndef TASKDETAILSPANEL_H
#define TASKDETAILSPANEL_H

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QWidget>

class Aria2Manager;
class FluentButton;
class FluentCheckBox;
class FluentIcon;
class FluentProgressBar;
class QGridLayout;
class QLabel;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

namespace Ui {
class TaskDetailsPanel;
}

/**
 * TaskDetailsPanel - the inspector for the currently selected download.
 *
 * Sections: 概要 (transfer numbers), 文件 (per-file progress + selection),
 * 连接 (peers), 服务器 (mirrors/URIs) and 选项 (the raw aria2 option map).
 *
 * Everything is refreshed from Aria2Manager::taskDetail(), which is fetched on
 * demand for Aria2Manager::detailGid() only, so opening the panel does not make
 * the poll loop any heavier.
 */
class TaskDetailsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit TaskDetailsPanel(Aria2Manager *aria2, QWidget *parent = nullptr);
    ~TaskDetailsPanel() override;

    QString gid() const { return m_gid; }
    /// Bind the panel to a task (empty string clears it).
    void setGid(const QString &gid);

signals:
    void closeRequested();
    void toast(const QString &message, bool isError);

public slots:
    void refresh();

protected:
    void changeEvent(QEvent *event) override;

private:
    // The "文件" tab was removed: for an ordinary download it listed a single row
    // (the file itself, already shown in the header) and it cost a whole tab.
    // The Tracker tab only exists for torrent tasks - see refresh().
    enum Section { Overview = 0, Peers, Servers, Tracker, Options, SectionCount };
    /// Section index of the Tracker tab, for the handful of places that care.
    static constexpr int kTrackerSection = Tracker;

    /// One "caption  value" pair in one of the overview grids. Created on first
    /// use and then only ever given new text.
    struct FieldRow {
        QLabel *caption = nullptr;
        class ElidedLabel *value = nullptr;
        const char *captionText = nullptr;
        QString text;               ///< what the value label currently says
        bool wanted = false;        ///< visibility requested by the last update
    };

    /// One row of a list section (a peer, a server…). Rows are pooled per key and
    /// reused across refreshes.
    struct Row {
        QString key;
        QWidget *widget = nullptr;
        class ElidedLabel *primary = nullptr;
        class ElidedLabel *secondary = nullptr;
        QLabel *trailing = nullptr;
        bool seen = false;          ///< touched during the current refresh pass
    };

    void buildHeadline();
    void buildTabs();
    void buildCommands();
    void buildSections();
    /// The cards, grids and empty-state labels of every section, created once.
    ///
    /// This used to be rebuilt on every refresh - and refresh() runs on every poll
    /// (once a second, speeds and progress change constantly), so the whole pane
    /// was torn down and rebuilt under the user's cursor: it flashed, and a
    /// rebuild resets the scroll bar, which made the sections impossible to scroll.
    /// Now only the text inside the rows changes; the widget tree stays put.
    void buildSkeleton();
    void updateOverview();
    void updatePeers();
    void updateServers();
    /// The per-task tracker list (only meaningful for torrent tasks).
    void updateTracker();
    void updateOptions();
    void addTrackerFromInput();
    void removeSelectedTracker();
    void importTrackers();
    /// Parses whatever was read from a file or a URL and adds what survived.
    void applyImportedTrackers(const QByteArray &data, const QString &source);
    void restyle();
    void retranslate();

    /// A Fluent-styled section card with a heading; returns the body layout.
    QVBoxLayout *addCard(QVBoxLayout *into, const char *heading);
    /// Caption+value pair for `id`, created in `grid` on first use.
    QLabel *field(const QString &id, QGridLayout *grid, const char *caption, bool mono = false);
    /// Sets a field's text, skipping work (and the repaint) when it is unchanged.
    void setField(const QString &id, const QString &text);
    void setFieldVisible(const QString &id, bool visible);
    /// The pooled row for `key`, created if this is the first time it is seen.
    Row *rowFor(QList<Row> &rows, const QString &key, QVBoxLayout *into);
    /// Fills one list row; the tint colours the trailing label.
    static void fillRow(Row &row, const QString &primary, const QString &secondary,
                        const QString &trailing, const QColor &tint);
    /// After a pass: rows nothing claimed this time are simply hidden, not deleted.
    static void hideUnseen(QList<Row> &rows);

public:
    /// Bring one of the sections to the front (also what `--detail` uses).
    void setSection(int section);

private:

    Ui::TaskDetailsPanel *ui = nullptr;
    Aria2Manager *m_aria2 = nullptr;
    QString m_gid;
    int m_section = Overview;

    // headline
    FluentIcon *m_headGlyph = nullptr;
    QLabel *m_headName = nullptr;
    QLabel *m_headMeta = nullptr;
    FluentProgressBar *m_headProgress = nullptr;

    // tabs + command bar
    QList<FluentButton *> m_tabs;
    FluentButton *m_closeButton = nullptr;
    FluentButton *m_openButton = nullptr;
    FluentButton *m_folderButton = nullptr;
    FluentButton *m_copyButton = nullptr;
    FluentButton *m_pauseButton = nullptr;
    FluentButton *m_retryButton = nullptr;
    FluentButton *m_removeButton = nullptr;

    // section bodies (built once, never rebuilt)
    QList<QWidget *> m_sectionContent;
    /// The scroll area each section body lives in, so a rebuild can put the
    /// scroll position back where the user left it.
    QList<QScrollArea *> m_sectionScrolls;
    QList<QLabel *> m_captionLabels;
    QList<const char *> m_captions;

    // ---- section skeleton (created once) --------------------------------------
    // Overview
    QWidget *m_basicCard = nullptr;
    QGridLayout *m_basicGrid = nullptr;
    QWidget *m_transferCard = nullptr;
    QGridLayout *m_transferGrid = nullptr;
    QWidget *m_errorCard = nullptr;
    QLabel *m_errorLabel = nullptr;
    QLabel *m_overviewEmpty = nullptr;
    QHash<QString, FieldRow> m_fields;
    QHash<QString, int> m_fieldRows;

    // Peers
    QVBoxLayout *m_peersBody = nullptr;
    QList<Row> m_peerRows;
    QWidget *m_peersCard = nullptr;
    QLabel *m_peersEmpty = nullptr;

    // Servers
    QVBoxLayout *m_uriBody = nullptr;
    QList<Row> m_uriRows;
    QWidget *m_uriCard = nullptr;
    QVBoxLayout *m_serverBody = nullptr;
    QList<Row> m_serverRows;
    QWidget *m_serverCard = nullptr;
    QLabel *m_serversEmpty = nullptr;

    // Tracker (torrent tasks only)
    QWidget *m_trackerCard = nullptr;
    class QPlainTextEdit *m_trackerList = nullptr;
    class FluentLineEdit *m_trackerEdit = nullptr;
    QLabel *m_trackerEmpty = nullptr;
    QLabel *m_trackerHint = nullptr;
    FluentButton *m_trackerAdd = nullptr;
    FluentButton *m_trackerRemove = nullptr;
    FluentButton *m_trackerImport = nullptr;
    QString m_trackerText;      ///< what the list currently shows

    // Options
    QGridLayout *m_optionsGrid = nullptr;
    QHash<QString, FieldRow> m_optionFields;
    /// The option names currently laid out, in order: the grid is only rebuilt
    /// when this changes, so a refresh does not re-lay-out the whole list.
    QStringList m_optionOrder;
    QWidget *m_optionsCard = nullptr;
    QLabel *m_optionsEmpty = nullptr;
};

#endif // TASKDETAILSPANEL_H
