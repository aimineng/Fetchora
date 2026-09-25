#include "ui/pages/AboutPage.h"

#include "Aria2Manager.h"
#include "DownloadHistory.h"
#include "ui/FluentButton.h"
#include "ui/FluentTheme.h"
#include "ui/FluentWidgets.h"
#include "ui/pages/ui_AboutPage.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace {

/// 本地找不到 LICENSE 文件时打开的 MIT 许可证正文。
const char *kLicenseUrl = "https://opensource.org/license/mit";
const char *kProjectUrl = "https://github.com/aimineng/Fetchora";

/// 主视觉徽标数量：Qt / aria2 / 许可证 / 引擎 PID。
const int kBadgeCount = 4;

/// 功能特性条目数量，retranslate() 里逐条填充。
const int kFeatureCount = 10;

/// 运行状态行数量：状态 / 版本 / PID / 任务 / 活动队列 / 历史 / 累计下载。
const int kFactCount = 7;

/**
 * QLabel 样式串。
 *
 * 字号必须写在样式表里：应用级样式表有一条 `QLabel { font-size: 14px; }`，
 * 它会盖掉 setFont()。字族一律取自 FluentTheme::uiFont()，不手写字体列表。
 */
QString labelStyle(const QColor &color, int pixelSize, int weight = QFont::Normal)
{
    return QStringLiteral("QLabel { color: %1; font-family: \"%2\"; font-size: %3px;"
                          " font-weight: %4; background: transparent; }")
        .arg(color.name(QColor::HexArgb), FluentTheme::uiFont(pixelSize).family())
        .arg(pixelSize)
        .arg(weight);
}

/// 卡片里的一根分隔线（应用级样式表按 fluentRole 上色）。
QFrame *makeDivider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setProperty("fluentRole", "divider");
    line->setFixedHeight(1);
    return line;
}

} // namespace

AboutPage::AboutPage(Aria2Manager *aria2, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::AboutPage)
    , m_aria2(aria2)
{
    ui->setupUi(this);

    // .ui 只描述结构；字号与颜色全部在这里按主题决定。
    ui->appNameLabel->setProperty("fluentRole", "title");
    ui->appNameLabel->setFont(FluentTheme::uiFont(28, QFont::DemiBold));
    ui->versionLabel->setProperty("fluentRole", "caption");
    ui->taglineLabel->setProperty("fluentRole", "body");
    ui->footerLabel->setProperty("fluentRole", "tertiary");
    ui->footerLabel->setAlignment(Qt::AlignCenter);

    buildHero();
    buildBadges();

    m_notice = new InfoBar(this);
    m_notice->setClosable(true);
    ui->noticeLayout->addWidget(m_notice);

    buildCards();
    buildActions();
    wireManager();

    retranslate();
    restyle();
}

AboutPage::~AboutPage()
{
    delete ui;
}

// ============================================================================
//  构建
// ============================================================================
void AboutPage::buildHero()
{
    // 磁贴背景由 C++ 上色，所以两个宿主都需要自己绘制样式表背景。
    ui->heroHost->setAttribute(Qt::WA_StyledBackground, true);
    ui->heroTileHost->setAttribute(Qt::WA_StyledBackground, true);

    m_heroGlyph = new FluentIcon(FluentTheme::Glyph::Download, 34, ui->heroTileHost);
    m_heroGlyph->setFixedSize(72, 72);
    ui->heroTileLayout->addWidget(m_heroGlyph, 0, Qt::AlignCenter);
}

void AboutPage::buildBadges()
{
    for (int i = 0; i < kBadgeCount; ++i) {
        auto *badge = new QLabel(this);
        badge->setFont(FluentTheme::uiFont(12));
        badge->setAlignment(Qt::AlignCenter);
        // 插到尾部 spacer 之前，整行保持居中。
        ui->badgeRow->insertWidget(ui->badgeRow->count() - 1, badge, 0, Qt::AlignCenter);
        m_badges << badge;
    }
}

FluentCard *AboutPage::addCard(const QChar &glyph, QLabel **titleOut)
{
    auto *card = new FluentCard(this);

    // 卡片标题行：小字形 + 标题，正文由调用者继续往 body() 里放。
    auto *head = new QHBoxLayout;
    head->setSpacing(FluentTheme::spacingS());

    auto *icon = new FluentIcon(glyph, 15, card);
    icon->useAccentColor();
    head->addWidget(icon, 0, Qt::AlignVCenter);

    auto *title = new QLabel(card);
    head->addWidget(title, 1, Qt::AlignVCenter);

    card->body()->addLayout(head);
    ui->cardsLayout->addWidget(card);

    m_cards << card;
    m_cardIcons << icon;
    m_cardTitles << title;
    if (titleOut)
        *titleOut = title;
    return card;
}

void AboutPage::buildCards()
{
    // ------------------------------------------------------------ 功能特性
    FluentCard *features = addCard(FluentTheme::Glyph::Rocket, nullptr);
    auto *featureList = new QVBoxLayout;
    featureList->setSpacing(6);
    for (int i = 0; i < kFeatureCount; ++i) {
        auto *item = new QLabel(features);
        item->setWordWrap(true);
        featureList->addWidget(item);
        m_featureItems << item;
    }
    features->body()->addLayout(featureList);

    // ------------------------------------------------------------ 运行状态
    FluentCard *facts = addCard(FluentTheme::Glyph::Server, nullptr);
    for (int i = 0; i < kFactCount; ++i) {
        if (i > 0)
            facts->body()->addWidget(makeDivider(facts));

        auto *row = new QHBoxLayout;
        row->setSpacing(FluentTheme::spacingM());

        auto *dot = new QLabel(facts);
        dot->setFixedSize(8, 8);
        row->addWidget(dot, 0, Qt::AlignVCenter);

        auto *label = new QLabel(facts);
        label->setFixedWidth(120);
        row->addWidget(label, 0, Qt::AlignVCenter);

        auto *value = new QLabel(facts);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row->addWidget(value, 1);

        facts->body()->addLayout(row);
        m_facts << FactRow{label, dot, value};
    }

    // ------------------------------------------------------------ 开源许可
    FluentCard *license = addCard(FluentTheme::Glyph::Shield, nullptr);
    m_licenseBody = new QLabel(license);
    m_licenseBody->setWordWrap(true);
    license->body()->addWidget(m_licenseBody);

    m_licenseHint = new QLabel(license);
    m_licenseHint->setWordWrap(true);
    license->body()->addWidget(m_licenseHint);

    m_licenseButton = new FluentButton(license);
    m_licenseButton->setRole(FluentButton::Hyperlink);
    m_licenseButton->setGlyph(FluentTheme::Glyph::Link);
    auto *licenseRow = new QHBoxLayout;
    licenseRow->addWidget(m_licenseButton, 0, Qt::AlignLeft);
    licenseRow->addStretch(1);
    license->body()->addLayout(licenseRow);
    connect(m_licenseButton, &QPushButton::clicked, this, &AboutPage::openLicense);

    // ---------------------------------------------------------------- 致谢
    FluentCard *credits = addCard(FluentTheme::Glyph::Lightbulb, nullptr);
    for (int i = 0; i < 3; ++i) {
        auto *item = new QLabel(credits);
        item->setWordWrap(true);
        credits->body()->addWidget(item);
        m_creditItems << item;
    }
}

void AboutPage::buildActions()
{
    auto add = [this](const QChar &glyph, FluentButton::Role role) {
        auto *button = new FluentButton(this);
        button->setGlyph(glyph);
        button->setRole(role);
        // 放在尾部 spacer 之前，按钮行保持左对齐。
        ui->actionLayout->insertWidget(ui->actionLayout->count() - 1, button);
        return button;
    };

    m_updateButton = add(FluentTheme::Glyph::Refresh, FluentButton::Accent);
    m_homeButton = add(FluentTheme::Glyph::Globe, FluentButton::Standard);
    m_configButton = add(FluentTheme::Glyph::Folder, FluentButton::Standard);

    connect(m_updateButton, &QPushButton::clicked, this, &AboutPage::checkForUpdates);
    connect(m_homeButton, &QPushButton::clicked, this, &AboutPage::openProjectHome);
    connect(m_configButton, &QPushButton::clicked, this, &AboutPage::openConfigFolder);
}

void AboutPage::wireManager()
{
    if (m_aria2) {
        // 引擎版本、PID、任务数与历史条目都是实时值。
        connect(m_aria2, &Aria2Manager::engineReadyChanged, this, &AboutPage::refresh);
        connect(m_aria2, &Aria2Manager::engineStateChanged, this, &AboutPage::refresh);
        connect(m_aria2, &Aria2Manager::statisticsChanged, this, &AboutPage::refresh);
        if (DownloadHistory *history = m_aria2->history())
            connect(history, &DownloadHistory::changed, this, &AboutPage::refresh);
    }
    connect(FluentTheme::instance(), &FluentTheme::changed, this, &AboutPage::restyle);
}

// ============================================================================
//  行为
// ============================================================================
void AboutPage::openDocument(const QStringList &fileNames, const QString &fallbackUrl,
                             const QString &missingHint)
{
    // 依次在可执行文件目录、当前目录及它们的上级目录里找随程序分发的文档
    // （开发构建里 exe 位于 <源码根>/build/<类型>/，所以要看两级父目录）。
    QStringList roots;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString workDir = QDir::currentPath();
    roots << appDir << appDir + QStringLiteral("/..") << appDir + QStringLiteral("/../..")
          << workDir << workDir + QStringLiteral("/..") << workDir + QStringLiteral("/../..");
    for (const QString &root : std::as_const(roots)) {
        for (const QString &name : fileNames) {
            const QFileInfo info(QDir(root).filePath(name));
            if (info.exists() && info.isFile()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
                return;
            }
        }
    }

    if (!fallbackUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(fallbackUrl));
        return;
    }

    m_notice->setSeverity(InfoBar::Warning);
    m_notice->setTitle(tr("没有找到项目文档"));
    m_notice->setMessage(missingHint);
    m_notice->show();
}

void AboutPage::openProjectHome()
{
    // A README shipped next to the binary wins (it is the local documentation);
    // otherwise fall back to the repository, which is where the project lives.
    openDocument({QStringLiteral("README.md"), QStringLiteral("README.MD"),
                  QStringLiteral("README.txt")},
                 QString::fromLatin1(kProjectUrl),
                 tr("发行目录里没有随程序分发的 README.md，请从源码目录查看项目说明。"));
}

void AboutPage::openLicense()
{
    openDocument({QStringLiteral("LICENSE"), QStringLiteral("LICENSE.txt"),
                  QStringLiteral("LICENSE.md")},
                 QString::fromLatin1(kLicenseUrl),
                 tr("没有找到随程序分发的 LICENSE 文件。"));
}

void AboutPage::openConfigFolder()
{
    // 设置、会话与历史数据库都写在 AppConfigLocation 下。
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void AboutPage::checkForUpdates()
{
    // 本程序没有内置在线更新源，这里只重新读取本机的版本信息。
    refresh();
    const QString engine = (m_aria2 && !m_aria2->engineVersion().isEmpty())
                               ? m_aria2->engineVersion()
                               : tr("未连接");
    m_notice->setSeverity(InfoBar::Info);
    m_notice->setTitle(tr("当前版本 %1").arg(QCoreApplication::applicationVersion()));
    m_notice->setMessage(tr("aria2 引擎：%1。本程序未内置在线更新源，版本信息来自本地构建。")
                             .arg(engine));
    m_notice->show();
}

void AboutPage::setFact(int index, const QString &value, const QString &tone)
{
    if (index < 0 || index >= m_facts.size())
        return;
    m_facts[index].value->setText(value);
    m_facts[index].dot->setProperty("tone", tone);
    applyFactTone(m_facts[index].dot);
}

void AboutPage::applyFactTone(QLabel *dot) const
{
    if (!dot)
        return;
    const FluentTheme *t = FluentTheme::instance();
    const QString tone = dot->property("tone").toString();
    QColor color = t->textTertiary();
    if (tone == QLatin1String("success"))
        color = t->success();
    else if (tone == QLatin1String("caution"))
        color = t->caution();
    else if (tone == QLatin1String("critical"))
        color = t->critical();
    else if (tone == QLatin1String("accent"))
        color = t->accent();
    dot->setStyleSheet(QStringLiteral("QLabel { background: %1; border-radius: 4px; }")
                           .arg(color.name()));
}

// ============================================================================
//  刷新 / 翻译 / 主题
// ============================================================================
void AboutPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        retranslate();
    } else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::EnabledChange) {
        restyle();
    }
}

void AboutPage::refresh()
{
    const QString version = QCoreApplication::applicationVersion();
    ui->versionLabel->setText(version.isEmpty() ? tr("开发版本") : tr("版本 %1").arg(version));

    const bool ready = m_aria2 && m_aria2->engineReady();
    const bool running = m_aria2 && m_aria2->engineRunning();
    const QString engine = m_aria2 ? m_aria2->engineVersion() : QString();
    const int pid = m_aria2 ? m_aria2->enginePid() : 0;
    const QVariantMap stats = m_aria2 ? m_aria2->statistics() : QVariantMap();
    DownloadHistory *history = m_aria2 ? m_aria2->history() : nullptr;

    // ---- 徽标 ------------------------------------------------------------
    if (m_badges.size() == kBadgeCount) {
        m_badges[1]->setText(engine.isEmpty() ? tr("aria2 未连接") : tr("aria2 %1").arg(engine));
        m_badges[3]->setText(pid > 0 ? tr("引擎 PID %1").arg(pid) : tr("引擎未运行"));
    }

    // ---- 运行状态 --------------------------------------------------------
    const QString dim = QStringLiteral("secondary");
    setFact(0, ready ? tr("已连接") : (running ? tr("启动中…") : tr("未连接")),
            ready ? QStringLiteral("success")
                  : (running ? QStringLiteral("caution") : QStringLiteral("critical")));
    setFact(1, engine.isEmpty() ? tr("未知") : engine, dim);
    setFact(2, pid > 0 ? QString::number(pid) : tr("未运行"), dim);
    setFact(3, QString::number(stats.value(QStringLiteral("taskCount")).toInt()), dim);
    setFact(4,
            QStringLiteral("%1 / %2")
                .arg(stats.value(QStringLiteral("activeCount")).toInt())
                .arg(stats.value(QStringLiteral("waitingCount")).toInt()),
            dim);
    setFact(5, QString::number(history ? history->count() : 0), dim);
    setFact(6, FluentTheme::formatSize(stats.value(QStringLiteral("completedLength")).toDouble()),
            dim);
}

void AboutPage::retranslate()
{
    // ---- 主视觉 ----------------------------------------------------------
    ui->taglineLabel->setText(tr("基于 aria2 的多协议下载器"));
    if (m_badges.size() == kBadgeCount) {
        m_badges[0]->setText(tr("Qt 6 · QWidget"));
        m_badges[2]->setText(tr("MIT License"));
    }

    // ---- 卡片标题 --------------------------------------------------------
    const QStringList cardTitles = {tr("功能特性"), tr("运行状态"), tr("开源许可"), tr("致谢")};
    for (int i = 0; i < m_cardTitles.size(); ++i)
        m_cardTitles[i]->setText(cardTitles.value(i));

    // ---- 功能特性 --------------------------------------------------------
    const QStringList features = {
        tr("HTTP / HTTPS / FTP 多协议下载"),
        tr("BitTorrent：DHT、磁力链接与 PEX"),
        tr("Metalink 元链接下载"),
        tr("多线程分片并行下载"),
        tr("断点续传与失败自动重试"),
        tr("全局与单任务实时限速"),
        tr("计划任务：按时段全速 / 涓流"),
        tr("浏览器扩展桥接（HTTP + WebSocket）"),
        tr("SQLite 下载历史：搜索、筛选与统计"),
        tr("内置种子制作与 .torrent 解析"),
    };
    for (int i = 0; i < m_featureItems.size(); ++i)
        m_featureItems[i]->setText(QStringLiteral("•   ") + features.value(i));

    // ---- 运行状态 --------------------------------------------------------
    const QStringList factLabels = {tr("引擎状态"), tr("引擎版本"), tr("进程 PID"), tr("任务总数"),
                                    tr("活动 / 队列"), tr("历史条目"), tr("累计下载")};
    for (int i = 0; i < m_facts.size(); ++i)
        m_facts[i].label->setText(factLabels.value(i));

    // ---- 开源许可 --------------------------------------------------------
    m_licenseBody->setText(tr("本项目以 MIT 许可证开源。"));
    m_licenseHint->setText(tr("你可以自由使用、修改与分发本项目，只需保留版权声明与许可证全文。"));
    m_licenseButton->setText(tr("查看许可证"));
    m_licenseButton->setTooltipText(tr("打开随程序分发的 LICENSE 文件"));

    // ---- 致谢 ------------------------------------------------------------
    const QStringList credits = {
        tr("aria2 — 下载引擎（GNU GPL v2+，版权归其作者所有）"),
        tr("Qt 6 — 跨平台应用框架"),
        tr("Segoe Fluent Icons — 界面图标字体"),
    };
    for (int i = 0; i < m_creditItems.size(); ++i)
        m_creditItems[i]->setText(credits.value(i));

    // ---- 操作按钮 --------------------------------------------------------
    m_updateButton->setText(tr("检查更新"));
    m_updateButton->setTooltipText(tr("重新读取本机版本与 aria2 引擎信息"));
    m_homeButton->setText(tr("打开项目主页"));
    m_homeButton->setTooltipText(tr("打开随程序分发的项目说明文档"));
    m_configButton->setText(tr("打开配置目录"));
    m_configButton->setTooltipText(tr("在资源管理器中打开配置与历史数据库所在目录"));

    refresh();
}

void AboutPage::restyle()
{
    const FluentTheme *t = FluentTheme::instance();
    const QColor accent = t->accent();
    const bool dark = t->isDark();

    // ---- 主视觉：强调色渐变 + 强调色描边 --------------------------------
    ui->heroHost->setStyleSheet(
        QStringLiteral("QWidget#heroHost { background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                       " stop:0 %1, stop:1 %2); border: 1px solid %3; border-radius: %4px; }")
            .arg(QColor(accent.red(), accent.green(), accent.blue(), dark ? 107 : 66)
                     .name(QColor::HexArgb),
                 QColor(accent.red(), accent.green(), accent.blue(), 15).name(QColor::HexArgb),
                 QColor(accent.red(), accent.green(), accent.blue(), 97).name(QColor::HexArgb))
            .arg(FluentTheme::RadiusXLarge));

    ui->heroTileHost->setStyleSheet(
        QStringLiteral("QWidget#heroTileHost { background: %1; border: 1px solid %2;"
                       " border-radius: 16px; }")
            .arg(accent.name(),
                 QColor(255, 255, 255, dark ? 56 : 140).name(QColor::HexArgb)));

    m_heroGlyph->setIconColor(t->onAccent());
    m_heroGlyph->setFixedSize(72, 72);

    // ---- 徽标 ------------------------------------------------------------
    const QString badgeStyle =
        QStringLiteral("QLabel { background: %1; color: %2; border: 1px solid %3;"
                       " border-radius: 11px; padding: 3px 10px; font-family: \"%4\";"
                       " font-size: 12px; }")
            .arg(t->controlFill().name(), t->textSecondary().name(), t->strokeSubtle().name(),
                 FluentTheme::uiFont(12).family());
    for (QLabel *badge : std::as_const(m_badges))
        badge->setStyleSheet(badgeStyle);

    // ---- 卡片 ------------------------------------------------------------
    for (QLabel *title : std::as_const(m_cardTitles))
        title->setStyleSheet(labelStyle(t->textPrimary(), 16, QFont::DemiBold));
    for (FluentIcon *icon : std::as_const(m_cardIcons))
        icon->useAccentColor();
    for (QLabel *item : std::as_const(m_featureItems))
        item->setStyleSheet(labelStyle(t->textSecondary(), 14));
    for (QLabel *item : std::as_const(m_creditItems))
        item->setStyleSheet(labelStyle(t->textSecondary(), 14));
    for (const FactRow &fact : std::as_const(m_facts)) {
        fact.label->setStyleSheet(labelStyle(t->textSecondary(), 14));
        fact.value->setStyleSheet(labelStyle(t->textPrimary(), 14, QFont::DemiBold));
        applyFactTone(fact.dot);
    }
    m_licenseBody->setStyleSheet(labelStyle(t->textPrimary(), 14));
    m_licenseHint->setStyleSheet(labelStyle(t->textSecondary(), 12));

    ui->taglineLabel->setStyleSheet(labelStyle(t->textSecondary(), 14));
}
