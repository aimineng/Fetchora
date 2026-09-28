#include "ui/pages/AboutPage.h"

#include "Aria2Manager.h"
#include "DownloadHistory.h"
#include "SettingsManager.h"
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
#include <QLocale>
#include <QRegularExpression>
#include <QShowEvent>
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

/// 运行状态行数量：状态 / 版本 / PID / 任务 / 活动队列 / 历史 / 累计下载 /
/// 最新版本 / 更新通道。
const int kFactCount = 9;

/// 检查更新写进运行状态卡片的两行。
const int kFactLatestVersion = 7;
const int kFactUpdateChannel = 8;

/// release notes 摘进 InfoBar 的字符数上限。
const int kSummaryLimit = 300;

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

/**
 * InfoBar 只暴露 set* 系列接口，一次结果要写四个字段，这里收在一处。
 *
 * actionText 为空时 InfoBar 会把动作按钮藏起来，所以不需要单独清理上一次的
 * 「打开发布页面」。
 */
void showNotice(InfoBar *bar, InfoBar::Severity severity, const QString &title,
                const QString &message, const QString &actionText = QString())
{
    if (!bar)
        return;
    bar->setSeverity(severity);
    bar->setTitle(title);
    bar->setMessage(message);
    bar->setActionText(actionText);
    bar->show();
}

/**
 * 把 release notes 压成一行短摘要：去掉 markdown 标记，换行折成空格。
 *
 * GitHub 的正文是多行 markdown，整段塞进 InfoBar 会把页面撑得很难看，所以只留
 * 开头 kSummaryLimit 个字，超出的用省略号收尾。
 *
 * 逐行过滤而不是整段替换：发布说明的开头往往是「下载表格」和安装代码块（本项目的
 * 发布正文就是这样），整段折行后会得到一串 `| Platform | Asset |` 和 `sudo apt
 * install ...`，既不像摘要也读不懂。表格行、代码块与分隔线直接丢掉，剩下的标题、
 * 列表、强调标记再退化成纯文本——InfoBar 里的 QLabel 不认 markdown。
 */
QString summarizeNotes(const QString &notes)
{
    static const QRegularExpression heading(QStringLiteral("^[ \\t]{0,3}#{1,6}[ \\t]*"));
    static const QRegularExpression rule(QStringLiteral("^[ \\t]*([-*_])([ \\t]*\\1){2,}[ \\t]*$"));
    static const QRegularExpression emphasis(QStringLiteral("(\\*\\*|__|\\*|`)"));
    static const QRegularExpression gaps(QStringLiteral(" {2,}"));

    QStringList kept;
    bool inFence = false;
    const QStringList lines = notes.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line.remove(QLatin1Char('\r'));
        // ``` 围起来的代码块整块跳过：安装命令不是摘要。
        if (line.trimmed().startsWith(QLatin1String("```"))) {
            inFence = !inFence;
            continue;
        }
        if (inFence)
            continue;
        const QString trimmed = line.trimmed();
        // 表格行与 <hr> 都没有可读内容。
        if (trimmed.startsWith(QLatin1Char('|')) || rule.match(trimmed).hasMatch())
            continue;
        if (trimmed.isEmpty()) {
            // 空行仍然留一个空格，免得两段被粘成一个词。
            kept << QString();
            continue;
        }
        line.remove(heading);
        line.remove(emphasis);
        kept << line.trimmed();
    }

    QString text = kept.join(QLatin1Char(' '));
    text.replace(gaps, QStringLiteral(" "));
    text = text.trimmed();
    if (text.size() > kSummaryLimit) {
        int cut = kSummaryLimit;
        // 别把一个代理对从中间切断，否则末尾会多出一个替换字符。
        if (text.at(cut - 1).isHighSurrogate())
            --cut;
        text = text.left(cut) + QStringLiteral("…");
    }
    return text;
}

} // namespace

AboutPage::AboutPage(Aria2Manager *aria2, QWidget *parent, SettingsManager *settings)
    : QWidget(parent)
    , ui(new Ui::AboutPage)
    , m_aria2(aria2)
    // 主窗口可以不传设置：引擎自己就挂着一份，两条路都通。
    , m_settings(settings ? settings : (aria2 ? aria2->settings() : nullptr))
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

    m_updates = new UpdateChecker(this);
    wireUpdateChecker();

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

void AboutPage::wireUpdateChecker()
{
    connect(m_updates, &UpdateChecker::checkFinished, this, &AboutPage::onUpdateCheckFinished);
    connect(m_updates, &UpdateChecker::checkFailed, this, &AboutPage::onUpdateCheckFailed);
    connect(m_updates, &UpdateChecker::downloadProgress, this, &AboutPage::onUpdateProgress);
    connect(m_updates, &UpdateChecker::downloadFinished, this, &AboutPage::onUpdateDownloaded);
    connect(m_updates, &UpdateChecker::downloadFailed, this, &AboutPage::onUpdateDownloadFailed);

    // 「打开发布页面」只连一次：每次结果只换动作文案，不重新接线，否则点一下会
    // 打开好几个标签页。动作真正打开的地址在触发时从 m_release 里取。
    connect(m_notice, &InfoBar::actionTriggered, this, &AboutPage::openReleasePage);
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

// ---------------------------------------------------------------------------
//  检查更新
//
//  一个按钮走完「检查 → 可下载 → 正在下载 → 可安装」：状态机在这里，网络与
//  发布列表的解析都在 UpdateChecker 里，本页只把结果翻译成界面。
// ---------------------------------------------------------------------------
void AboutPage::checkForUpdates()
{
    switch (m_updateState) {
    case UpdateState::Idle:
    case UpdateState::UpToDate:
        beginUpdateCheck();
        break;
    case UpdateState::Available:
        beginUpdateDownload();
        break;
    case UpdateState::ReadyToInstall:
        installReadyUpdate();
        break;
    case UpdateState::Checking:
    case UpdateState::Downloading:
        // 这两个状态下按钮是禁用的，正常点不到；真点到了也不该重入。
        break;
    }
}

void AboutPage::beginUpdateCheck()
{
    // 引擎版本、PID 之类的本地事实顺手重读一次，和以前的行为一致。
    refresh();

    m_updateState = UpdateState::Checking;
    m_noticeKind = NoticeKind::Checking;
    updateButtonForState();
    renderNotice();
    // 通道选择来自设置：勾了预发布就把 GitHub 的 pre-release 也算进来。
    m_updates->check(m_settings && m_settings->updateIncludePrerelease());
}

void AboutPage::onUpdateCheckFinished(const UpdateChecker::Release &release, bool updateAvailable)
{
    if (updateAvailable) {
        m_release = release;
        // 记住已经提醒过哪一个版本，下次启动的检查就不会重复打扰用户。
        if (m_settings && m_settings->lastNotifiedVersion() != release.version)
            m_settings->setLastNotifiedVersion(release.version);

        m_updateState = UpdateState::Available;
        m_noticeKind = NoticeKind::Available;
        emit toast(tr("发现新版本 %1").arg(release.version), false);
    } else {
        m_release = UpdateChecker::Release();
        m_updateState = UpdateState::UpToDate;
        m_noticeKind = NoticeKind::UpToDate;
        emit toast(tr("已是最新版本"), false);
    }
    updateButtonForState();
    renderNotice();
    updateUpdateFacts();
}

void AboutPage::onUpdateCheckFailed(const QString &reason)
{
    // 退回可重试的状态；失败原因由 UpdateChecker 给，已经是翻译好的文本，
    // 这里不能再套一层 tr()。
    m_updateState = UpdateState::Idle;
    m_noticeKind = NoticeKind::CheckFailed;
    m_lastError = reason;
    updateButtonForState();
    renderNotice();
    emit toast(reason, true);
    updateUpdateFacts();
}

void AboutPage::beginUpdateDownload()
{
    const QUrl asset = m_release.preferredAsset();
    // 资源地址的最后一段就是文件名，UpdateChecker 用它落到临时目录里。
    const QString fileName = asset.fileName();
    if (!asset.isValid() || fileName.isEmpty()) {
        // 这个平台没有对应产物：不下载，直接把发布页面递到用户手上。
        m_noticeKind = NoticeKind::NoAsset;
        renderNotice();
        emit toast(tr("这个版本没有适用于当前平台的安装包"), true);
        return;
    }

    m_downloadPercent = -1;
    m_downloadName = fileName;
    m_updateState = UpdateState::Downloading;
    m_noticeKind = NoticeKind::Downloading;
    updateButtonForState();
    renderNotice();
    m_updates->download(asset, fileName);
}

void AboutPage::onUpdateProgress(qint64 received, qint64 total)
{
    // 总大小未知时（分块传输）没有百分数可报，按钮保持「正在下载…」。
    if (total <= 0 || m_updateState != UpdateState::Downloading)
        return;
    const int percent = static_cast<int>(received * 100 / total);
    if (percent == m_downloadPercent)
        return;
    m_downloadPercent = percent;
    updateButtonForState();
}

void AboutPage::onUpdateDownloaded(const QString &path)
{
    m_installerPath = path;
    m_downloadPercent = 100;
    m_updateState = UpdateState::ReadyToInstall;
    m_noticeKind = NoticeKind::ReadyToInstall;
    updateButtonForState();
    renderNotice();
    emit toast(tr("安装包已下载完成"), false);
    updateUpdateFacts();
}

void AboutPage::onUpdateDownloadFailed(const QString &reason)
{
    // 回到可下载状态，用户可以直接再点一次重试。
    m_downloadPercent = -1;
    m_updateState = UpdateState::Available;
    m_noticeKind = NoticeKind::DownloadFailed;
    m_lastError = reason;
    updateButtonForState();
    renderNotice();
    emit toast(reason, true);
    updateUpdateFacts();
}

void AboutPage::installReadyUpdate()
{
    if (m_installerPath.isEmpty() || !UpdateChecker::launchInstaller(m_installerPath)) {
        m_noticeKind = NoticeKind::InstallFailed;
        renderNotice();
        emit toast(tr("无法启动安装包"), true);
        return;
    }
#if defined(Q_OS_WIN)
    // 安装程序要替换正在运行的可执行文件，本进程必须先让开。
    QCoreApplication::quit();
#endif
}

void AboutPage::openReleasePage()
{
    // InfoBar 的动作按钮只接了一次，目标地址在触发时才取：先看当前提供的那
    // 个版本，没有就用发布列表页兜底。
    QString page = m_release.pageUrl;
    if (page.isEmpty())
        page = UpdateChecker::releasesPageUrl().toString();
    if (!page.isEmpty())
        UpdateChecker::openInBrowser(QUrl(page));
}

void AboutPage::renderNotice()
{
    switch (m_noticeKind) {
    case NoticeKind::None:
        // 还没出过结果：InfoBar 保持原样（可能压根没显示过）。
        break;
    case NoticeKind::Checking:
        showNotice(m_notice, InfoBar::Warning, tr("正在检查更新…"),
                   tr("正在从 GitHub 获取发布列表，请稍候。"));
        break;
    case NoticeKind::UpToDate:
        showNotice(m_notice, InfoBar::Success, tr("已是最新版本"),
                   tr("当前版本 %1，没有可用的更新。").arg(UpdateChecker::currentVersion()));
        break;
    case NoticeKind::Available:
        showNotice(m_notice, InfoBar::Info, tr("发现新版本 %1").arg(m_release.version),
                   releaseSummary(m_release), tr("打开发布页面"));
        break;
    case NoticeKind::Downloading:
        showNotice(m_notice, InfoBar::Info, tr("正在下载更新"),
                   tr("正在下载 %1，完成后即可安装。").arg(m_downloadName));
        break;
    case NoticeKind::ReadyToInstall:
#if defined(Q_OS_WIN)
        showNotice(m_notice, InfoBar::Success, tr("安装包已下载"),
                   tr("安装包已保存到 %1。点击“重启并安装”完成更新。").arg(m_installerPath));
#else
        // 非 Windows 没有可执行的安装程序，只能把文件交给系统。
        showNotice(m_notice, InfoBar::Success, tr("安装包已下载"),
                   tr("安装包已保存到 %1，已交由系统打开。").arg(m_installerPath));
#endif
        break;
    case NoticeKind::CheckFailed:
        // 原因是 UpdateChecker 给的翻译好的文本，直接显示。
        showNotice(m_notice, InfoBar::Warning, tr("检查更新失败"), m_lastError);
        break;
    case NoticeKind::DownloadFailed:
        showNotice(m_notice, InfoBar::Error, tr("下载更新失败"), m_lastError);
        break;
    case NoticeKind::NoAsset:
        showNotice(m_notice, InfoBar::Warning, tr("这个版本没有适用于当前平台的安装包"),
                   tr("发布页面里可能还有其它文件，你可以手动挑选。"), tr("打开发布页面"));
        break;
    case NoticeKind::InstallFailed:
        showNotice(m_notice, InfoBar::Error, tr("无法启动安装包"),
                   tr("安装包 %1 无法运行，请手动打开或重新下载。").arg(m_installerPath));
        break;
    }

    // 按钮在页面底部，结果却显示在顶部：把那条通知滚进视野，否则点了半天看不到
    // 反馈（Toast 会立刻出现，但转瞬即逝）。
    if (m_notice->isVisible())
        ui->scrollArea->ensureWidgetVisible(m_notice, 0, FluentTheme::spacingL());
}

void AboutPage::updateButtonForState()
{
    if (!m_updateButton)
        return;

    switch (m_updateState) {
    case UpdateState::Idle:
    case UpdateState::UpToDate:
    case UpdateState::Checking:
        m_updateButton->setGlyph(FluentTheme::Glyph::Refresh);
        break;
    case UpdateState::Available:
    case UpdateState::Downloading:
        m_updateButton->setGlyph(FluentTheme::Glyph::Download);
        break;
    case UpdateState::ReadyToInstall:
        m_updateButton->setGlyph(FluentTheme::Glyph::Rocket);
        break;
    }

    switch (m_updateState) {
    case UpdateState::Idle:
    case UpdateState::UpToDate:
        m_updateButton->setLoading(false);
        m_updateButton->setText(tr("检查更新"));
        m_updateButton->setTooltipText(tr("从 GitHub Releases 检查是否有新版本"));
        break;
    case UpdateState::Checking:
        // setLoading() 自己会禁用按钮。
        m_updateButton->setText(tr("正在检查…"));
        m_updateButton->setTooltipText(tr("正在从 GitHub 获取发布列表…"));
        m_updateButton->setLoading(true);
        break;
    case UpdateState::Available:
        m_updateButton->setLoading(false);
        m_updateButton->setText(m_release.isValid()
                                    ? tr("下载并安装 %1").arg(m_release.version)
                                    : tr("下载并安装新版本"));
        m_updateButton->setTooltipText(tr("下载新版本的安装包"));
        break;
    case UpdateState::Downloading:
        m_updateButton->setLoading(false);
        m_updateButton->setText(m_downloadPercent >= 0
                                    ? tr("正在下载 %1%").arg(m_downloadPercent)
                                    : tr("正在下载…"));
        m_updateButton->setTooltipText(tr("正在下载安装包…"));
        m_updateButton->setEnabled(false);
        break;
    case UpdateState::ReadyToInstall:
        m_updateButton->setLoading(false);
        m_updateButton->setText(tr("重启并安装"));
        m_updateButton->setTooltipText(tr("运行已下载的安装包并退出本程序"));
        break;
    }
}

void AboutPage::updateUpdateFacts()
{
    if (m_facts.size() < kFactCount)
        return;

    const UpdateChecker::Release latest = m_updates ? m_updates->latest() : UpdateChecker::Release();
    switch (m_updateState) {
    case UpdateState::Available:
    case UpdateState::Downloading:
    case UpdateState::ReadyToInstall:
        setFact(kFactLatestVersion, m_release.version, QStringLiteral("accent"));
        break;
    case UpdateState::UpToDate:
        setFact(kFactLatestVersion,
                latest.isValid() ? latest.version : UpdateChecker::currentVersion(),
                QStringLiteral("success"));
        break;
    case UpdateState::Idle:
    case UpdateState::Checking:
        setFact(kFactLatestVersion, latest.isValid() ? latest.version : tr("尚未检查"),
                QStringLiteral("secondary"));
        break;
    }

    // 通道是设置里的选项，每次出结果都重读一遍，改完设置回来就是新值。
    const bool prerelease = m_settings && m_settings->updateIncludePrerelease();
    setFact(kFactUpdateChannel, prerelease ? tr("包含预览版") : tr("稳定版"),
            prerelease ? QStringLiteral("accent") : QStringLiteral("secondary"));
}

QString AboutPage::releaseSummary(const UpdateChecker::Release &release) const
{
    // GitHub 给的是 UTC，按规格用系统区域格式原样显示，不做时区换算。
    const QString published = release.publishedAt.isValid()
                                  ? QLocale::system().toString(release.publishedAt, QLocale::ShortFormat)
                                  : tr("未知时间");
    const QString tag = release.tagName.isEmpty() ? release.version : release.tagName;
    const QString notes = summarizeNotes(release.notes);
    if (notes.isEmpty())
        return tr("标签 %1 · 发布于 %2").arg(tag, published);
    return tr("标签 %1 · 发布于 %2 —— %3").arg(tag, published, notes);
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

void AboutPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 每次翻回这一页都重读一遍：更新通道是设置里的选项，可能刚在设置页被改过，
    // 引擎状态也同理。
    refresh();
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

    // ---- 更新 ------------------------------------------------------------
    updateUpdateFacts();
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
                                    tr("活动 / 队列"), tr("历史条目"), tr("累计下载"),
                                    tr("最新版本"), tr("更新通道")};
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
    // 检查更新按钮的文案、图标与提示随状态变化（下载中还带百分数），统一由状态
    // 机重新套用；切换语言后这里也必须走一遍，否则会留下旧语言的文案。
    updateButtonForState();
    // 已经显示出来的那条通知同理：按当时的结果用新语言重建一遍。
    renderNotice();
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
