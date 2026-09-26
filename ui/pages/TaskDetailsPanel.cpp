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
#include <QSet>
#include <QSignalBlocker>
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
        {QT_TR_NOOP("概要")}, {QT_TR_NOOP("连接")},
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
        if (uri.isEmpty()) {
            emit toast(tr("这个任务没有可复制的链接"), true);
            return;
        }
        m_aria2->copyToClipboard(uri);
        // Naming the task matters: this button copies the *inspected* task, which
        // is not necessarily the row the pointer is over.
        const QString name = detail.value(QStringLiteral("fileName")).toString();
        emit toast(name.isEmpty() ? tr("已复制下载链接：%1").arg(uri)
                                  : tr("已复制「%1」的链接：%2").arg(name, uri),
                   false);
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
    buildSkeleton();
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

QLabel *TaskDetailsPanel::field(const QString &id, QGridLayout *grid, const char *caption, bool mono)
{
    auto it = m_fields.constFind(id);
    if (it != m_fields.constEnd())
        return it->value;

    FieldRow row;
    row.captionText = caption;
    row.caption = new QLabel(tr(caption), grid->parentWidget());
    row.caption->setProperty(kCaptionRole, kCaptionValue);
    row.caption->setMinimumWidth(84);
    row.caption->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_captionLabels << row.caption;
    m_captions << caption;

    // Elided rather than wrapped - see rowFor().
    row.value = new ElidedLabel(grid->parentWidget());
    row.value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (mono)
        row.value->setFont(FluentTheme::monoFont());

    m_fields.insert(id, row);
    return row.value;
}

void TaskDetailsPanel::setField(const QString &id, const QString &text)
{
    auto it = m_fields.find(id);
    if (it == m_fields.end() || it->text == text)
        // Identical text: not touching the label means no repaint at all, which
        // is the whole point - most values do not change between two polls.
        return;
    it->text = text;
    it->value->setFullText(text);
}

void TaskDetailsPanel::setFieldVisible(const QString &id, bool visible)
{
    // Remembering what was asked for, rather than reading isVisible(): a row inside
    // a hidden card is not "visible" either, and that made the comparison lie.
    auto it = m_fields.find(id);
    if (it == m_fields.end() || it->wanted == visible)
        return;
    it->wanted = visible;
    it->caption->setVisible(visible);
    it->value->setVisible(visible);
}

TaskDetailsPanel::Row *TaskDetailsPanel::rowFor(QList<Row> &rows, const QString &key,
                                                QVBoxLayout *into)
{
    for (Row &row : rows) {
        if (row.key == key)
            return &row;
    }

    Row row;
    row.key = key;
    row.widget = new QWidget(into->parentWidget());
    auto *layout = new QHBoxLayout(row.widget);
    layout->setContentsMargins(2, 4, 2, 4);
    layout->setSpacing(10);

    auto *text = new QWidget(row.widget);
    auto *textLayout = new QVBoxLayout(text);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    // Elided, not wrapped: a wrapped row changes its height with the pane's width,
    // and that is what made the inspector flash while the splitter was dragged.
    row.primary = new ElidedLabel(text);
    row.secondary = new ElidedLabel(text);
    row.secondary->setProperty(kCaptionRole, kCaptionValue);
    textLayout->addWidget(row.primary);
    textLayout->addWidget(row.secondary);

    row.trailing = new QLabel(row.widget);

    layout->addWidget(text, 1);
    layout->addWidget(row.trailing, 0, Qt::AlignRight | Qt::AlignVCenter);

    into->addWidget(row.widget);
    rows.append(row);
    return &rows.last();
}

void TaskDetailsPanel::fillRow(Row &row, const QString &primary, const QString &secondary,
                               const QString &trailing, const QColor &tint)
{
    row.primary->setFullText(primary);
    row.secondary->setFullText(secondary);
    if (row.trailing->text() != trailing)
        row.trailing->setText(trailing);
    const QString colour = QStringLiteral("QLabel { color: %1; }").arg(tint.name());
    if (row.trailing->styleSheet() != colour)
        row.trailing->setStyleSheet(colour);
}

void TaskDetailsPanel::hideUnseen(QList<Row> &rows)
{
    for (Row &row : rows) {
        if (!row.seen && row.widget->isVisible())
            row.widget->hide();
        row.seen = false;
    }
}

// ============================================================================
//  the section skeleton: built once, updated in place afterwards
// ============================================================================
void TaskDetailsPanel::buildSkeleton()
{
    // ---- 概要 -------------------------------------------------------------
    QWidget *content = m_sectionContent.value(Overview);
    auto *root = qobject_cast<QVBoxLayout *>(content->layout());

    m_overviewEmpty = new QLabel(tr("未选择任务"), content);
    m_overviewEmpty->setProperty(kCaptionRole, kCaptionValue);
    m_captionLabels << m_overviewEmpty;
    m_captions << QT_TR_NOOP("未选择任务");
    root->insertWidget(root->count() - 1, m_overviewEmpty);

    QVBoxLayout *basic = addCard(root, QT_TR_NOOP("基本信息"));
    m_basicCard = basic->parentWidget();
    m_basicGrid = new QGridLayout();
    m_basicGrid->setHorizontalSpacing(12);
    m_basicGrid->setVerticalSpacing(6);
    m_basicGrid->setColumnStretch(1, 1);
    basic->addLayout(m_basicGrid);

    int row = 0;
    field(QStringLiteral("name"), m_basicGrid, QT_TR_NOOP("名称"));
    m_fieldRows.insert(QStringLiteral("name"), row++);
    field(QStringLiteral("gid"), m_basicGrid, QT_TR_NOOP("GID"));
    m_fieldRows.insert(QStringLiteral("gid"), row++);
    field(QStringLiteral("status"), m_basicGrid, QT_TR_NOOP("状态"));
    m_fieldRows.insert(QStringLiteral("status"), row++);
    field(QStringLiteral("dir"), m_basicGrid, QT_TR_NOOP("保存到"));
    m_fieldRows.insert(QStringLiteral("dir"), row++);
    field(QStringLiteral("type"), m_basicGrid, QT_TR_NOOP("类型"));
    m_fieldRows.insert(QStringLiteral("type"), row++);
    field(QStringLiteral("infohash"), m_basicGrid, QT_TR_NOOP("信息哈希"), true);
    m_fieldRows.insert(QStringLiteral("infohash"), row++);
    field(QStringLiteral("pieces"), m_basicGrid, QT_TR_NOOP("分片"));
    m_fieldRows.insert(QStringLiteral("pieces"), row++);

    for (auto it = m_fieldRows.constBegin(); it != m_fieldRows.constEnd(); ++it) {
        FieldRow &fieldRow = m_fields[it.key()];
        m_basicGrid->addWidget(fieldRow.caption, it.value(), 0);
        m_basicGrid->addWidget(fieldRow.value, it.value(), 1);
        fieldRow.caption->hide();
        fieldRow.value->hide();
    }

    QVBoxLayout *transfer = addCard(root, QT_TR_NOOP("传输"));
    m_transferCard = transfer->parentWidget();
    m_transferGrid = new QGridLayout();
    m_transferGrid->setHorizontalSpacing(12);
    m_transferGrid->setVerticalSpacing(6);
    m_transferGrid->setColumnStretch(1, 1);
    transfer->addLayout(m_transferGrid);

    struct TransferField { const char *id; const char *caption; };
    static const TransferField transferFields[] = {
        {"total", QT_TR_NOOP("总大小")},        {"done", QT_TR_NOOP("已下载")},
        {"uploaded", QT_TR_NOOP("已上传")},     {"downspeed", QT_TR_NOOP("下载速度")},
        {"upspeed", QT_TR_NOOP("上传速度")},    {"avgspeed", QT_TR_NOOP("平均速度")},
        {"eta", QT_TR_NOOP("剩余时间")},        {"connections", QT_TR_NOOP("连接数")},
        {"seeders", QT_TR_NOOP("种子 / 用户")},
    };
    row = 0;
    for (const TransferField &f : transferFields) {
        const QString id = QString::fromLatin1(f.id);
        field(id, m_transferGrid, f.caption);
        FieldRow &fieldRow = m_fields[id];
        m_transferGrid->addWidget(fieldRow.caption, row, 0);
        m_transferGrid->addWidget(fieldRow.value, row, 1);
        fieldRow.caption->hide();
        fieldRow.value->hide();
        ++row;
    }

    QVBoxLayout *error = addCard(root, QT_TR_NOOP("错误信息"));
    m_errorCard = error->parentWidget();
    m_errorLabel = new QLabel(m_errorCard);
    m_errorLabel->setWordWrap(true);
    error->addWidget(m_errorLabel);
    m_errorCard->hide();

    // ---- 连接 -------------------------------------------------------------
    content = m_sectionContent.value(Peers);
    root = qobject_cast<QVBoxLayout *>(content->layout());

    m_peersEmpty = new QLabel(tr("暂无连接的用户（仅 BitTorrent 任务有 Peer 信息）"), content);
    m_peersEmpty->setProperty(kCaptionRole, kCaptionValue);
    m_peersEmpty->setWordWrap(true);
    m_captionLabels << m_peersEmpty;
    m_captions << QT_TR_NOOP("暂无连接的用户（仅 BitTorrent 任务有 Peer 信息）");
    root->insertWidget(root->count() - 1, m_peersEmpty);

    QVBoxLayout *peersCard = addCard(root, QT_TR_NOOP("已连接的用户"));
    m_peersCard = peersCard->parentWidget();
    m_peersBody = peersCard;

    // ---- 服务器 -----------------------------------------------------------
    content = m_sectionContent.value(Servers);
    root = qobject_cast<QVBoxLayout *>(content->layout());

    m_serversEmpty = new QLabel(tr("暂无服务器信息"), content);
    m_serversEmpty->setProperty(kCaptionRole, kCaptionValue);
    m_captionLabels << m_serversEmpty;
    m_captions << QT_TR_NOOP("暂无服务器信息");
    root->insertWidget(root->count() - 1, m_serversEmpty);

    QVBoxLayout *uriCard = addCard(root, QT_TR_NOOP("下载地址"));
    m_uriCard = uriCard->parentWidget();
    m_uriBody = uriCard;

    QVBoxLayout *serverCard = addCard(root, QT_TR_NOOP("服务器 / 镜像"));
    m_serverCard = serverCard->parentWidget();
    m_serverBody = serverCard;

    // ---- 选项 -------------------------------------------------------------
    content = m_sectionContent.value(Options);
    root = qobject_cast<QVBoxLayout *>(content->layout());

    m_optionsEmpty = new QLabel(tr("正在读取任务选项…"), content);
    m_optionsEmpty->setProperty(kCaptionRole, kCaptionValue);
    m_captionLabels << m_optionsEmpty;
    m_captions << QT_TR_NOOP("正在读取任务选项…");
    root->insertWidget(root->count() - 1, m_optionsEmpty);

    QVBoxLayout *optionsCard = addCard(root, QT_TR_NOOP("aria2 选项"));
    m_optionsCard = optionsCard->parentWidget();
    auto *refreshOptions = new FluentButton(tr("刷新选项"), m_optionsCard);
    refreshOptions->setRole(FluentButton::Subtle);
    refreshOptions->setCompact(true);
    connect(refreshOptions, &QPushButton::clicked, this, [this]() {
        if (m_aria2 && !m_gid.isEmpty())
            m_aria2->fetchTaskOptions(m_gid);
    });
    optionsCard->addWidget(refreshOptions);

    m_optionsGrid = new QGridLayout();
    m_optionsGrid->setHorizontalSpacing(14);
    m_optionsGrid->setVerticalSpacing(4);
    m_optionsGrid->setColumnStretch(1, 1);
    optionsCard->addLayout(m_optionsGrid);
}

void TaskDetailsPanel::updateOverview()
{
    const QVariantMap detail = m_aria2 ? m_aria2->taskDetail() : QVariantMap();
    const FluentTheme *t = FluentTheme::instance();
    const bool hasTask = !m_gid.isEmpty() && !detail.isEmpty();

    m_overviewEmpty->setVisible(!hasTask);
    m_basicCard->setVisible(hasTask);
    m_transferCard->setVisible(hasTask);
    if (!hasTask) {
        m_errorCard->hide();
        return;
    }

    const QString status = detail.value(QStringLiteral("status")).toString();
    const bool isTorrent = detail.value(QStringLiteral("isTorrent")).toBool();

    // The rows that always apply. They are created hidden (a field only appears
    // once it has a value), so each update has to say which ones it wants; the
    // three that depend on the task are handled below.
    static const char *const alwaysVisible[] = {
        "name", "gid", "status", "dir", "type",
        "total", "done", "uploaded", "downspeed", "upspeed",
        "avgspeed", "eta", "connections",
    };
    for (const char *id : alwaysVisible)
        setFieldVisible(QString::fromLatin1(id), true);

    setField(QStringLiteral("name"), elide(detail.value(QStringLiteral("fileName")).toString(), 200));
    setField(QStringLiteral("gid"), detail.value(QStringLiteral("gid")).toString());
    setField(QStringLiteral("status"), FluentTheme::statusLabel(status));
    setField(QStringLiteral("dir"), elide(detail.value(QStringLiteral("dir")).toString(), 200));
    setField(QStringLiteral("type"), isTorrent ? tr("BitTorrent 任务") : tr("普通下载"));

    const QString infoHash = detail.value(QStringLiteral("infoHash")).toString();
    setField(QStringLiteral("infohash"), FluentTheme::prettyInfoHash(infoHash));
    setFieldVisible(QStringLiteral("infohash"), !infoHash.isEmpty());

    const int pieceLength = detail.value(QStringLiteral("pieceLength")).toInt();
    setField(QStringLiteral("pieces"),
             tr("%1 片 × %2")
                 .arg(detail.value(QStringLiteral("pieceCount")).toInt())
                 .arg(FluentTheme::formatSize(pieceLength)));
    setFieldVisible(QStringLiteral("pieces"), pieceLength > 0);

    setField(QStringLiteral("total"),
             FluentTheme::formatSize(detail.value(QStringLiteral("totalLength")).toDouble()));
    setField(QStringLiteral("done"),
             FluentTheme::formatSize(detail.value(QStringLiteral("completedLength")).toDouble()));
    setField(QStringLiteral("uploaded"),
             FluentTheme::formatSize(detail.value(QStringLiteral("uploadLength")).toDouble()));
    setField(QStringLiteral("downspeed"),
             FluentTheme::formatSpeed(detail.value(QStringLiteral("downloadSpeed")).toDouble()));
    setField(QStringLiteral("upspeed"),
             FluentTheme::formatSpeed(detail.value(QStringLiteral("uploadSpeed")).toDouble()));
    setField(QStringLiteral("avgspeed"),
             FluentTheme::formatSpeed(detail.value(QStringLiteral("avgSpeed")).toDouble()));
    setField(QStringLiteral("eta"),
             FluentTheme::formatDuration(detail.value(QStringLiteral("eta")).toDouble()));
    setField(QStringLiteral("connections"),
             QString::number(detail.value(QStringLiteral("connections")).toInt()));
    setField(QStringLiteral("seeders"),
             tr("%1 个种子 · %2 个连接")
                 .arg(detail.value(QStringLiteral("numSeeders")).toInt())
                 .arg(detail.value(QStringLiteral("connections")).toInt()));
    setFieldVisible(QStringLiteral("seeders"), isTorrent);

    const QString error = detail.value(QStringLiteral("errorMessage")).toString();
    if (m_errorLabel->text() != error)
        m_errorLabel->setText(error);
    m_errorCard->setVisible(!error.isEmpty());
    m_errorLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->critical().name()));
}

void TaskDetailsPanel::updatePeers()
{
    const QVariantList peers = m_aria2
                                   ? m_aria2->taskDetail().value(QStringLiteral("peers")).toList()
                                   : QVariantList();
    const FluentTheme *t = FluentTheme::instance();

    m_peersCard->setVisible(!peers.isEmpty());
    m_peersEmpty->setVisible(peers.isEmpty());

    for (const QVariant &v : peers) {
        const QVariantMap p = v.toMap();
        const bool seeder = p.value(QStringLiteral("seeder")).toBool();
        const QString key = p.value(QStringLiteral("ip")).toString() + QLatin1Char(':')
                            + p.value(QStringLiteral("port")).toString();
        Row *row = rowFor(m_peerRows, key, m_peersBody);
        row->seen = true;
        row->widget->show();
        fillRow(*row, key,
                tr("上传 %1 · 下载 %2")
                    .arg(FluentTheme::formatSpeed(p.value(QStringLiteral("uploadSpeed")).toDouble()),
                         FluentTheme::formatSpeed(p.value(QStringLiteral("speed")).toDouble())),
                seeder ? tr("做种") : tr("下载"), seeder ? t->success() : t->info());
    }

    hideUnseen(m_peerRows);
}

void TaskDetailsPanel::updateServers()
{
    const QVariantMap detail = m_aria2 ? m_aria2->taskDetail() : QVariantMap();
    const QVariantList servers = detail.value(QStringLiteral("servers")).toList();
    const QVariantList uris = detail.value(QStringLiteral("uris")).toList();
    const FluentTheme *t = FluentTheme::instance();

    m_uriCard->setVisible(!uris.isEmpty());
    m_serverCard->setVisible(!servers.isEmpty());
    m_serversEmpty->setVisible(uris.isEmpty() && servers.isEmpty());

    for (const QVariant &v : uris) {
        const QVariantMap u = v.toMap();
        const QString uri = u.value(QStringLiteral("uri")).toString();
        const QString status = u.value(QStringLiteral("status")).toString();
        Row *row = rowFor(m_uriRows, uri, m_uriBody);
        row->seen = true;
        row->widget->show();
        fillRow(*row, elide(uri, 120), status, status,
                status == QLatin1String("used") ? t->success() : t->textTertiary());
    }
    hideUnseen(m_uriRows);

    for (const QVariant &v : servers) {
        const QVariantMap s = v.toMap();
        const QString key = s.value(QStringLiteral("index")).toString();
        Row *row = rowFor(m_serverRows, key, m_serverBody);
        row->seen = true;
        row->widget->show();
        fillRow(*row, elide(s.value(QStringLiteral("currentUri")).toString(), 120),
                tr("连接 %1").arg(key),
                FluentTheme::formatSpeed(s.value(QStringLiteral("downloadSpeed")).toDouble()),
                t->accent());
    }
    hideUnseen(m_serverRows);
}

void TaskDetailsPanel::updateOptions()
{
    const QVariantMap options = m_aria2
                                    ? m_aria2->taskDetail().value(QStringLiteral("options")).toMap()
                                    : QVariantMap();
    const bool hasTask = !m_gid.isEmpty() && m_aria2 && !m_aria2->taskDetail().isEmpty();

    m_optionsEmpty->setVisible(hasTask && options.isEmpty());
    m_optionsCard->setVisible(hasTask && !options.isEmpty());
    if (!hasTask || options.isEmpty()) {
        // Hide the label pairs, not their parent: parentWidget() is the card
        // itself, and hiding that took the heading down with it.
        for (FieldRow &row : m_optionFields) {
            row.caption->hide();
            row.value->hide();
        }
        if (hasTask && options.isEmpty() && m_aria2 && !m_gid.isEmpty())
            m_aria2->fetchTaskOptions(m_gid);
        return;
    }

    QStringList keys = options.keys();
    keys.sort();

    // The layout is touched only when the *set* of options changes. Re-adding the
    // pairs on every refresh (once a second) invalidated the grid, so the whole
    // list was laid out again every second - which is what made this tab flicker
    // even though the widgets were reused.
    if (keys != m_optionOrder) {
        m_optionOrder = keys;
        int row = 0;
        for (const QString &key : std::as_const(keys)) {
            auto it = m_optionFields.find(key);
            if (it == m_optionFields.end()) {
                FieldRow created;
                created.captionText = nullptr;
                created.caption = new QLabel(key, m_optionsCard);
                created.caption->setProperty(kCaptionRole, kCaptionValue);
                created.caption->setFont(FluentTheme::monoFont());
                created.value = new ElidedLabel(m_optionsCard);
                created.value->setFont(FluentTheme::monoFont());
                created.value->setTextInteractionFlags(Qt::TextSelectableByMouse);
                it = m_optionFields.insert(key, created);
            }
            m_optionsGrid->addWidget(it->caption, row, 0);
            m_optionsGrid->addWidget(it->value, row, 1);
            it->caption->show();
            it->value->show();
            ++row;
        }

        // Options the engine no longer reports (the set changes with the task).
        const QSet<QString> live(keys.constBegin(), keys.constEnd());
        for (auto it = m_optionFields.begin(); it != m_optionFields.end(); ++it) {
            if (live.contains(it.key()))
                continue;
            it->caption->hide();
            it->value->hide();
        }
    }

    for (const QString &key : std::as_const(keys)) {
        auto it = m_optionFields.find(key);
        if (it == m_optionFields.end())
            continue;
        const QString raw = options.value(key).toString();
        const QString text = elide(raw, 160);
        if (it->text == text)
            continue;
        it->text = text;
        it->value->setText(text);
        // The whole value stays reachable: the row elides it to keep the list
        // steady, so the tooltip is where the rest lives.
        it->value->setToolTip(raw);
    }
}

// ============================================================================
void TaskDetailsPanel::setGid(const QString &gid)
{
    m_gid = gid;
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
    // Only the text inside the rows is touched. The widgets were built once (see
    // buildSkeleton) and stay where they are, so a refresh costs a few setText()
    // calls and repaints only the labels whose value actually changed - no
    // flashing, and the scroll position is never disturbed.
    switch (m_section) {
    case Overview: updateOverview(); break;
    case Peers:    updatePeers(); break;
    case Servers:  updateServers(); break;
    case Options:  updateOptions(); break;
    default: break;
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
    static const char *tabCaptions[SectionCount] = {"概要", "连接", "服务器", "选项"};
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

    // The rows are created once, so their captions have to be re-translated here
    // rather than by a rebuild.
    for (int i = 0; i < m_captionLabels.size() && i < m_captions.size(); ++i) {
        if (m_captions.at(i))
            m_captionLabels.at(i)->setText(tr(m_captions.at(i)));
    }
    for (const FieldRow &row : std::as_const(m_fields)) {
        if (row.captionText && row.caption)
            row.caption->setText(tr(row.captionText));
    }
    refresh();
}
