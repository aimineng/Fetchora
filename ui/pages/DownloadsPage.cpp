#include "ui/pages/DownloadsPage.h"

#include "Aria2Manager.h"
#include "ui/FluentButton.h"
#include "ui/FluentInputs.h"
#include "ui/FluentTheme.h"
#include "ui/FluentWidgets.h"
#include "ui/pages/ui_DownloadsPage.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace {

/// The filter chips are plain buttons carrying a dynamic property so the
/// generated style sheet can colour the selected one.
const char *kChipProperty = "fluentChip";

QString chipStyle(const FluentTheme *t, bool selected)
{
    if (selected) {
        return QStringLiteral(
                   "QPushButton { background: %1; color: %2; border: 1px solid %3;"
                   "              border-radius: 14px; padding: 0 14px; min-height: 28px; }")
            .arg(QColor(t->accent().red(), t->accent().green(), t->accent().blue(),
                        t->isDark() ? 61 : 36)
                     .name(QColor::HexArgb),
                 t->accent().name(),
                 QColor(t->accent().red(), t->accent().green(), t->accent().blue(), 128)
                     .name(QColor::HexArgb));
    }
    return QStringLiteral(
               "QPushButton { background: transparent; color: %1; border: 1px solid %2;"
               "              border-radius: 14px; padding: 0 14px; min-height: 28px; }"
               "QPushButton:hover { background: %3; }")
        .arg(t->textSecondary().name(), t->strokeSubtle().name(), t->subtleHover().name());
}

} // namespace

DownloadsPage::DownloadsPage(Aria2Manager *aria2, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DownloadsPage)
    , m_aria2(aria2)
    , m_emptyTitle(tr("还没有下载任务"))
    , m_emptyHint(tr("点击“新建”或直接把链接粘贴进来"))
{
    ui->setupUi(this);

    ui->pageTitle->setProperty("fluentRole", "title");
    ui->pageSubtitle->setProperty("fluentRole", "caption");
    ui->filterChips->setStyleSheet(QStringLiteral("background: transparent;"));

    buildStatCards();

    m_banner = new InfoBar(this);
    m_banner->setSeverity(InfoBar::Warning);
    m_banner->setActionText(tr("重启引擎"));
    connect(m_banner, &InfoBar::actionTriggered, this, [this]() {
        if (m_aria2)
            m_aria2->restartEngine();
    });
    // Between the header and the statistics strip.
    qobject_cast<QVBoxLayout *>(layout())->insertWidget(1, m_banner);

    buildCommandBar();
    buildFilterChips();

    m_list = new FluentTaskList(this);
    m_list->setEmptyStateText(m_emptyTitle, m_emptyHint);
    ui->listHostLayout->addWidget(m_list);

    connect(m_list, &FluentTaskList::selectionChanged, this, &DownloadsPage::selectionChanged);
    connect(m_list, &FluentTaskList::pauseRequested, this, [this](const QString &gid) {
        m_aria2->pauseTask(gid);
    });
    connect(m_list, &FluentTaskList::resumeRequested, this, [this](const QString &gid) {
        const QVariantList tasks = m_aria2->tasks();
        for (const QVariant &v : tasks) {
            const QVariantMap t = v.toMap();
            if (t.value(QStringLiteral("gid")).toString() != gid)
                continue;
            if (t.value(QStringLiteral("status")).toString() == QLatin1String("error"))
                m_aria2->retryTask(gid);
            else
                m_aria2->resumeTask(gid);
            break;
        }
    });
    connect(m_list, &FluentTaskList::openRequested, this, [this](const QString &gid) {
        m_aria2->openFile(gid);
    });
    connect(m_list, &FluentTaskList::folderRequested, this, [this](const QString &gid) {
        m_aria2->openFolder(gid);
    });
    connect(m_list, &FluentTaskList::removeRequested, this, [this](const QString &gid) {
        m_aria2->removeTask(gid, 0);
    });
    connect(m_list, &FluentTaskList::copyLinkRequested, this, [this](const QString &gid) {
        const QVariantList tasks = m_aria2->tasks();
        for (const QVariant &v : tasks) {
            const QVariantMap t = v.toMap();
            if (t.value(QStringLiteral("gid")).toString() == gid) {
                m_aria2->copyToClipboard(t.value(QStringLiteral("uri")).toString());
                emit toast(tr("已复制下载链接"), false);
                return;
            }
        }
    });

    wireManager();
    restyle();
    refresh();
}

DownloadsPage::~DownloadsPage()
{
    delete ui;
}

void DownloadsPage::buildStatCards()
{
    struct Spec {
        QChar glyph;
        QString label;
        int tintSlot;   // 0 accent, 1 success, 2 info, 3 caution
    };
    const QList<Spec> specs = {
        {FluentTheme::Glyph::Speed, tr("总下载速度"), 0},
        {FluentTheme::Glyph::Upload, tr("总上传速度"), 1},
        {FluentTheme::Glyph::Download, tr("活动 / 队列"), 2},
        {FluentTheme::Glyph::Disk, tr("累计下载"), 3},
    };
    for (const Spec &s : specs) {
        auto *card = new StatCard(this);
        card->setGlyph(s.glyph);
        card->setLabel(s.label);
        card->setValue(QStringLiteral("--"));
        card->setProperty("tintSlot", s.tintSlot);
        m_cards << card;
        ui->statLayout->addWidget(card, 1);
    }
}

void DownloadsPage::buildCommandBar()
{
    auto add = [this](const QChar &glyph, const QString &text, bool accent) {
        auto *b = new FluentButton(this);
        if (!glyph.isNull())
            b->setGlyph(glyph);
        if (!text.isEmpty())
            b->setText(text);
        else
            b->setIconOnly(true);
        b->setRole(accent ? FluentButton::Accent : FluentButton::Standard);
        ui->commandLayout->addWidget(b);
        return b;
    };

    m_newButton = add(FluentTheme::Glyph::Add, tr("新建"), true);
    m_torrentButton = add(FluentTheme::Glyph::AddFile, tr("种子"), false);
    m_refreshButton = add(FluentTheme::Glyph::Refresh, QString(), false);
    m_refreshButton->setRole(FluentButton::Subtle);
    m_clearButton = add(FluentTheme::Glyph::Delete, QString(), false);
    m_clearButton->setRole(FluentButton::Subtle);
    m_detailsButton = add(FluentTheme::Glyph::Tune, QString(), false);
    m_detailsButton->setRole(FluentButton::Subtle);

    // Pause/resume-all share one slot: only one is visible at a time.
    m_pauseAllButton = new FluentButton(this);
    m_pauseAllButton->setGlyph(FluentTheme::Glyph::Pause);
    m_pauseAllButton->setText(tr("全部暂停"));
    m_pauseAllButton->setVisible(false);
    ui->commandLayout->insertWidget(2, m_pauseAllButton);

    m_resumeAllButton = new FluentButton(this);
    m_resumeAllButton->setGlyph(FluentTheme::Glyph::Play);
    m_resumeAllButton->setText(tr("全部开始"));
    m_resumeAllButton->setVisible(false);
    ui->commandLayout->insertWidget(3, m_resumeAllButton);

    m_newButton->setTooltipText(tr("新建下载 (Ctrl+N)"));
    m_torrentButton->setTooltipText(tr("打开 .torrent / .metalink 文件"));
    m_refreshButton->setTooltipText(tr("刷新 (F5)"));
    m_clearButton->setTooltipText(tr("清除已完成记录"));
    m_detailsButton->setTooltipText(tr("显示/隐藏详情面板"));

    connect(m_newButton, &QPushButton::clicked, this, &DownloadsPage::newTaskRequested);
    connect(m_torrentButton, &QPushButton::clicked, this, &DownloadsPage::torrentPickerRequested);
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() {
        if (m_aria2)
            m_aria2->refreshNow();
    });
    connect(m_clearButton, &QPushButton::clicked, this, [this]() {
        if (m_aria2)
            m_aria2->purgeResults();
    });
    connect(m_detailsButton, &QPushButton::clicked, this, &DownloadsPage::detailsToggleRequested);
    connect(m_pauseAllButton, &QPushButton::clicked, this, [this]() {
        if (m_aria2)
            m_aria2->pauseAll();
    });
    connect(m_resumeAllButton, &QPushButton::clicked, this, [this]() {
        if (m_aria2)
            m_aria2->resumeAll();
    });
}

void DownloadsPage::buildFilterChips()
{
    struct Chip { QString key; QString label; };
    const QList<Chip> chips = {
        {QStringLiteral("all"), tr("全部")},
        {QStringLiteral("active"), tr("下载中")},
        {QStringLiteral("waiting"), tr("队列")},
        {QStringLiteral("complete"), tr("已完成")},
        {QStringLiteral("error"), tr("失败")},
        {QStringLiteral("bt"), tr("BT")},
    };
    for (const Chip &c : chips) {
        auto *chip = new FluentButton(this);
        chip->setText(c.label);
        chip->setProperty("chipKey", c.key);
        chip->setFixedHeight(28);
        ui->filterChipLayout->addWidget(chip);
        m_chips << chip;
        connect(chip, &QPushButton::clicked, this, [this, key = c.key]() { setStatusFilter(key); });
    }

    // Sort selector.
    m_sortCombo = new QComboBox(this);
    m_sortCombo->addItems({tr("默认顺序"), tr("最快优先"), tr("体积优先")});
    m_sortCombo->setFixedHeight(28);
    ui->filterLayout->addWidget(m_sortCombo, 0, Qt::AlignRight);
    ui->sortCombo->hide();
}

void DownloadsPage::wireManager()
{
    if (!m_aria2)
        return;
    connect(m_aria2, &Aria2Manager::tasksChanged, this, &DownloadsPage::refresh);
    connect(m_aria2, &Aria2Manager::viewChanged, this, &DownloadsPage::refresh);
    connect(m_aria2, &Aria2Manager::statisticsChanged, this, &DownloadsPage::refresh);
    connect(m_aria2, &Aria2Manager::engineReadyChanged, this, &DownloadsPage::refresh);
    connect(m_aria2, &Aria2Manager::engineErrorChanged, this, &DownloadsPage::refresh);
    connect(FluentTheme::instance(), &FluentTheme::changed, this, &DownloadsPage::restyle);
}

void DownloadsPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::EnabledChange || event->type() == QEvent::PaletteChange)
        restyle();
}

void DownloadsPage::setStatusFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    // NOTE: the filter is deliberately kept per page. Aria2Manager::setFilter()
    // is a *global* view filter, and this page is instantiated twice (all
    // downloads + the queue); letting both drive the shared filter means the one
    // constructed last wins and the other renders an empty list forever.
    restyle();
    refresh();
}

/// Apply this page's own status filter plus the global search text.
QVariantList DownloadsPage::visibleTasks() const
{
    QVariantList visible;
    if (!m_aria2)
        return visible;

    const QString needle = m_aria2->searchText().trimmed().toLower();
    const QVariantList all = m_aria2->tasks();
    for (const QVariant &v : all) {
        const QVariantMap t = v.toMap();
        const QString status = t.value(QStringLiteral("status")).toString();

        bool show = true;
        if (m_filter == QLatin1String("active"))
            show = status == QLatin1String("active");
        else if (m_filter == QLatin1String("waiting"))
            show = status == QLatin1String("waiting") || status == QLatin1String("paused");
        else if (m_filter == QLatin1String("complete"))
            show = status == QLatin1String("complete");
        else if (m_filter == QLatin1String("error"))
            show = status == QLatin1String("error");
        else if (m_filter == QLatin1String("bt"))
            show = t.value(QStringLiteral("isTorrent")).toBool();

        if (show && !needle.isEmpty()) {
            show = t.value(QStringLiteral("fileName")).toString().toLower().contains(needle)
                || t.value(QStringLiteral("uri")).toString().toLower().contains(needle);
        }
        if (show)
            visible << v;
    }
    return visible;
}

void DownloadsPage::setShowFilterBar(bool show)
{
    ui->filterChips->setVisible(show);
    ui->sortCombo->setVisible(show);
}

void DownloadsPage::setEmptyStateText(const QString &title, const QString &hint)
{
    m_emptyTitle = title;
    m_emptyHint = hint;
    m_list->setEmptyStateText(title, hint);
}

void DownloadsPage::selectTask(const QString &gid)
{
    m_list->setSelectedGid(gid);
}

void DownloadsPage::restyle()
{
    const FluentTheme *t = FluentTheme::instance();
    const QList<QColor> tints = {t->accent(), t->success(), t->info(), t->caution()};
    for (StatCard *card : std::as_const(m_cards)) {
        const int slot = card->property("tintSlot").toInt();
        card->setTint(tints.value(slot, t->accent()));
    }
    for (FluentButton *chip : std::as_const(m_chips)) {
        const bool selected = chip->property("chipKey").toString() == m_filter;
        chip->setStyleSheet(chipStyle(t, selected));
    }
}

void DownloadsPage::refresh()
{
    if (!m_aria2)
        return;

    // ---- task list -------------------------------------------------------
    const QVariantList filtered = visibleTasks();
    m_list->setTasks(filtered);

    // ---- header ----------------------------------------------------------
    const QVariantMap stats = m_aria2->statistics();
    const int shown = m_list->cardCount();
    const int total = stats.value(QStringLiteral("taskCount")).toInt();
    QString title;
    if (m_filter == QLatin1String("active"))
        title = tr("正在下载");
    else if (m_filter == QLatin1String("waiting"))
        title = tr("队列");
    else if (m_filter == QLatin1String("complete"))
        title = tr("已完成");
    else if (m_filter == QLatin1String("error"))
        title = tr("失败的任务");
    else if (m_filter == QLatin1String("bt"))
        title = tr("BitTorrent 任务");
    else
        title = tr("全部下载");
    ui->pageTitle->setText(title);
    ui->pageSubtitle->setText(tr("%1 个任务显示中 · 共 %2 个").arg(shown).arg(total));

    // ---- statistics ------------------------------------------------------
    const int activeCount = stats.value(QStringLiteral("activeCount")).toInt();
    const int waitingCount = stats.value(QStringLiteral("waitingCount")).toInt();
    const int stoppedCount = stats.value(QStringLiteral("stoppedCount")).toInt();
    m_cards[0]->setValue(FluentTheme::formatSpeed(
        stats.value(QStringLiteral("downloadSpeed")).toDouble()));
    m_cards[1]->setValue(FluentTheme::formatSpeed(
        stats.value(QStringLiteral("uploadSpeed")).toDouble()));
    m_cards[1]->setSecondary(tr("已上传 %1").arg(
        FluentTheme::formatSize(stats.value(QStringLiteral("uploadedLength")).toDouble())));
    m_cards[2]->setValue(QStringLiteral("%1 / %2").arg(activeCount).arg(waitingCount));
    m_cards[2]->setSecondary(tr("已完成 %1").arg(stoppedCount));
    m_cards[3]->setValue(FluentTheme::formatSize(
        stats.value(QStringLiteral("completedLength")).toDouble()));
    m_cards[3]->setSecondary(tr("总量 %1").arg(
        FluentTheme::formatSize(stats.value(QStringLiteral("totalLength")).toDouble())));

    // ---- command bar state ----------------------------------------------
    m_pauseAllButton->setVisible(activeCount > 0);
    m_resumeAllButton->setVisible(activeCount == 0 && waitingCount > 0);
    m_clearButton->setEnabled(stoppedCount > 0);

    // ---- engine banner ---------------------------------------------------
    const bool ready = m_aria2->engineReady();
    const QString error = m_aria2->engineError();
    if (!ready && !error.isEmpty()) {
        m_banner->setTitle(tr("aria2 引擎未就绪"));
        m_banner->setMessage(error);
        m_banner->show();
    } else {
        m_banner->hide();
    }

    restyle();
}
