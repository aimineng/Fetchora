#include "ui/pages/TaskDetailsPanel.h"

#include "Aria2Manager.h"
#include "ui/FluentButton.h"
#include "ui/FluentInputs.h"
#include "ui/FluentTheme.h"
#include "ui/FluentWidgets.h"
#include "ui/pages/ui_TaskDetailsPanel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

/// Dynamic property the generated style sheet uses for small captions.
const char *kCaptionRole = "fluentRole";
const char *kCaptionValue = "caption";

QString elide(const QString &text, int max = 96)
{
    if (text.size() <= max)
        return text;
    return text.left(max - 1) + QChar(0x2026);
}

} // namespace

TaskDetailsPanel::TaskDetailsPanel(Aria2Manager *aria2, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TaskDetailsPanel)
    , m_aria2(aria2)
{
    ui->setupUi(this);
    setMinimumWidth(320);

    ui->panelTitle->setProperty(kCaptionRole, "subtitle");
    ui->panelSubtitle->setProperty(kCaptionRole, kCaptionValue);

    buildHeadline();
    buildTabs();
    buildCommands();
    buildSections();

    if (m_aria2) {
        connect(m_aria2, &Aria2Manager::taskDetailChanged, this, &TaskDetailsPanel::refresh);
        connect(m_aria2, &Aria2Manager::detailGidChanged, this, [this]() {
            if (m_aria2->detailGid() != m_gid)
                setGid(m_aria2->detailGid());
        });
        connect(m_aria2, &Aria2Manager::tasksChanged, this, &TaskDetailsPanel::refresh);
    }
    connect(FluentTheme::instance(), &FluentTheme::changed, this, &TaskDetailsPanel::restyle);

    setGid(m_aria2 ? m_aria2->detailGid() : QString());
}

TaskDetailsPanel::~TaskDetailsPanel()
{
    delete ui;
}

// ============================================================================
//  construction
// ============================================================================
void TaskDetailsPanel::buildHeadline()
{
    m_headGlyph = new FluentIcon(FluentTheme::Glyph::File, 20, ui->headlineHost);
    m_headGlyph->setFixedSize(40, 40);

    auto *text = new QWidget(ui->headlineHost);
    auto *textLayout = new QVBoxLayout(text);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    m_headName = new QLabel(text);
    m_headName->setProperty(kCaptionRole, "subtitle");
    m_headName->setWordWrap(false);

    m_headMeta = new QLabel(text);
    m_headMeta->setProperty(kCaptionRole, kCaptionValue);

    m_headProgress = new FluentProgressBar(text);
    m_headProgress->setBarHeight(3);

    textLayout->addWidget(m_headName);
    textLayout->addWidget(m_headMeta);
    textLayout->addWidget(m_headProgress);

    ui->headlineLayout->addWidget(m_headGlyph, 0, Qt::AlignTop);
    ui->headlineLayout->addWidget(text, 1);
}

void TaskDetailsPanel::buildTabs()
{
    struct Spec { const char *caption; };
    const QList<Spec> specs = {
        {QT_TR_NOOP("概要")}, {QT_TR_NOOP("文件")}, {QT_TR_NOOP("连接")},
        {QT_TR_NOOP("服务器")}, {QT_TR_NOOP("选项")},
    };
    for (int i = 0; i < specs.size(); ++i) {
        auto *tab = new FluentButton(tr(specs.at(i).caption), this);
        tab->setRole(FluentButton::Subtle);
        tab->setCompact(true);
        tab->setFixedHeight(28);
        connect(tab, &QPushButton::clicked, this, [this, i]() { setSection(i); });
        ui->tabLayout->addWidget(tab);
        m_tabs << tab;
    }
    ui->tabLayout->addStretch(1);
}

void TaskDetailsPanel::buildCommands()
{
    // Icon-only with tooltips: the inspector is a splitter pane and the English
    // captions do not fit once the pane is narrow.
    auto add = [this](const QChar &glyph, const char *caption, const char *tip) {
        auto *b = new FluentButton(this);
        b->setGlyph(glyph);
        b->setText(tr(caption));
        b->setIconOnly(true);
        b->setRole(FluentButton::Subtle);
        b->setCompact(true);
        b->setTooltipText(tr(tip));
        ui->commandLayout->addWidget(b);
        return b;
    };

    m_openButton = add(FluentTheme::Glyph::OpenFile, QT_TR_NOOP("打开"), QT_TR_NOOP("打开已完成的文件"));
    m_folderButton = add(FluentTheme::Glyph::Folder, QT_TR_NOOP("文件夹"), QT_TR_NOOP("在资源管理器中显示"));
    m_copyButton = add(FluentTheme::Glyph::Copy, QT_TR_NOOP("复制链接"), QT_TR_NOOP("复制下载地址"));
    m_pauseButton = add(FluentTheme::Glyph::Pause, QT_TR_NOOP("暂停"), QT_TR_NOOP("暂停或继续"));
    m_retryButton = add(FluentTheme::Glyph::Refresh, QT_TR_NOOP("重试"), QT_TR_NOOP("重新开始失败的任务"));
    m_removeButton = add(FluentTheme::Glyph::Delete, QT_TR_NOOP("移除"), QT_TR_NOOP("从列表移除（不删除文件）"));
    m_removeButton->setRole(FluentButton::Danger);

    ui->commandLayout->addStretch(1);

    m_closeButton = new FluentButton(this);
    m_closeButton->setGlyph(FluentTheme::Glyph::Close);
    m_closeButton->setIconOnly(true);
    m_closeButton->setCompact(true);
    m_closeButton->setRole(FluentButton::Subtle);
    m_closeButton->setTooltipText(tr("隐藏详情面板"));
    ui->headerActionLayout->addWidget(m_closeButton);

    connect(m_closeButton, &QPushButton::clicked, this, &TaskDetailsPanel::closeRequested);
    connect(m_openButton, &QPushButton::clicked, this, [this]() {
        if (m_aria2 && !m_gid.isEmpty())
            m_aria2->openFile(m_gid);
    });
    connect(m_folderButton, &QPushButton::clicked, this, [this]() {
        if (m_aria2 && !m_gid.isEmpty())
            m_aria2->openFolder(m_gid);
    });
    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        if (!m_aria2 || m_gid.isEmpty())
            return;
        const QVariantMap detail = m_aria2->taskDetail();
        const QString uri = detail.value(QStringLiteral("uri")).toString();
        m_aria2->copyToClipboard(uri);
        emit toast(tr("已复制下载链接"), false);
    });
    connect(m_pauseButton, &QPushButton::clicked, this, [this]() {
        if (!m_aria2 || m_gid.isEmpty())
            return;
        const QString status = m_aria2->taskDetail().value(QStringLiteral("status")).toString();
        if (status == QLatin1String("active") || status == QLatin1String("waiting"))
            m_aria2->pauseTask(m_gid);
        else
            m_aria2->resumeTask(m_gid);
    });
    connect(m_retryButton, &QPushButton::clicked, this, [this]() {
        if (m_aria2 && !m_gid.isEmpty())
            m_aria2->retryTask(m_gid);
    });
    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
        if (m_aria2 && !m_gid.isEmpty())
            m_aria2->removeTask(m_gid, 0);
    });
}

void TaskDetailsPanel::buildSections()
{
    for (int i = 0; i < SectionCount; ++i) {
        auto *scroll = new QScrollArea(ui->sectionStack);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));

        auto *content = new QWidget(scroll);
        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(0, 0, 8, 0);
        layout->setSpacing(10);
        layout->addStretch(1);
        scroll->setWidget(content);

        m_sectionContent << content;
        m_sectionScrolls << scroll;
        ui->sectionStack->addWidget(scroll);
    }
    setSection(Overview);
}

QVBoxLayout *TaskDetailsPanel::addCard(QVBoxLayout *into, const char *heading)
{
    auto *card = new FluentCard(into->parentWidget());
    card->setVariant(FluentCard::Card);
    QVBoxLayout *body = card->body();
    body->setContentsMargins(14, 12, 14, 14);
    body->setSpacing(8);

    auto *title = new QLabel(tr(heading), card);
    title->setProperty(kCaptionRole, "subtitle");
    m_captionLabels << title;
    m_captions << heading;
    body->addWidget(title);

    // Insert before the trailing stretch so cards stack downwards.
    into->insertWidget(into->count() - 1, card);
    return body;
}

QLabel *TaskDetailsPanel::addField(QGridLayout *grid, int row, const char *caption, bool mono)
{
    auto *label = new QLabel(tr(caption), grid->parentWidget());
    label->setProperty(kCaptionRole, kCaptionValue);
    label->setMinimumWidth(84);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_captionLabels << label;
    m_captions << caption;

    auto *value = new QLabel(grid->parentWidget());
    value->setWordWrap(true);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (mono)
        value->setFont(FluentTheme::monoFont());

    grid->addWidget(label, row, 0);
    grid->addWidget(value, row, 1);
    return value;
}

void TaskDetailsPanel::buildOverview(QWidget *content)
{
    auto *root = qobject_cast<QVBoxLayout *>(content->layout());
    const QVariantMap detail = m_aria2 ? m_aria2->taskDetail() : QVariantMap();
    const FluentTheme *t = FluentTheme::instance();

    // ---- identity --------------------------------------------------------
    QVBoxLayout *basic = addCard(root, QT_TR_NOOP("基本信息"));
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    basic->addLayout(grid);

    int row = 0;
    QLabel *name = addField(grid, row++, QT_TR_NOOP("名称"));
    name->setText(elide(detail.value(QStringLiteral("fileName")).toString(), 200));
    name->setProperty(kCaptionRole, "body");

    addField(grid, row++, QT_TR_NOOP("GID"))
        ->setText(detail.value(QStringLiteral("gid")).toString());
    addField(grid, row++, QT_TR_NOOP("状态"))
        ->setText(FluentTheme::statusLabel(detail.value(QStringLiteral("status")).toString()));
    addField(grid, row++, QT_TR_NOOP("保存到"))
        ->setText(elide(detail.value(QStringLiteral("dir")).toString(), 200));
    addField(grid, row++, QT_TR_NOOP("类型"))->setText(
        detail.value(QStringLiteral("isTorrent")).toBool() ? tr("BitTorrent 任务")
                                                           : tr("普通下载"));
    if (!detail.value(QStringLiteral("infoHash")).toString().isEmpty()) {
        addField(grid, row++, QT_TR_NOOP("信息哈希"), true)
            ->setText(FluentTheme::prettyInfoHash(
                detail.value(QStringLiteral("infoHash")).toString()));
    }
    const int pieceLength = detail.value(QStringLiteral("pieceLength")).toInt();
    if (pieceLength > 0) {
        addField(grid, row++, QT_TR_NOOP("分片"))
            ->setText(tr("%1 片 × %2")
                          .arg(detail.value(QStringLiteral("pieceCount")).toInt())
                          .arg(FluentTheme::formatSize(pieceLength)));
    }

    // ---- transfer --------------------------------------------------------
    QVBoxLayout *transfer = addCard(root, QT_TR_NOOP("传输"));
    auto *grid2 = new QGridLayout();
    grid2->setHorizontalSpacing(12);
    grid2->setVerticalSpacing(6);
    grid2->setColumnStretch(1, 1);
    transfer->addLayout(grid2);

    row = 0;
    addField(grid2, row++, QT_TR_NOOP("总大小"))
        ->setText(FluentTheme::formatSize(detail.value(QStringLiteral("totalLength")).toDouble()));
    addField(grid2, row++, QT_TR_NOOP("已下载"))
        ->setText(FluentTheme::formatSize(
            detail.value(QStringLiteral("completedLength")).toDouble()));
    addField(grid2, row++, QT_TR_NOOP("已上传"))
        ->setText(FluentTheme::formatSize(detail.value(QStringLiteral("uploadLength")).toDouble()));
    addField(grid2, row++, QT_TR_NOOP("下载速度"))
        ->setText(FluentTheme::formatSpeed(
            detail.value(QStringLiteral("downloadSpeed")).toDouble()));
    addField(grid2, row++, QT_TR_NOOP("上传速度"))
        ->setText(FluentTheme::formatSpeed(
            detail.value(QStringLiteral("uploadSpeed")).toDouble()));
    addField(grid2, row++, QT_TR_NOOP("平均速度"))
        ->setText(FluentTheme::formatSpeed(detail.value(QStringLiteral("avgSpeed")).toDouble()));
    addField(grid2, row++, QT_TR_NOOP("剩余时间"))
        ->setText(FluentTheme::formatDuration(detail.value(QStringLiteral("eta")).toDouble()));
    addField(grid2, row++, QT_TR_NOOP("连接数"))
        ->setText(QString::number(detail.value(QStringLiteral("connections")).toInt()));
    if (detail.value(QStringLiteral("isTorrent")).toBool()) {
        addField(grid2, row++, QT_TR_NOOP("种子 / 用户"))
            ->setText(tr("%1 个种子 · %2 个连接")
                          .arg(detail.value(QStringLiteral("numSeeders")).toInt())
                          .arg(detail.value(QStringLiteral("connections")).toInt()));
    }

    const QString error = detail.value(QStringLiteral("errorMessage")).toString();
    if (!error.isEmpty()) {
        QVBoxLayout *errCard = addCard(root, QT_TR_NOOP("错误信息"));
        auto *label = new QLabel(error, errCard->parentWidget());
        label->setWordWrap(true);
        label->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->critical().name()));
        errCard->addWidget(label);
    }

    root->addStretch(1);
}

void TaskDetailsPanel::buildFilesSection(QWidget *content)
{
    auto *root = qobject_cast<QVBoxLayout *>(content->layout());
    const QVariantList files = m_aria2
                                   ? m_aria2->taskDetail().value(QStringLiteral("files")).toList()
                                   : QVariantList();

    QVBoxLayout *card = addCard(root, QT_TR_NOOP("文件列表"));
    Q_UNUSED(card)

    if (files.isEmpty()) {
        auto *empty = new QLabel(tr("该任务还没有可显示的文件信息"), content);
        empty->setProperty(kCaptionRole, kCaptionValue);
        root->insertWidget(root->count() - 1, empty);
        root->addStretch(1);
        return;
    }

    for (const QVariant &v : files) {
        const QVariantMap f = v.toMap();
        const qint64 length = f.value(QStringLiteral("length")).toLongLong();
        const qint64 done = f.value(QStringLiteral("completedLength")).toLongLong();
        const double pct = length > 0 ? double(done) * 100.0 / double(length) : 0.0;
        const QString path = f.value(QStringLiteral("path")).toString();

        auto *rowWidget = new QWidget(content);
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(10);

        const int index = f.value(QStringLiteral("index")).toInt();
        auto *check = new FluentCheckBox(rowWidget);
        check->setChecked(m_filesLoaded
                              ? m_selectedFiles.contains(QString::number(index))
                              : f.value(QStringLiteral("selected")).toBool());
        connect(check, &QCheckBox::toggled, this, [this, index](bool on) {
            const QString key = QString::number(index);
            if (on && !m_selectedFiles.contains(key))
                m_selectedFiles << key;
            else if (!on)
                m_selectedFiles.removeAll(key);
            m_filesLoaded = true;
        });
        rowLayout->addWidget(check);

        auto *text = new QWidget(rowWidget);
        auto *textLayout = new QVBoxLayout(text);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(3);

        auto *nameLabel = new QLabel(elide(f.value(QStringLiteral("name")).toString()), text);
        nameLabel->setToolTip(path);
        auto *metaLabel = new QLabel(
            tr("%1 / %2 · %3%")
                .arg(FluentTheme::formatSize(double(done)),
                     FluentTheme::formatSize(double(length)))
                .arg(int(pct)),
            text);
        metaLabel->setProperty(kCaptionRole, kCaptionValue);

        auto *bar = new FluentProgressBar(text);
        bar->setBarHeight(3);
        bar->setValue(pct);

        textLayout->addWidget(nameLabel);
        textLayout->addWidget(metaLabel);
        textLayout->addWidget(bar);
        rowLayout->addWidget(text, 1);

        root->insertWidget(root->count() - 1, rowWidget);
    }

    auto *actions = new QWidget(content);
    auto *actionLayout = new QHBoxLayout(actions);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);

    auto *all = new FluentButton(tr("全选"), actions);
    all->setRole(FluentButton::Subtle);
    all->setCompact(true);
    auto *none = new FluentButton(tr("全不选"), actions);
    none->setRole(FluentButton::Subtle);
    none->setCompact(true);
    auto *apply = new FluentButton(tr("应用选择"), actions);
    apply->setRole(FluentButton::Accent);
    apply->setCompact(true);

    QStringList everyIndex;
    for (const QVariant &v : files)
        everyIndex << QString::number(v.toMap().value(QStringLiteral("index")).toInt());

    connect(all, &QPushButton::clicked, this, [this, everyIndex]() {
        m_selectedFiles = everyIndex;
        m_filesLoaded = true;
        refresh();
    });
    connect(none, &QPushButton::clicked, this, [this]() {
        m_selectedFiles.clear();
        m_filesLoaded = true;
        refresh();
    });
    connect(apply, &QPushButton::clicked, this, [this]() {
        if (m_aria2 && !m_gid.isEmpty()) {
            m_aria2->selectTaskFiles(m_gid, m_selectedFiles);
            emit toast(tr("已更新文件选择"), false);
        }
    });

    actionLayout->addWidget(all);
    actionLayout->addWidget(none);
    actionLayout->addStretch(1);
    actionLayout->addWidget(apply);
    root->insertWidget(root->count() - 1, actions);
    root->addStretch(1);
}

void TaskDetailsPanel::buildPeersSection(QWidget *content)
{
    auto *root = qobject_cast<QVBoxLayout *>(content->layout());
    const QVariantList peers = m_aria2
                                   ? m_aria2->taskDetail().value(QStringLiteral("peers")).toList()
                                   : QVariantList();
    const FluentTheme *t = FluentTheme::instance();

    if (peers.isEmpty()) {
        auto *empty = new QLabel(tr("暂无连接的用户（仅 BitTorrent 任务有 Peer 信息）"), content);
        empty->setProperty(kCaptionRole, kCaptionValue);
        empty->setWordWrap(true);
        root->insertWidget(root->count() - 1, empty);
        root->addStretch(1);
        return;
    }

    addCard(root, QT_TR_NOOP("已连接的用户"));
    for (const QVariant &v : peers) {
        const QVariantMap p = v.toMap();
        const bool seeder = p.value(QStringLiteral("seeder")).toBool();
        auto *row = addListRow(root,
                               p.value(QStringLiteral("ip")).toString() + QLatin1Char(':')
                                   + p.value(QStringLiteral("port")).toString(),
                               tr("上传 %1 · 下载 %2")
                                   .arg(FluentTheme::formatSpeed(
                                            p.value(QStringLiteral("uploadSpeed")).toDouble()),
                                        FluentTheme::formatSpeed(
                                            p.value(QStringLiteral("speed")).toDouble())),
                               seeder ? tr("做种") : tr("下载"),
                               seeder ? t->success() : t->info());
        Q_UNUSED(row)
    }
    root->addStretch(1);
}

void TaskDetailsPanel::buildServersSection(QWidget *content)
{
    auto *root = qobject_cast<QVBoxLayout *>(content->layout());
    const QVariantMap detail = m_aria2 ? m_aria2->taskDetail() : QVariantMap();
    const QVariantList servers = detail.value(QStringLiteral("servers")).toList();
    const QVariantList uris = detail.value(QStringLiteral("uris")).toList();
    const FluentTheme *t = FluentTheme::instance();

    if (!uris.isEmpty()) {
        addCard(root, QT_TR_NOOP("下载地址"));
        for (const QVariant &v : uris) {
            const QVariantMap u = v.toMap();
            const QString status = u.value(QStringLiteral("status")).toString();
            addListRow(root, elide(u.value(QStringLiteral("uri")).toString(), 120), status,
                       status, status == QLatin1String("used") ? t->success() : t->textTertiary());
        }
    }

    if (!servers.isEmpty()) {
        addCard(root, QT_TR_NOOP("服务器 / 镜像"));
        for (const QVariant &v : servers) {
            const QVariantMap s = v.toMap();
            addListRow(root, elide(s.value(QStringLiteral("currentUri")).toString(), 120),
                       tr("连接 %1").arg(s.value(QStringLiteral("index")).toString()),
                       FluentTheme::formatSpeed(
                           s.value(QStringLiteral("downloadSpeed")).toDouble()),
                       t->accent());
        }
    }

    if (uris.isEmpty() && servers.isEmpty()) {
        auto *empty = new QLabel(tr("暂无服务器信息"), content);
        empty->setProperty(kCaptionRole, kCaptionValue);
        root->insertWidget(root->count() - 1, empty);
    }
    root->addStretch(1);
}

void TaskDetailsPanel::buildOptionsSection(QWidget *content)
{
    auto *root = qobject_cast<QVBoxLayout *>(content->layout());
    const QVariantMap options = m_aria2
                                    ? m_aria2->taskDetail().value(QStringLiteral("options")).toMap()
                                    : QVariantMap();

    QVBoxLayout *card = addCard(root, QT_TR_NOOP("aria2 选项"));
    if (options.isEmpty()) {
        auto *empty = new QLabel(tr("正在读取任务选项…"), content);
        empty->setProperty(kCaptionRole, kCaptionValue);
        root->insertWidget(root->count() - 1, empty);
        root->addStretch(1);
        if (m_aria2 && !m_gid.isEmpty())
            m_aria2->fetchTaskOptions(m_gid);
        return;
    }

    auto *refresh = new FluentButton(tr("刷新选项"), card->parentWidget());
    refresh->setRole(FluentButton::Subtle);
    refresh->setCompact(true);
    connect(refresh, &QPushButton::clicked, this, [this]() {
        if (m_aria2 && !m_gid.isEmpty())
            m_aria2->fetchTaskOptions(m_gid);
    });
    card->addWidget(refresh);

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(1, 1);
    card->addLayout(grid);

    QStringList keys = options.keys();
    keys.sort();
    int row = 0;
    for (const QString &key : std::as_const(keys)) {
        auto *k = new QLabel(key, card->parentWidget());
        k->setProperty(kCaptionRole, kCaptionValue);
        k->setFont(FluentTheme::monoFont());
        auto *val = new QLabel(elide(options.value(key).toString(), 160), card->parentWidget());
        val->setFont(FluentTheme::monoFont());
        val->setTextInteractionFlags(Qt::TextSelectableByMouse);
        val->setWordWrap(true);
        grid->addWidget(k, row, 0);
        grid->addWidget(val, row, 1);
        ++row;
    }
    root->addStretch(1);
}

QWidget *TaskDetailsPanel::addListRow(QVBoxLayout *into, const QString &primary,
                                      const QString &secondary, const QString &trailing,
                                      const QColor &tint, QWidget *leading)
{
    Q_UNUSED(leading)
    auto *row = new QWidget(into->parentWidget());
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(2, 4, 2, 4);
    layout->setSpacing(10);

    auto *text = new QWidget(row);
    auto *textLayout = new QVBoxLayout(text);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    auto *primaryLabel = new QLabel(primary, text);
    primaryLabel->setWordWrap(true);
    auto *secondaryLabel = new QLabel(secondary, text);
    secondaryLabel->setProperty(kCaptionRole, kCaptionValue);
    textLayout->addWidget(primaryLabel);
    textLayout->addWidget(secondaryLabel);

    auto *trailingLabel = new QLabel(trailing, row);
    trailingLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(tint.name()));

    layout->addWidget(text, 1);
    layout->addWidget(trailingLabel, 0, Qt::AlignRight | Qt::AlignVCenter);

    into->insertWidget(into->count() - 1, row);
    return row;
}

// ============================================================================
//  state
// ============================================================================
void TaskDetailsPanel::setGid(const QString &gid)
{
    const bool changed = (m_gid != gid);
    m_gid = gid;
    if (changed) {
        m_selectedFiles.clear();
        m_filesLoaded = false;
    }
    if (m_aria2 && !gid.isEmpty() && m_section == Options)
        m_aria2->fetchTaskOptions(gid);
    refresh();
}

void TaskDetailsPanel::setSection(int section)
{
    m_section = qBound(0, section, int(SectionCount) - 1);
    ui->sectionStack->setCurrentIndex(m_section);
    restyle();
    refresh();
}

void TaskDetailsPanel::clearLayout(QLayout *layout)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            // Hide before the deferred delete: a widget that was taken out of the
            // layout is still a visible child until the event loop gets to it, so
            // it kept being painted on top of the freshly built rows.
            w->hide();
            w->deleteLater();
        } else if (QLayout *child = item->layout()) {
            clearLayout(child);
        }
        delete item;
    }
}

void TaskDetailsPanel::refresh()
{
    if (!m_aria2 || !ui)
        return;

    const QVariantMap detail = m_aria2->taskDetail();
    const bool hasTask = !m_gid.isEmpty() && !detail.isEmpty();

    // ---- headline --------------------------------------------------------
    const FluentTheme *t = FluentTheme::instance();
    if (hasTask) {
        m_headName->setText(detail.value(QStringLiteral("fileName")).toString());
        m_headMeta->setText(
            tr("%1 · %2 / %3")
                .arg(FluentTheme::statusLabel(detail.value(QStringLiteral("status")).toString()),
                     FluentTheme::formatSize(
                         detail.value(QStringLiteral("completedLength")).toDouble()),
                     FluentTheme::formatSize(detail.value(QStringLiteral("totalLength")).toDouble())));
        const double progress = detail.value(QStringLiteral("progress")).toDouble();
        m_headProgress->setValue(progress);
        m_headProgress->setBarColor(
            t->statusColor(detail.value(QStringLiteral("status")).toString()));
        m_headProgress->setVisible(true);
        m_headGlyph->setGlyph(detail.value(QStringLiteral("isTorrent")).toBool()
                                  ? FluentTheme::Glyph::Torrent
                                  : FluentTheme::fileGlyph(
                                        detail.value(QStringLiteral("fileName")).toString()));
        m_headGlyph->setIconColor(
            t->statusColor(detail.value(QStringLiteral("status")).toString()));
    } else {
        m_headName->setText(tr("未选择任务"));
        m_headMeta->setText(tr("在上方列表中选择一个任务即可查看详情"));
        m_headProgress->setVisible(false);
        m_headGlyph->setGlyph(FluentTheme::Glyph::File);
        m_headGlyph->setIconColor(t->textTertiary());
    }

    const QString status = detail.value(QStringLiteral("status")).toString();
    const bool isActive = status == QLatin1String("active");
    const bool isDone = status == QLatin1String("complete");
    const bool isError = status == QLatin1String("error");
    m_pauseButton->setGlyph(isActive ? FluentTheme::Glyph::Pause : FluentTheme::Glyph::Play);
    m_pauseButton->setText(isActive ? tr("暂停") : tr("继续"));
    m_pauseButton->setVisible(hasTask);
    m_retryButton->setVisible(hasTask && (isError || isDone));
    m_openButton->setEnabled(isDone);
    m_folderButton->setEnabled(hasTask);
    m_copyButton->setEnabled(hasTask);
    for (FluentButton *button : std::as_const(m_tabs))
        button->setVisible(hasTask);

    // ---- section body ----------------------------------------------------
    QWidget *content = m_sectionContent.value(m_section);
    if (!content)
        return;

    // The body is rebuilt from scratch (the sections are short lists of labels,
    // and this keeps one code path instead of five update-in-place ones). Two
    // things make that invisible:
    //
    //   * repaints are off while the old rows are hidden and the new ones are
    //     built, so the panel never shows a half-empty intermediate state, and
    //   * the scroll position is restored afterwards, because a rebuild resets
    //     the bar and the user could never scroll past the first screen.
    QScrollArea *scroll = m_sectionScrolls.value(m_section);
    const int scrollPos = scroll ? scroll->verticalScrollBar()->value() : 0;
    const bool updates = content->updatesEnabled();
    content->setUpdatesEnabled(false);

    clearLayout(content->layout());
    m_captionLabels.clear();
    m_captions.clear();
    qobject_cast<QVBoxLayout *>(content->layout())->addStretch(1);

    if (!hasTask) {
        auto *empty = new QLabel(tr("未选择任务"), content);
        empty->setProperty(kCaptionRole, kCaptionValue);
        auto *layout = qobject_cast<QVBoxLayout *>(content->layout());
        layout->insertWidget(layout->count() - 1, empty);
        layout->addStretch(1);
        content->setUpdatesEnabled(updates);
        return;
    }

    switch (m_section) {
    case Overview: buildOverview(content); break;
    case Files:    buildFilesSection(content); break;
    case Peers:    buildPeersSection(content); break;
    case Servers:  buildServersSection(content); break;
    case Options:  buildOptionsSection(content); break;
    default: break;
    }

    content->setUpdatesEnabled(updates);
    if (scroll) {
        // Let the new rows decide the content size before restoring the position:
        // the scroll bar's range follows the layout, and a value set against a
        // stale range is simply clamped away.
        if (auto *layout = qobject_cast<QVBoxLayout *>(content->layout()))
            layout->activate();
        scroll->verticalScrollBar()->setValue(scrollPos);
    }
}

// ============================================================================
//  theming + language
// ============================================================================
void TaskDetailsPanel::restyle()
{
    const FluentTheme *t = FluentTheme::instance();
    for (int i = 0; i < m_tabs.size(); ++i) {
        const bool current = (i == m_section);
        m_tabs.at(i)->setStyleSheet(
            current
                ? QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3;"
                                 " border-radius: 14px; padding: 0 14px; }")
                      .arg(QColor(t->accent().red(), t->accent().green(), t->accent().blue(),
                                  t->isDark() ? 61 : 36)
                               .name(QColor::HexArgb),
                           t->accent().name(),
                           QColor(t->accent().red(), t->accent().green(), t->accent().blue(), 128)
                               .name(QColor::HexArgb))
                : QStringLiteral("QPushButton { background: transparent; color: %1;"
                                 " border: 1px solid %2; border-radius: 14px; padding: 0 14px; }"
                                 "QPushButton:hover { background: %3; }")
                      .arg(t->textSecondary().name(), t->strokeSubtle().name(),
                           t->subtleHover().name()));
    }
    ui->panelSubtitle->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));
}

void TaskDetailsPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::LanguageChange)
        return;

    ui->retranslateUi(this);
    // Tabs, buttons and cards are built in C++: rebuild their captions.
    static const char *tabCaptions[SectionCount] = {"概要", "文件", "连接", "服务器", "选项"};
    for (int i = 0; i < m_tabs.size(); ++i)
        m_tabs.at(i)->setText(tr(tabCaptions[i]));

    m_closeButton->setTooltipText(tr("隐藏详情面板"));
    m_openButton->setText(tr("打开"));
    m_folderButton->setText(tr("文件夹"));
    m_copyButton->setText(tr("复制链接"));
    m_retryButton->setText(tr("重试"));
    m_removeButton->setText(tr("移除"));
    m_pauseButton->setText(m_aria2
                               && m_aria2->taskDetail().value(QStringLiteral("status")).toString()
                                      == QLatin1String("active")
                               ? tr("暂停")
                               : tr("继续"));

    // Captions recorded by addField()/addCard() were already translated once;
    // refresh() rebuilds every section with the new catalogue.
    refresh();
}
