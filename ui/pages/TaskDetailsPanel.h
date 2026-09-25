#ifndef TASKDETAILSPANEL_H
#define TASKDETAILSPANEL_H

#include <QList>
#include <QPair>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QWidget>

class Aria2Manager;
class FluentButton;
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
    enum Section { Overview = 0, Files, Peers, Servers, Options, SectionCount };

    void buildHeadline();
    void buildTabs();
    void buildCommands();
    void buildSections();
    void buildOverview(QWidget *content);
    void buildFilesSection(QWidget *content);
    void buildPeersSection(QWidget *content);
    void buildServersSection(QWidget *content);
    void buildOptionsSection(QWidget *content);
    void restyle();
    void retranslate();
    /// Clear every dynamically created child of a section container.
    void clearLayout(QLayout *layout);

    /// Adds a "caption  value" row and remembers the caption for retranslation.
    QLabel *addField(QGridLayout *grid, int row, const char *caption, bool mono = false);
    /// A Fluent-styled section card with a heading; returns the body layout.
    QVBoxLayout *addCard(QVBoxLayout *into, const char *heading);
    /// One list row (used by files / peers / servers / options).
    QWidget *addListRow(QVBoxLayout *into, const QString &primary, const QString &secondary,
                        const QString &trailing, const QColor &tint, QWidget *leading = nullptr);
    void setSection(int section);

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

    // section bodies (rebuilt on every refresh)
    QList<QWidget *> m_sectionContent;
    QList<QLabel *> m_captionLabels;
    QList<const char *> m_captions;

    // file selection state, kept between refreshes
    QStringList m_selectedFiles;
    bool m_filesLoaded = false;
};

#endif // TASKDETAILSPANEL_H
