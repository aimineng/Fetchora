#include "ui/pages/SettingsPage.h"

#include "Aria2Manager.h"
#include "DownloadHistory.h"
#include "SettingsManager.h"
#include "ui/FluentInputs.h"
#include "ui/FluentTheme.h"
#include "ui/FluentWidgets.h"
#include "ui/LanguageManager.h"
#include "ui/pages/ui_SettingsPage.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextCursor>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

namespace {

/// Section order, mirrored by buildSections(). The enum is what the fields of
/// the RPC / engine section are tagged with: everything in it is read by aria2
/// exactly once, when the process starts, so editing it needs a restart.
enum SectionIndex {
    SectionGeneral = 0,
    SectionDownload,
    SectionConnection,
    SectionProxy,
    SectionTorrent,
    SectionEngine,
    SectionNotify,
    SectionHistory,
    SectionAdvanced,
    SectionAbout,
    SectionCount
};

/// The tint of the selected rail row. FluentButton paints itself from its role
/// and ignores style sheets, so the tint lives on a wrapper widget that does
/// honour one; the button keeps its Subtle role either way.
QString railRowStyle(const FluentTheme *t, bool active, const QString &rowName)
{
    const QColor accent = t->accent();
    const QColor tint(accent.red(), accent.green(), accent.blue(), t->isDark() ? 56 : 36);
    if (!active) {
        return QStringLiteral("QWidget#%1 { background: transparent; border: none;"
                              " border-left: 3px solid transparent; }")
            .arg(rowName);
    }
    return QStringLiteral("QWidget#%1 { background: %2; border: none;"
                          " border-left: 3px solid %3; border-radius: %4px; }")
        .arg(rowName, tint.name(QColor::HexArgb), accent.name(),
             QString::number(FluentTheme::RadiusMedium));
}

/// A scroll area must not paint its own viewport background over the page.
QString scrollAreaStyle()
{
    return QStringLiteral("QScrollArea { background: transparent; border: none; }"
                          "QScrollArea > QWidget > QWidget { background: transparent; }");
}

QFont monospaceFont()
{
    QFont font = FluentTheme::uiFont(12);
    // A single family - never a fallback list: FluentTheme resolves it once.
    font.setFamily(FluentTheme::monoFont());
    return font;
}

} // namespace

// ============================================================================
//  Construction
// ============================================================================
SettingsPage::SettingsPage(SettingsManager *settings, Aria2Manager *aria2, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsPage)
    , m_settings(settings)
    , m_aria2(aria2)
{
    ui->setupUi(this);

    ui->pageTitle->setProperty("fluentRole", "title");
    ui->pageSubtitle->setProperty("fluentRole", "caption");
    ui->railHost->setStyleSheet(QStringLiteral("QWidget#railHost { background: transparent; }"));
    ui->railScroll->setStyleSheet(scrollAreaStyle());
    ui->bodyLayout->setStretchFactor(ui->sectionStack, 1);

    buildHeader();
    buildSections();

    connect(ui->sectionStack, &QStackedWidget::currentChanged, this, [this](int) { restyle(); });
    connect(FluentTheme::instance(), &FluentTheme::changed, this, &SettingsPage::restyle);

    if (m_aria2) {
        connect(m_aria2, &Aria2Manager::engineLogChanged, this, &SettingsPage::refreshLogs);
        connect(m_aria2, &Aria2Manager::rpcLogChanged, this, &SettingsPage::refreshLogs);
        connect(m_aria2, &Aria2Manager::bridgeClientsChanged, this, &SettingsPage::refreshBridgeInfo);
        connect(m_aria2, &Aria2Manager::engineReadyChanged, this, &SettingsPage::refreshBridgeInfo);
    }

    refreshLogs();
    refreshBridgeInfo();
    refreshRestartBar();
    refreshSubtitle();
    setSection(SectionGeneral);
}

SettingsPage::~SettingsPage()
{
    delete ui;
}

// ============================================================================
//  Header
// ============================================================================
void SettingsPage::buildHeader()
{
    // Raised when a setting that aria2 only reads at startup was edited.
    m_restartButton = new FluentButton(this);
    m_restartButton->setRole(FluentButton::Accent);
    m_restartButton->setGlyph(FluentTheme::Glyph::Refresh);
    m_restartButton->setText(tr("重启引擎以生效"));
    m_restartButton->setTooltipText(tr("按新的设置重新启动 aria2 引擎"));
    m_restartButton->hide();
    ui->headerActionsLayout->addWidget(m_restartButton, 0, Qt::AlignVCenter);
    m_actionLabels.append(qMakePair(m_restartButton, QT_TR_NOOP("重启引擎以生效")));
    connect(m_restartButton, &QPushButton::clicked, this, &SettingsPage::restartEngine);

    // Search box at the top of the rail: filters every section by caption.
    auto *railSearch = new QHBoxLayout();
    railSearch->setSpacing(FluentTheme::spacingS());
    railSearch->setContentsMargins(0, 0, 0, 0);

    m_search = new FluentLineEdit(ui->railHost);
    m_search->setHeader(tr("搜索设置"));
    m_search->setFieldGlyph(FluentTheme::Glyph::Search);
    // The rail is 200px wide: keep the field shrinkable so it never pushes the
    // clear button out of the rail.
    m_search->setMinimumWidth(120);
    railSearch->addWidget(m_search, 1);

    m_clearSearch = new FluentButton(ui->railHost);
    m_clearSearch->setRole(FluentButton::Subtle);
    m_clearSearch->setGlyph(FluentTheme::Glyph::Close);
    m_clearSearch->setCompact(true);
    m_clearSearch->setIconOnly(true);
    m_clearSearch->setTooltipText(tr("清除搜索"));
    railSearch->addWidget(m_clearSearch, 0, Qt::AlignBottom);

    ui->railSearchLayout->addLayout(railSearch);
    connect(m_search, &QLineEdit::textChanged, this, &SettingsPage::applyFilter);
    connect(m_clearSearch, &QPushButton::clicked, this, [this]() { m_search->clear(); });
}

// ============================================================================
//  Sections
// ============================================================================
void SettingsPage::buildSections()
{
    // Combo captions, as QT_TR_NOOP sources. They are declared inside this
    // member function on purpose: that is the context lupdate records them
    // under, and it is the context tr() looks them up with at runtime.
    static const char *const kLanguageLabels[] = {
        QT_TR_NOOP("跟随系统"), QT_TR_NOOP("简体中文"), QT_TR_NOOP("English")};
    static const char *const kThemeLabels[] = {
        QT_TR_NOOP("深色"), QT_TR_NOOP("浅色"), QT_TR_NOOP("跟随系统")};
    static const char *const kFileAllocationLabels[] = {
        QT_TR_NOOP("不预分配"), QT_TR_NOOP("预分配"), QT_TR_NOOP("快速预分配")};
    static const char *const kProxyModeLabels[] = {
        QT_TR_NOOP("不使用代理"), QT_TR_NOOP("跟随系统代理"), QT_TR_NOOP("自定义代理")};

    // ====================================================== 0. 常规 / General
    QVBoxLayout *page = addSectionPage(QT_TR_NOOP("常规"),
                                       QT_TR_NOOP("界面语言、外观与窗口行为"),
                                       FluentTheme::Glyph::Settings);
    QVBoxLayout *body = addGroupCard(page, QT_TR_NOOP("外观"));

    FluentComboBox *language = addCombo(body, QStringLiteral("language"), QT_TR_NOOP("语言"),
                                        kLanguageLabels,
                                        {QStringLiteral("system"), QStringLiteral("zh"),
                                         QStringLiteral("en")});
    connect(language, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, language](int) {
                // The translator is the one thing that has to react on the spot.
                const QString code = language->currentData().toString();
                LanguageManager::instance()->apply(code);
                emit toast(tr("界面语言已切换为 %1").arg(LanguageManager::displayName(code)), false);
            });

    addCombo(body, QStringLiteral("theme"), QT_TR_NOOP("主题"), kThemeLabels,
             {QStringLiteral("dark"), QStringLiteral("light"), QStringLiteral("system")});

    // ---- 界面字体 --------------------------------------------------------
    // The family list is whatever this machine has installed, so it cannot be a
    // QT_TR_NOOP table; the first entry restores the platform default.
    auto *fontCombo = new FluentComboBox(body->parentWidget());
    fontCombo->setHeader(tr("界面字体"));
    fontCombo->addItem(tr("跟随系统"), QString());
    const QStringList families = QFontDatabase::families();
    for (const QString &family : families)
        fontCombo->addItem(family, family);
    {
        const QString current = m_settings->uiFontFamily();
        const int index = current.isEmpty() ? 0 : fontCombo->findData(current);
        fontCombo->setCurrentIndex(index < 0 ? 0 : index);
    }
    body->addWidget(fontCombo);

    {
        Field field;
        field.kind = Field::Choice;
        field.widget = fontCombo;
        field.row = fontCombo;
        field.group = groupOf(body);
        field.key = QStringLiteral("uiFontFamily");
        field.title = QT_TR_NOOP("界面字体");
        field.section = m_buildSection;
        field.labels.append(nullptr);
        field.values = QStringList{QString()};
        field.keepItems = true;
        m_fields.append(field);
    }
    connect(fontCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, fontCombo](int) {
                const QString family = fontCombo->currentData().toString();
                m_settings->setUiFontFamily(family);
                FluentTheme::setUiFontFamily(family);
                emit toast(family.isEmpty() ? tr("已恢复系统默认字体")
                                            : tr("界面字体已切换为 %1").arg(family),
                           false);
            });

    auto *fontSize = new FluentSpinBox(body->parentWidget());
    fontSize->setHeader(tr("字体大小"));
    fontSize->setDescription(tr("界面基准字号，图标与标题会按比例缩放"));
    fontSize->setRange(11, 20);
    // The separating space lives in the code, not in the translatable string: a
    // leading space would be trimmed away by the .ts tooling.
    fontSize->setSuffix(QStringLiteral(" ") + tr("像素"));
    fontSize->setValue(m_settings->uiFontSize());
    body->addWidget(fontSize);
    {
        Field field;
        field.kind = Field::Number;
        field.widget = fontSize;
        field.row = fontSize;
        field.group = groupOf(body);
        field.key = QStringLiteral("uiFontSize");
        field.title = QT_TR_NOOP("字体大小");
        field.description = QT_TR_NOOP("界面基准字号，图标与标题会按比例缩放");
        field.section = m_buildSection;
        m_fields.append(field);
    }
    connect(fontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_settings->setUiFontSize(value);
        FluentTheme::setUiFontSize(value);
    });

    FieldSlot accentSlot = addInlineRow(body);
    FluentLineEdit *accent = addLineEdit(accentSlot.field, QStringLiteral("accentColor"),
                                         QT_TR_NOOP("强调色"),
                                         QT_TR_NOOP("十六进制颜色，例如 #0F6CBD；留空时跟随 Windows 强调色"));
    FluentButton *systemAccent =
        addTrailingAction(accentSlot.trailing, QT_TR_NOOP("使用系统强调色"),
                          FluentButton::Standard, FluentTheme::Glyph::Palette);
    connect(systemAccent, &QPushButton::clicked, this, [this, accent]() {
        const QString hex = FluentTheme::instance()->accent().name();
        accent->setText(hex);
        storeValue(QStringLiteral("accentColor"), hex, SectionGeneral);
        FluentTheme::instance()->refresh();
        emit toast(tr("已使用系统强调色 %1").arg(hex), false);
    });
    connect(accent, &QLineEdit::editingFinished, this, [this, accent]() {
        // Committed on focus loss so typing "#0F" is not reported as an error.
        const QString hex = accent->text().trimmed();
        if (hex.isEmpty() || QColor::isValidColorName(hex))
            return;
        const QString fallback = FluentTheme::instance()->accent().name();
        accent->setText(fallback);
        storeValue(QStringLiteral("accentColor"), fallback, SectionGeneral);
        emit toast(tr("“%1”不是有效的颜色值，已恢复为系统强调色").arg(hex), true);
    });

    FluentSwitch *mica = addSwitch(body, QStringLiteral("useMica"), QT_TR_NOOP("Mica 材质"),
                                   QT_TR_NOOP("使用 Windows 11 的云母背景，窗口更贴合桌面"));
    connect(mica, &QAbstractButton::toggled, this, [](bool) {
        // A refresh is what re-applies the native backdrop in the shell.
        FluentTheme::instance()->refresh();
    });
    addSwitch(body, QStringLiteral("enableAnimations"), QT_TR_NOOP("动画效果"),
              QT_TR_NOOP("控件状态切换时的过渡动画"));

    body = addGroupCard(page, QT_TR_NOOP("启动与窗口"));
    addSwitch(body, QStringLiteral("autoStart"), QT_TR_NOOP("开机自启"),
              QT_TR_NOOP("登录 Windows 后自动启动 Fetchora"));
    addSwitch(body, QStringLiteral("startMinimized"), QT_TR_NOOP("启动时最小化"),
              QT_TR_NOOP("启动后直接隐藏到托盘，不弹出主窗口"));
    addSwitch(body, QStringLiteral("minimizeToTray"), QT_TR_NOOP("最小化到托盘"),
              QT_TR_NOOP("最小化窗口时隐藏到系统托盘"));
    addSwitch(body, QStringLiteral("closeToTray"), QT_TR_NOOP("关闭时最小化到托盘"),
              QT_TR_NOOP("关闭窗口后继续在后台运行并保持下载"));
    addSwitch(body, QStringLiteral("confirmOnExit"), QT_TR_NOOP("退出前确认"),
              QT_TR_NOOP("退出程序前弹出确认提示"));
    addSwitch(body, QStringLiteral("showDetailsPanel"), QT_TR_NOOP("显示详情面板"),
              QT_TR_NOOP("在下载页右侧显示所选任务的详细信息"));
    page->addStretch(1);

    // ==================================================== 1. 下载 / Downloads
    page = addSectionPage(QT_TR_NOOP("下载"), QT_TR_NOOP("保存位置、并发数与速度限制"),
                          FluentTheme::Glyph::Download);

    body = addGroupCard(page, QT_TR_NOOP("保存位置"));
    FieldSlot dirSlot = addInlineRow(body);
    FluentLineEdit *dirEdit =
        addLineEdit(dirSlot.field, QStringLiteral("downloadDir"), QT_TR_NOOP("默认下载目录"),
                    QT_TR_NOOP("新任务保存文件的位置"));
    FluentButton *browseDir = addTrailingAction(dirSlot.trailing, QT_TR_NOOP("浏览…"),
                                               FluentButton::Standard, FluentTheme::Glyph::Folder);
    connect(browseDir, &QPushButton::clicked, this, [this, dirEdit]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("选择下载目录"), dirEdit->text());
        if (dir.isEmpty())
            return;
        dirEdit->setText(dir);
        storeValue(QStringLiteral("downloadDir"), dir, SectionDownload);
    });

    body = addGroupCard(page, QT_TR_NOOP("并发与分片"));
    addSpinBox(body, QStringLiteral("maxConcurrentDownloads"), QT_TR_NOOP("最大同时下载数"),
               QT_TR_NOOP("同时进行的任务数量（-j）"), 1, 16);
    addSpinBox(body, QStringLiteral("split"), QT_TR_NOOP("单任务分片数"),
               QT_TR_NOOP("单个任务使用的连接数（-s）"), 1, 16);
    addSpinBox(body, QStringLiteral("maxConnectionPerServer"), QT_TR_NOOP("每服务器连接数"),
               QT_TR_NOOP("对同一台服务器使用的最大连接数（-x）"), 1, 16);
    addLineEdit(body, QStringLiteral("minSplitSize"), QT_TR_NOOP("最小分片大小"),
                QT_TR_NOOP("小于该体积的文件不再分片，支持 K / M 后缀"));

    body = addGroupCard(page, QT_TR_NOOP("下载行为"));
    addSwitch(body, QStringLiteral("continueDownload"), QT_TR_NOOP("继续未完成的下载"),
              QT_TR_NOOP("启动时恢复上一次未完成的任务"));
    addSwitch(body, QStringLiteral("alwaysResume"), QT_TR_NOOP("断点续传"),
              QT_TR_NOOP("即使服务器不支持 Range 请求也继续下载剩余部分"));
    addSwitch(body, QStringLiteral("autoRename"), QT_TR_NOOP("自动重命名"),
              QT_TR_NOOP("同名文件存在时自动改名，不覆盖已有文件"));
    addSwitch(body, QStringLiteral("allowOverwrite"), QT_TR_NOOP("允许覆盖"),
              QT_TR_NOOP("允许覆盖已存在的同名文件"));
    addSwitch(body, QStringLiteral("remoteTime"), QT_TR_NOOP("使用远程时间"),
              QT_TR_NOOP("使用服务器上的修改时间而不是本地时间"));
    addCombo(body, QStringLiteral("fileAllocation"), QT_TR_NOOP("文件预分配"),
             kFileAllocationLabels,
             {QStringLiteral("none"), QStringLiteral("prealloc"), QStringLiteral("falloc")});

    body = addGroupCard(page, QT_TR_NOOP("速度与缓存"));
    addLineEdit(body, QStringLiteral("maxDownloadLimit"), QT_TR_NOOP("下载限速"),
                QT_TR_NOOP("单个任务的下载速度上限，例如 512K；留空为不限速"));
    addLineEdit(body, QStringLiteral("maxUploadLimit"), QT_TR_NOOP("上传限速"),
                QT_TR_NOOP("单个任务的上传速度上限，例如 128K；留空为不限速"));
    addSpinBox(body, QStringLiteral("maxOverallDownloadLimitKB"), QT_TR_NOOP("全局下载限速"),
               QT_TR_NOOP("所有任务合计的下载上限，0 表示不限速"), 0, 1048576, tr("KB/s"));
    addSpinBox(body, QStringLiteral("maxOverallUploadLimitKB"), QT_TR_NOOP("全局上传限速"),
               QT_TR_NOOP("所有任务合计的上传上限，0 表示不限速"), 0, 1048576, tr("KB/s"));
    addLineEdit(body, QStringLiteral("diskCache"), QT_TR_NOOP("磁盘缓存"),
                QT_TR_NOOP("aria2 用于写盘的内存缓存，例如 64M；0 表示禁用"));
    page->addStretch(1);

    // ================================================== 2. 连接 / Connection
    page = addSectionPage(QT_TR_NOOP("连接"), QT_TR_NOOP("请求标识、超时重试与 HTTP 行为"),
                          FluentTheme::Glyph::Globe);

    body = addGroupCard(page, QT_TR_NOOP("请求标识"));
    addLineEdit(body, QStringLiteral("userAgent"), QT_TR_NOOP("用户代理"),
                QT_TR_NOOP("发送给服务器的 User-Agent；留空使用 aria2 默认值"));
    addLineEdit(body, QStringLiteral("referer"), QT_TR_NOOP("引用页"),
                QT_TR_NOOP("请求中携带的 Referer，可用于绕过防盗链"));

    body = addGroupCard(page, QT_TR_NOOP("超时与重试"));
    addSpinBox(body, QStringLiteral("connectTimeout"), QT_TR_NOOP("连接超时"),
               QT_TR_NOOP("建立连接的最长等待时间"), 1, 600, tr("秒"));
    addSpinBox(body, QStringLiteral("socketTimeout"), QT_TR_NOOP("传输超时"),
               QT_TR_NOOP("两次数据传输之间的最长间隔"), 1, 600, tr("秒"));
    addSpinBox(body, QStringLiteral("timeout"), QT_TR_NOOP("超时"),
               QT_TR_NOOP("整个请求的最长等待时间"), 1, 86400, tr("秒"));
    addSpinBox(body, QStringLiteral("maxTries"), QT_TR_NOOP("最大重试次数"),
               QT_TR_NOOP("连接失败后的重试次数，0 表示不重试"), 0, 100, tr("次"));
    addSpinBox(body, QStringLiteral("retryWait"), QT_TR_NOOP("重试间隔"),
               QT_TR_NOOP("两次重试之间的等待时间"), 0, 600, tr("秒"));
    addSpinBox(body, QStringLiteral("lowestSpeedLimit"), QT_TR_NOOP("最低速度限制"),
               QT_TR_NOOP("速度低于该值时放弃任务，0 表示不限制"), 0, 1048576, tr("KB/s"));

    body = addGroupCard(page, QT_TR_NOOP("HTTP"));
    addSwitch(body, QStringLiteral("enableHttpKeepAlive"), QT_TR_NOOP("HTTP Keep-Alive"),
              QT_TR_NOOP("复用连接以减少握手开销"));
    addSwitch(body, QStringLiteral("enableHttpPipelining"), QT_TR_NOOP("HTTP 管线化"),
              QT_TR_NOOP("在同一条连接上连续发送多个请求"));
    addSwitch(body, QStringLiteral("conditionalGet"), QT_TR_NOOP("条件 GET"),
              QT_TR_NOOP("本地文件较新时跳过下载（If-Modified-Since）"));
    addSwitch(body, QStringLiteral("useHead"), QT_TR_NOOP("使用 HEAD"),
              QT_TR_NOOP("先用 HEAD 请求确认文件信息再开始下载"));
    addSwitch(body, QStringLiteral("noWantDigestHeader"), QT_TR_NOOP("不发送 Want-Digest"),
              QT_TR_NOOP("不在请求中要求服务器返回摘要信息"));
    addSwitch(body, QStringLiteral("streamPieceSelector"), QT_TR_NOOP("流式分片选择"),
              QT_TR_NOOP("按顺序下载分片，媒体文件可以边下边看"));
    addSwitch(body, QStringLiteral("parameterizedUri"), QT_TR_NOOP("参数化 URI"),
              QT_TR_NOOP("把重复的 URI 参数提取成模板后再发送"));
    addSwitch(body, QStringLiteral("autoFileRenaming"), QT_TR_NOOP("自动重命名文件"),
              QT_TR_NOOP("服务端文件名冲突时自动追加序号"));
    page->addStretch(1);

    // ======================================================= 3. 代理 / Proxy
    page = addSectionPage(QT_TR_NOOP("代理"), QT_TR_NOOP("代理模式、服务器与身份验证"),
                          FluentTheme::Glyph::Shield);

    body = addGroupCard(page, QT_TR_NOOP("模式"));
    addCombo(body, QStringLiteral("proxyMode"), QT_TR_NOOP("代理模式"), kProxyModeLabels,
             {QStringLiteral("none"), QStringLiteral("system"), QStringLiteral("custom")});
    addSwitch(body, QStringLiteral("noProxy"), QT_TR_NOOP("忽略代理"),
              QT_TR_NOOP("完全禁用代理，直连所有服务器"));

    body = addGroupCard(page, QT_TR_NOOP("代理服务器"));
    addLineEdit(body, QStringLiteral("allProxy"), QT_TR_NOOP("全部代理"),
                QT_TR_NOOP("对所有协议生效的代理，例如 http://127.0.0.1:8080"));
    addLineEdit(body, QStringLiteral("httpProxy"), QT_TR_NOOP("HTTP 代理"),
                QT_TR_NOOP("仅用于 HTTP 请求的代理地址"));
    addLineEdit(body, QStringLiteral("httpsProxy"), QT_TR_NOOP("HTTPS 代理"),
                QT_TR_NOOP("仅用于 HTTPS 请求的代理地址"));
    addLineEdit(body, QStringLiteral("ftpProxy"), QT_TR_NOOP("FTP 代理"),
                QT_TR_NOOP("仅用于 FTP 请求的代理地址"));

    body = addGroupCard(page, QT_TR_NOOP("身份验证"));
    addLineEdit(body, QStringLiteral("allProxyUser"), QT_TR_NOOP("代理用户名"),
                QT_TR_NOOP("需要身份验证的代理所使用的用户名"));
    FluentLineEdit *passwd =
        addLineEdit(body, QStringLiteral("allProxyPasswd"), QT_TR_NOOP("代理密码"),
                    QT_TR_NOOP("需要身份验证的代理所使用的密码"));
    // Masks the field and adds the reveal toggle on the trailing edge.
    passwd->setRevealButton(true);

    body = addGroupCard(page, QT_TR_NOOP("例外"));
    addLineEdit(body, QStringLiteral("noProxyList"), QT_TR_NOOP("不使用代理的地址"),
                QT_TR_NOOP("以逗号分隔的主机名，例如 localhost,127.0.0.1"));
    page->addStretch(1);

    // ============================================== 4. BitTorrent / 种子
    page = addSectionPage(QT_TR_NOOP("BitTorrent"), QT_TR_NOOP("监听端口、做种与 Tracker"),
                          FluentTheme::Glyph::Torrent);

    body = addGroupCard(page, QT_TR_NOOP("网络"));
    addSpinBox(body, QStringLiteral("btListenPort"), QT_TR_NOOP("监听端口"),
               QT_TR_NOOP("接受 BT 与 DHT 连接的端口，需要在防火墙中放行"), 0, 65535);
    addSpinBox(body, QStringLiteral("dhtListenPort"), QT_TR_NOOP("DHT 监听端口"),
               QT_TR_NOOP("DHT（UDP）使用的端口"), 0, 65535);
    addLineEdit(body, QStringLiteral("btExternalIp"), QT_TR_NOOP("外部 IP"),
                QT_TR_NOOP("对外公布的 IP 地址，用于 NAT 后的做种"));
    addSwitch(body, QStringLiteral("enableDht"), QT_TR_NOOP("启用 DHT"),
              QT_TR_NOOP("通过分布式哈希表寻找 Peer，无需 Tracker"));
    addSwitch(body, QStringLiteral("enableDht6"), QT_TR_NOOP("启用 DHT6"),
              QT_TR_NOOP("在 IPv6 网络上启用 DHT"));
    addSwitch(body, QStringLiteral("enableLpd"), QT_TR_NOOP("启用 LPD"),
              QT_TR_NOOP("通过本地网络发现同一网段内的 Peer"));

    body = addGroupCard(page, QT_TR_NOOP("加密与元数据"));
    addSwitch(body, QStringLiteral("btRequireCrypto"), QT_TR_NOOP("需要加密"),
              QT_TR_NOOP("只接受加密连接，拒绝明文握手"));
    addSwitch(body, QStringLiteral("btSaveMetadata"), QT_TR_NOOP("保存元数据"),
              QT_TR_NOOP("把磁力链接解析到的元数据保存到磁盘"));
    addSwitch(body, QStringLiteral("btLoadSavedMetadata"), QT_TR_NOOP("加载已保存的元数据"),
              QT_TR_NOOP("添加磁力链接时优先使用本地已保存的元数据"));
    addLineEdit(body, QStringLiteral("btSaveMetadataFile"), QT_TR_NOOP("保存元数据到"),
                QT_TR_NOOP("元数据文件的完整路径，留空使用默认位置"));
    addSwitch(body, QStringLiteral("btEnableHookAfterCheck"), QT_TR_NOOP("校验后执行钩子"),
              QT_TR_NOOP("哈希校验完成后执行 --on-bt-download-complete 钩子"));

    body = addGroupCard(page, QT_TR_NOOP("做种"));
    FluentSpinBox *seedRatio =
        addSpinBox(body, QStringLiteral("seedRatio"), QT_TR_NOOP("做种比率"),
                   QT_TR_NOOP("以 0.1 为单位，10 表示 1.0 倍分享率；0 表示不限制"), 0, 1000);
    {
        // The key holds a double, the spin box edits tenths of it.
        const QSignalBlocker blocker(seedRatio);
        seedRatio->setValue(qRound(readValue(QStringLiteral("seedRatio"), 1.0).toDouble() * 10.0));
    }
    connect(seedRatio, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, seedRatio](int) {
        // Runs after the generic connection installed by addSpinBox(), which
        // stored the raw tenths; connections fire in the order they were made.
        storeValue(QStringLiteral("seedRatio"), seedRatio->value() / 10.0, SectionTorrent);
    });
    addSpinBox(body, QStringLiteral("seedTime"), QT_TR_NOOP("做种时间"),
               QT_TR_NOOP("下载完成后的做种分钟数，0 表示不限制"), 0, 100000, tr("分钟"));
    addSwitch(body, QStringLiteral("seedUnverified"), QT_TR_NOOP("种子未验证即做种"),
              QT_TR_NOOP("哈希校验尚未完成时就开始上传"));
    addSwitch(body, QStringLiteral("btDetachSeedOnly"), QT_TR_NOOP("分离只做种任务"),
              QT_TR_NOOP("做种任务不再占用并发下载的名额"));
    addSwitch(body, QStringLiteral("btRemoveUnselectedFile"), QT_TR_NOOP("移除未选择的文件"),
              QT_TR_NOOP("下载完成后删除没有被选中的文件"));
    addSwitch(body, QStringLiteral("followTorrent"), QT_TR_NOOP("跟随 Torrent"),
              QT_TR_NOOP("磁力链接解析完成后自动开始下载"));

    body = addGroupCard(page, QT_TR_NOOP("连接限制"));
    addSpinBox(body, QStringLiteral("btMaxPeers"), QT_TR_NOOP("最大 Peer 数"),
               QT_TR_NOOP("单个种子允许连接的最大 Peer 数量"), 0, 10000);
    addSpinBox(body, QStringLiteral("btMaxOpenFiles"), QT_TR_NOOP("最大打开文件数"),
               QT_TR_NOOP("同时保持打开的文件句柄上限"), 1, 100000);
    addSpinBox(body, QStringLiteral("btRequestTimeout"), QT_TR_NOOP("请求超时"),
               QT_TR_NOOP("向 Peer 请求分片的等待时间"), 1, 600, tr("秒"));
    addSpinBox(body, QStringLiteral("btStopTimeout"), QT_TR_NOOP("停止超时"),
               QT_TR_NOOP("停止种子时的等待时间，0 表示立即停止"), 0, 600, tr("秒"));
    addSpinBox(body, QStringLiteral("btMetadataTimeout"), QT_TR_NOOP("元数据超时"),
               QT_TR_NOOP("等待磁力链接元数据的最长时间"), 1, 3600, tr("秒"));
    addSpinBox(body, QStringLiteral("btTrackerInterval"), QT_TR_NOOP("Tracker 间隔"),
               QT_TR_NOOP("向 Tracker 汇报的间隔，0 表示由服务器决定"), 0, 86400, tr("秒"));
    addSpinBox(body, QStringLiteral("btTrackerTimeout"), QT_TR_NOOP("Tracker 超时"),
               QT_TR_NOOP("等待 Tracker 响应的最长时间"), 1, 3600, tr("秒"));
    addSpinBox(body, QStringLiteral("btTimeout"), QT_TR_NOOP("BT 超时"),
               QT_TR_NOOP("等待 BT 下载数据的最长时间，0 表示不限制"), 0, 86400, tr("秒"));

    body = addGroupCard(page, QT_TR_NOOP("Tracker 与 DHT"));
    addLineEdit(body, QStringLiteral("btTracker"), QT_TR_NOOP("附加 Tracker"),
                QT_TR_NOOP("为所有种子附加的 Tracker 地址，以逗号分隔"));
    addLineEdit(body, QStringLiteral("dhtEntryPoint"), QT_TR_NOOP("DHT 入口"),
                QT_TR_NOOP("加入 DHT 网络使用的入口节点 host:port"));
    addLineEdit(body, QStringLiteral("dhtEntryPoint6"), QT_TR_NOOP("DHT 入口 6"),
                QT_TR_NOOP("IPv6 网络上的 DHT 入口节点"));
    addLineEdit(body, QStringLiteral("dhtFilePath"), QT_TR_NOOP("DHT 文件"),
                QT_TR_NOOP("保存 DHT 路由表的文件路径"));
    page->addStretch(1);

    // =================================================== 5. RPC / 引擎
    page = addSectionPage(QT_TR_NOOP("RPC / 引擎"), QT_TR_NOOP("远程接口、进程与会话"),
                          FluentTheme::Glyph::Server);

    // Raised by markRestartNeeded() whenever one of this section's rows changes.
    m_restartBar = new InfoBar(page->parentWidget());
    m_restartBar->setSeverity(InfoBar::Warning);
    m_restartBar->setClosable(true);
    m_restartBar->setActionText(tr("重启引擎"));
    page->insertWidget(2, m_restartBar);
    connect(m_restartBar, &InfoBar::actionTriggered, this, &SettingsPage::restartEngine);
    refreshRestartBar();

    body = addGroupCard(page, QT_TR_NOOP("RPC"));
    addSpinBox(body, QStringLiteral("rpcListenPort"), QT_TR_NOOP("RPC 监听端口"),
               QT_TR_NOOP("aria2 的 JSON-RPC 端口，修改后必须重启引擎"), 1024, 65535);
    FluentLineEdit *secret =
        addLineEdit(body, QStringLiteral("rpcSecret"), QT_TR_NOOP("RPC 密钥"),
                    QT_TR_NOOP("访问 RPC 接口所需的令牌；留空表示不校验"));
    secret->setRevealButton(true);
    addSwitch(body, QStringLiteral("rpcAllowOriginAll"), QT_TR_NOOP("允许所有来源"),
              QT_TR_NOOP("接受任意 Origin 的 RPC 请求"));
    addSwitch(body, QStringLiteral("rpcListenAll"), QT_TR_NOOP("监听所有地址"),
              QT_TR_NOOP("在 0.0.0.0 上监听，允许局域网访问"));
    addSpinBox(body, QStringLiteral("rpcMaxRequestSize"), QT_TR_NOOP("最大请求大小"),
               QT_TR_NOOP("单个 RPC 请求的字节上限"), 1024, 1073741824, tr("字节"));

    body = addGroupCard(page, QT_TR_NOOP("引擎"));
    FieldSlot exeSlot = addInlineRow(body);
    FluentLineEdit *exeEdit =
        addLineEdit(exeSlot.field, QStringLiteral("aria2Executable"), QT_TR_NOOP("aria2 可执行文件"),
                    QT_TR_NOOP("aria2c 的完整路径；留空则使用程序目录下的副本"), true);
    FluentButton *browseExe = addTrailingAction(exeSlot.trailing, QT_TR_NOOP("浏览…"),
                                               FluentButton::Standard, FluentTheme::Glyph::OpenFile);
    connect(browseExe, &QPushButton::clicked, this, [this, exeEdit]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("选择 aria2c 可执行文件"), exeEdit->text(),
            tr("可执行文件 (aria2c.exe *.exe);;所有文件 (*)"));
        if (path.isEmpty())
            return;
        exeEdit->setText(path);
        storeValue(QStringLiteral("aria2Executable"), path, SectionEngine);
    });
    addLineEdit(body, QStringLiteral("configFilePath"), QT_TR_NOOP("配置文件路径"),
                QT_TR_NOOP("传给 aria2 的 --conf-path，留空使用默认值"));
    addSwitch(body, QStringLiteral("autoRestartEngine"), QT_TR_NOOP("自动重启引擎"),
              QT_TR_NOOP("引擎崩溃或配置变更后自动重新启动"));
    addSwitch(body, QStringLiteral("stopWithProcess"), QT_TR_NOOP("随进程退出"),
              QT_TR_NOOP("退出 Fetchora 时一并结束 aria2 进程"));
    addSwitch(body, QStringLiteral("pauseMetadata"), QT_TR_NOOP("元数据下载后暂停"),
              QT_TR_NOOP("磁力链接解析完成后先暂停，等待用户确认文件"));
    addSwitch(body, QStringLiteral("keepUnfinishedDownloadResult"),
              QT_TR_NOOP("保留未完成的任务"),
              QT_TR_NOOP("退出时把未完成的任务写入会话文件"));

    body = addGroupCard(page, QT_TR_NOOP("会话"));
    addLineEdit(body, QStringLiteral("sessionFile"), QT_TR_NOOP("会话文件"),
                QT_TR_NOOP("保存未完成任务列表的文件路径"));
    addSwitch(body, QStringLiteral("autoSaveSession"), QT_TR_NOOP("自动保存会话"),
              QT_TR_NOOP("定期把任务列表写入会话文件"));
    addSpinBox(body, QStringLiteral("saveSessionInterval"), QT_TR_NOOP("保存间隔"),
               QT_TR_NOOP("自动保存会话的时间间隔"), 0, 86400, tr("秒"));

    body = addGroupCard(page, QT_TR_NOOP("附加命令行参数"));
    addLineEdit(body, QStringLiteral("extraAria2Args"), QT_TR_NOOP("附加命令行参数"),
                QT_TR_NOOP("直接追加到 aria2c 命令行的开关，例如 --max-download-limit=1M"),
                true);
    QHBoxLayout *engineStrip = addButtonStrip(body, true);
    FluentButton *verifyArgs = addAction(engineStrip, QT_TR_NOOP("校验参数"), FluentButton::Standard,
                                        FluentTheme::Glyph::Check);
    connect(verifyArgs, &QPushButton::clicked, this, [this]() {
        if (!confirm(tr("校验参数"),
                     tr("将按当前参数重新启动 aria2 引擎；参数无效时会给出提示，"
                        "已经完成的任务不会丢失。是否继续？")))
            return;
        restartEngine();
    });
    FluentButton *restartNow = addAction(engineStrip, QT_TR_NOOP("重启引擎"), FluentButton::Accent,
                                        FluentTheme::Glyph::Refresh);
    connect(restartNow, &QPushButton::clicked, this, &SettingsPage::restartEngine);
    page->addStretch(1);

    // ============================================ 6. 通知与集成 / Notifications
    page = addSectionPage(QT_TR_NOOP("通知与集成"), QT_TR_NOOP("系统通知、剪贴板与浏览器桥接"),
                          FluentTheme::Glyph::Info);

    m_bridgeBar = new InfoBar(page->parentWidget());
    m_bridgeBar->setSeverity(InfoBar::Info);
    page->insertWidget(2, m_bridgeBar);
    refreshBridgeInfo();

    body = addGroupCard(page, QT_TR_NOOP("通知"));
    addSwitch(body, QStringLiteral("enableCompleteNotification"), QT_TR_NOOP("完成时通知"),
              QT_TR_NOOP("任务下载完成后弹出系统通知"));
    addSwitch(body, QStringLiteral("enableErrorNotification"), QT_TR_NOOP("失败时通知"),
              QT_TR_NOOP("任务出错时弹出系统通知"));
    addSwitch(body, QStringLiteral("notifyOnStart"), QT_TR_NOOP("开始时通知"),
              QT_TR_NOOP("任务开始下载时也弹出通知"));
    addSwitch(body, QStringLiteral("showTraySpeed"), QT_TR_NOOP("托盘显示速度"),
              QT_TR_NOOP("在托盘图标的提示中显示实时下载与上传速度"));

    body = addGroupCard(page, QT_TR_NOOP("剪贴板"));
    addSwitch(body, QStringLiteral("autoPasteClipboard"), QT_TR_NOOP("自动粘贴剪贴板"),
              QT_TR_NOOP("新建任务时自动填入剪贴板中的链接"));
    addSwitch(body, QStringLiteral("clipboardMonitor"), QT_TR_NOOP("剪贴板监控"),
              QT_TR_NOOP("复制下载链接时自动弹出新建任务窗口"));

    body = addGroupCard(page, QT_TR_NOOP("浏览器集成"));
    FluentSwitch *bridgeSwitch =
        addSwitch(body, QStringLiteral("browserIntegration"), QT_TR_NOOP("浏览器集成"),
                  QT_TR_NOOP("浏览器扩展会把下载转发到 Fetchora"));
    connect(bridgeSwitch, &QAbstractButton::toggled, this, [this](bool) { refreshBridgeInfo(); });
    addSpinBox(body, QStringLiteral("browserPort"), QT_TR_NOOP("浏览器端口"),
               QT_TR_NOOP("浏览器扩展连接本程序所使用的端口"), 1024, 65535);
    page->addStretch(1);

    // ================================================== 7. 历史记录 / History
    page = addSectionPage(QT_TR_NOOP("历史记录"), QT_TR_NOOP("下载历史的存储与清理"),
                          FluentTheme::Glyph::History);

    body = addGroupCard(page, QT_TR_NOOP("存储"));
    addSwitch(body, QStringLiteral("enableSqliteHistory"), QT_TR_NOOP("使用 SQLite 历史"),
              QT_TR_NOOP("把下载记录写入 SQLite 数据库，便于搜索与统计"));
    FieldSlot dbSlot = addInlineRow(body);
    FluentLineEdit *dbEdit =
        addLineEdit(dbSlot.field, QStringLiteral("sqliteDbPath"), QT_TR_NOOP("数据库路径"),
                    QT_TR_NOOP("留空则使用程序数据目录下的 history.db"), true);
    FluentButton *browseDb = addTrailingAction(dbSlot.trailing, QT_TR_NOOP("浏览…"),
                                              FluentButton::Standard, FluentTheme::Glyph::Save);
    connect(browseDb, &QPushButton::clicked, this, [this, dbEdit]() {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("选择历史数据库"), dbEdit->text(), tr("SQLite 数据库 (*.db);;所有文件 (*)"));
        if (path.isEmpty())
            return;
        dbEdit->setText(path);
        storeValue(QStringLiteral("sqliteDbPath"), path, SectionHistory);
    });
    addSpinBox(body, QStringLiteral("historyKeepEntries"), QT_TR_NOOP("保留条数"),
               QT_TR_NOOP("最多保留的历史记录条数，超出的部分会被清理"), 0, 1000000, tr("条"));

    body = addGroupCard(page, QT_TR_NOOP("维护"));
    QHBoxLayout *historyStrip = addButtonStrip(body, false);
    FluentButton *pruneHistory = addAction(historyStrip, QT_TR_NOOP("立即清理"),
                                          FluentButton::Standard, FluentTheme::Glyph::Filter);
    FluentButton *clearHistory = addAction(historyStrip, QT_TR_NOOP("清空历史"),
                                          FluentButton::Danger, FluentTheme::Glyph::Delete);
    connect(pruneHistory, &QPushButton::clicked, this, [this]() {
        if (!m_aria2 || !m_aria2->history())
            return;
        const int keep = readValue(QStringLiteral("historyKeepEntries"), 2000).toInt();
        const int removed = m_aria2->history()->prune(keep);
        emit toast(removed > 0 ? tr("已清理 %1 条历史记录").arg(removed)
                               : tr("没有需要清理的历史记录"),
                   false);
    });
    connect(clearHistory, &QPushButton::clicked, this, [this]() {
        if (!m_aria2 || !m_aria2->history())
            return;
        if (!confirm(tr("清空历史记录"),
                     tr("将删除全部下载历史，已经下载的文件不会被删除。此操作无法撤销。")))
            return;
        m_aria2->history()->clear();
        emit toast(tr("下载历史已清空"), false);
    });
    page->addStretch(1);

    // ============================================ 8. 高级 / 开发者 / Advanced
    page = addSectionPage(QT_TR_NOOP("高级 / 开发者"), QT_TR_NOOP("调试开关、引擎日志与维护工具"),
                          FluentTheme::Glyph::Console);

    body = addGroupCard(page, QT_TR_NOOP("开发者选项"));
    addSwitch(body, QStringLiteral("advancedUser"), QT_TR_NOOP("高级用户模式"),
              QT_TR_NOOP("显示额外的实验性选项与原始参数"));
    addSwitch(body, QStringLiteral("enableEngineLog"), QT_TR_NOOP("显示引擎日志"),
              QT_TR_NOOP("记录 aria2c 的输出，便于排查启动问题"));
    addSwitch(body, QStringLiteral("enableRpcConsole"), QT_TR_NOOP("RPC 控制台"),
              QT_TR_NOOP("记录每一次 JSON-RPC 调用与响应"));

    m_engineConsole = addConsole(page, QT_TR_NOOP("引擎日志"),
                                 QT_TR_NOOP("aria2c 进程的标准输出与错误输出"));
    m_rpcConsole = addConsole(page, QT_TR_NOOP("RPC 日志"),
                              QT_TR_NOOP("与 aria2 之间的 JSON-RPC 请求与响应"));

    body = addGroupCard(page, QT_TR_NOOP("维护"));
    QHBoxLayout *maintenanceStrip = addButtonStrip(body, false);
    FluentButton *openConfig = addAction(maintenanceStrip, QT_TR_NOOP("打开配置目录"),
                                         FluentButton::Standard, FluentTheme::Glyph::Folder);
    FluentButton *resetAll = addAction(maintenanceStrip, QT_TR_NOOP("重置全部设置"),
                                       FluentButton::Danger, FluentTheme::Glyph::Refresh);
    connect(openConfig, &QPushButton::clicked, this, [this]() {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(dir);
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir)))
            emit toast(tr("无法打开配置目录 %1").arg(dir), true);
    });
    connect(resetAll, &QPushButton::clicked, this, [this]() {
        if (!m_settings)
            return;
        if (!confirm(tr("重置全部设置"),
                     tr("下载目录、连接数、代理、BitTorrent 与 RPC 等设置都会恢复为默认值，"
                        "此操作无法撤销。已经下载的文件不会被删除。")))
            return;
        m_settings->resetToDefaults();
        refreshFields();
        refreshSubtitle();
        refreshBridgeInfo();
        // The language and the theme are the two things a reset has to push
        // back into the running UI immediately.
        LanguageManager::instance()->apply(m_settings->language());
        FluentTheme::instance()->setMode(m_settings->theme());
        emit toast(tr("所有设置已恢复为默认值"), false);
    });
    page->addStretch(1);

    // ==================================================== 9. 关于 / About
    page = addSectionPage(QT_TR_NOOP("关于"), QT_TR_NOOP("版本与许可信息"),
                          FluentTheme::Glyph::App);
    body = addGroupCard(page, QT_TR_NOOP("关于 Fetchora"));
    addCaption(body, QT_TR_NOOP("Fetchora %1"), "subtitle",
               QCoreApplication::applicationVersion());
    addCaption(body, QT_TR_NOOP("基于 aria2 的 Fluent 风格下载管理器 · MIT License"),
               "caption", QString(), true);
    addCaption(body, QT_TR_NOOP("关于 Fetchora：完整的功能说明与开源许可见左侧「关于」页面。"),
               "caption", QString(), true);
    page->addStretch(1);

    Q_ASSERT(m_sections.size() == SectionCount);
}

// ------------------------------------------------------------------ structure
QVBoxLayout *SettingsPage::addSectionPage(const char *title, const char *subtitle, const QChar &glyph)
{
    const int index = m_sections.size();

    Section section;
    section.title = title;
    section.subtitle = subtitle;
    section.glyph = glyph;
    section.rowName = QStringLiteral("settingsRailRow%1").arg(index);

    // Rail row: a tinted wrapper around a Subtle FluentButton. FluentButton
    // paints itself from its role, so the "selected" tint is applied to the
    // wrapper - that is also what makes restyle() able to recolour it.
    auto *row = new QWidget(ui->railNavHost);
    row->setObjectName(section.rowName);
    row->setAttribute(Qt::WA_StyledBackground, true);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(2, 2, 2, 2);
    rowLayout->setSpacing(0);

    auto *button = new FluentButton(row);
    button->setRole(FluentButton::Subtle);
    button->setGlyph(glyph);
    button->setText(tr(title));
    rowLayout->addWidget(button, 1);
    ui->railNavLayout->addWidget(row);
    connect(button, &QPushButton::clicked, this, [this, index]() { setSection(index); });

    // Page: a scroll area so a long section stays usable in a short window.
    auto *area = new QScrollArea(ui->sectionStack);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setStyleSheet(scrollAreaStyle());

    auto *host = new QWidget(area);
    auto *page = new QVBoxLayout(host);
    page->setContentsMargins(0, 0, FluentTheme::spacingM(), FluentTheme::spacingL());
    page->setSpacing(FluentTheme::spacingM());
    area->setWidget(host);
    ui->sectionStack->addWidget(area);

    auto *heading = new QLabel(host);
    heading->setProperty("fluentRole", "subtitle");
    heading->setText(tr(title));
    page->addWidget(heading);
    auto *caption = new QLabel(host);
    caption->setProperty("fluentRole", "caption");
    caption->setText(subtitle ? tr(subtitle) : QString());
    page->addWidget(caption);

    section.railRow = row;
    section.railButton = button;
    section.titleLabel = heading;
    section.subtitleLabel = caption;
    m_sections.append(section);

    // Every field built from now on belongs to this section.
    m_buildSection = index;
    return page;
}

QVBoxLayout *SettingsPage::addGroupCard(QVBoxLayout *page, const char *title)
{
    auto *card = new FluentCard(page->parentWidget());
    card->setVariant(FluentCard::Layer);
    page->addWidget(card);
    m_groups.append(card);

    if (title) {
        QLabel *heading = addCaption(card->body(), title, "body");
        QFont font = heading->font();
        font.setWeight(QFont::DemiBold);
        heading->setFont(font);
    }
    return card->body();
}

QLabel *SettingsPage::addCaption(QVBoxLayout *into, const char *source, const char *role,
                                 const QString &arg, bool wrap)
{
    auto *label = new QLabel(into->parentWidget());
    if (role)
        label->setProperty("fluentRole", role);
    label->setWordWrap(wrap);
    label->setText(arg.isEmpty() ? tr(source) : tr(source).arg(arg));
    into->addWidget(label);

    Caption caption;
    caption.label = label;
    caption.source = source;
    caption.arg = arg;
    m_captions.append(caption);
    return label;
}

SettingsPage::FieldSlot SettingsPage::addInlineRow(QVBoxLayout *into)
{
    // A field with a trailing button on the same line: [field ...][button].
    auto *host = new QWidget(into->parentWidget());
    auto *row = new QHBoxLayout(host);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(FluentTheme::spacingM());

    auto *column = new QVBoxLayout();
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(FluentTheme::spacingS());
    row->addLayout(column, 1);
    into->addWidget(host);

    FieldSlot slot;
    slot.field = column;
    slot.trailing = row;
    return slot;
}

QHBoxLayout *SettingsPage::addButtonStrip(QVBoxLayout *into, bool rightAligned)
{
    auto *strip = new QHBoxLayout();
    strip->setContentsMargins(0, 0, 0, 0);
    strip->setSpacing(FluentTheme::spacingS());
    if (rightAligned)
        strip->addStretch(1);
    into->addLayout(strip);
    if (!rightAligned)
        strip->addStretch(1);
    return strip;
}

FluentButton *SettingsPage::addAction(QHBoxLayout *strip, const char *text, FluentButton::Role role,
                                      const QChar &glyph)
{
    auto *button = new FluentButton(this);
    button->setRole(role);
    if (!glyph.isNull())
        button->setGlyph(glyph);
    button->setText(tr(text));
    strip->addWidget(button, 0, Qt::AlignVCenter);
    m_actionLabels.append(qMakePair(button, text));
    return button;
}

FluentButton *SettingsPage::addTrailingAction(QHBoxLayout *row, const char *text,
                                              FluentButton::Role role, const QChar &glyph)
{
    // Bottom aligned: the input box of a FluentLineEdit is its last 32 pixels,
    // so this lines the button up with the box rather than with the caption.
    FluentButton *button = addAction(row, text, role, glyph);
    row->setAlignment(button, Qt::AlignBottom);
    return button;
}

QPlainTextEdit *SettingsPage::addConsole(QVBoxLayout *page, const char *title, const char *description)
{
    QVBoxLayout *body = addGroupCard(page, title);
    if (description)
        addCaption(body, description, "caption", QString(), true);

    auto *view = new QPlainTextEdit(body->parentWidget());
    view->setReadOnly(true);
    view->setProperty("fluentRole", "textArea");
    view->setProperty("fluentMono", true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setMaximumBlockCount(4000);
    view->setMinimumHeight(180);
    view->setFont(monospaceFont());
    body->addWidget(view);

    QHBoxLayout *strip = addButtonStrip(body, true);
    FluentButton *clear = addAction(strip, QT_TR_NOOP("清空显示"), FluentButton::Subtle,
                                    FluentTheme::Glyph::Delete);
    FluentButton *save = addAction(strip, QT_TR_NOOP("保存日志…"), FluentButton::Subtle,
                                   FluentTheme::Glyph::Save);
    connect(clear, &QPushButton::clicked, this, [view]() {
        // The "already shown" marker is intentionally kept, so the console does
        // not immediately refill with everything that was written before.
        view->clear();
    });
    connect(save, &QPushButton::clicked, this, [this, view]() { saveConsole(view); });
    return view;
}

// ------------------------------------------------------- generic field builders
FluentSwitch *SettingsPage::addSwitch(QVBoxLayout *into, const QString &key, const char *title,
                                      const char *description)
{
    // FluentSwitch paints the toggle only, so the caption row is built here.
    auto *row = new QWidget(into->parentWidget());
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(FluentTheme::spacingL());

    auto *text = new QVBoxLayout();
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    auto *titleLabel = new QLabel(tr(title), row);
    auto *descLabel = description ? new QLabel(tr(description), row) : nullptr;
    if (descLabel) {
        descLabel->setProperty("fluentRole", "caption");
        descLabel->setWordWrap(true);
    }
    text->addWidget(titleLabel);
    if (descLabel)
        text->addWidget(descLabel);
    layout->addLayout(text, 1);

    auto *sw = new FluentSwitch(row);
    sw->setHeader(tr(title));
    if (description)
        sw->setDescription(tr(description));
    sw->setChecked(readValue(key, false).toBool());
    sw->setFixedSize(40, 20);
    layout->addWidget(sw, 0, Qt::AlignVCenter);
    into->addWidget(row);

    Field field;
    field.kind = Field::Toggle;
    field.widget = sw;
    field.row = row;
    field.group = groupOf(into);
    field.titleLabel = titleLabel;
    field.descLabel = descLabel;
    field.key = key;
    field.title = title;
    field.description = description;
    field.section = m_buildSection;
    m_fields.append(field);

    const int section = m_buildSection;
    connect(sw, &QAbstractButton::toggled, this, [this, key, section](bool on) {
        storeValue(key, on, section);
    });
    return sw;
}

FluentLineEdit *SettingsPage::addLineEdit(QVBoxLayout *into, const QString &key, const char *title,
                                         const char *description, bool monospace)
{
    auto *edit = new FluentLineEdit(into->parentWidget());
    edit->setHeader(tr(title));
    if (description)
        edit->setDescription(tr(description));
    if (monospace)
        edit->setMonospace(true);
    edit->setText(readValue(key, QString()).toString());
    into->addWidget(edit);

    Field field;
    field.kind = Field::Text;
    field.widget = edit;
    field.row = qobject_cast<FluentCard *>(into->parentWidget())
                    ? static_cast<QWidget *>(edit)
                    : into->parentWidget();
    field.group = groupOf(into);
    field.key = key;
    field.title = title;
    field.description = description;
    field.section = m_buildSection;
    m_fields.append(field);

    const int section = m_buildSection;
    // textEdited only fires for user input, so refreshFields() cannot echo.
    connect(edit, &QLineEdit::textEdited, this, [this, key, section](const QString &text) {
        storeValue(key, text, section);
    });
    return edit;
}

FluentSpinBox *SettingsPage::addSpinBox(QVBoxLayout *into, const QString &key, const char *title,
                                        const char *description, int lo, int hi,
                                        const QString &suffix)
{
    auto *spin = new FluentSpinBox(into->parentWidget());
    spin->setRange(lo, hi);
    if (!suffix.isEmpty())
        spin->setSuffix(QStringLiteral(" ") + suffix);
    spin->setHeader(tr(title));
    if (description)
        spin->setDescription(tr(description));
    spin->setValue(readValue(key, lo).toInt());
    into->addWidget(spin);

    Field field;
    field.kind = Field::Number;
    field.widget = spin;
    field.row = qobject_cast<FluentCard *>(into->parentWidget())
                    ? static_cast<QWidget *>(spin)
                    : into->parentWidget();
    field.group = groupOf(into);
    field.key = key;
    field.title = title;
    field.description = description;
    field.section = m_buildSection;
    m_fields.append(field);

    const int section = m_buildSection;
    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, key, section](int value) {
        storeValue(key, value, section);
    });
    return spin;
}

FluentComboBox *SettingsPage::addCombo(QVBoxLayout *into, const QString &key, const char *title,
                                       const char *const *labels, const QStringList &values)
{
    auto *combo = new FluentComboBox(into->parentWidget());
    combo->setHeader(tr(title));
    for (int i = 0; i < values.size(); ++i)
        combo->addItem(labels ? tr(labels[i]) : values.at(i), values.at(i));
    const int index = combo->findData(readValue(key, values.value(0)).toString());
    combo->setCurrentIndex(index < 0 ? 0 : index);
    into->addWidget(combo);

    Field field;
    field.kind = Field::Choice;
    field.widget = combo;
    field.row = qobject_cast<FluentCard *>(into->parentWidget())
                    ? static_cast<QWidget *>(combo)
                    : into->parentWidget();
    field.group = groupOf(into);
    field.key = key;
    field.title = title;
    field.section = m_buildSection;
    for (int i = 0; i < values.size(); ++i)
        field.labels.append(labels ? labels[i] : nullptr);
    field.values = values;
    m_fields.append(field);

    const int section = m_buildSection;
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, key, combo, section](int) {
                storeValue(key, combo->currentData().toString(), section);
            });
    return combo;
}

void SettingsPage::rebuildCombo(FluentComboBox *combo, const Field &field)
{
    const QSignalBlocker blocker(combo);
    const QString current = combo->currentData().toString();
    combo->clear();
    for (int i = 0; i < field.values.size(); ++i) {
        const char *source = field.labels.value(i, nullptr);
        combo->addItem(source ? tr(source) : field.values.at(i), field.values.at(i));
    }
    const int index = combo->findData(current);
    combo->setCurrentIndex(index < 0 ? 0 : index);
}

QWidget *SettingsPage::groupOf(const QVBoxLayout *into) const
{
    QWidget *widget = into ? into->parentWidget() : nullptr;
    while (widget && !qobject_cast<FluentCard *>(widget))
        widget = widget->parentWidget();
    return widget;
}

// ============================================================================
//  Reading and writing
// ============================================================================
QVariant SettingsPage::readValue(const QString &key, const QVariant &fallback) const
{
    if (!m_settings)
        return fallback;
    const QVariant stored = m_settings->value(key);
    if (stored.isValid())
        return stored;
    // Nothing was ever written for this key: the value loadSettings() applied
    // carries the default, which raw QSettings cannot know about.
    const QVariant live = m_settings->property(key.toUtf8().constData());
    return live.isValid() ? live : fallback;
}

void SettingsPage::writeValue(const QString &key, const QVariant &value)
{
    if (m_refreshing || !m_settings)
        return;
    // Every key this page edits is also a Q_PROPERTY of SettingsManager. The
    // property write runs the typed setter, which is what keeps the in-memory
    // value (and therefore the aria2 command line) in step with what is stored;
    // setValue() is the fallback for a key without a property of its own.
    if (!m_settings->setProperty(key.toUtf8().constData(), value))
        m_settings->setValue(key, value);
}

void SettingsPage::storeValue(const QString &key, const QVariant &value, int section)
{
    if (m_refreshing)
        return;
    writeValue(key, value);
    if (section == SectionEngine)
        markRestartNeeded();
}

void SettingsPage::refreshFields()
{
    // Re-read every row from the settings, e.g. after resetToDefaults().
    m_refreshing = true;
    for (const Field &field : std::as_const(m_fields)) {
        switch (field.kind) {
        case Field::Toggle: {
            if (auto *sw = qobject_cast<FluentSwitch *>(field.widget))
                sw->setChecked(readValue(field.key, false).toBool());
            break;
        }
        case Field::Text: {
            if (auto *edit = qobject_cast<FluentLineEdit *>(field.widget))
                edit->setText(readValue(field.key, QString()).toString());
            break;
        }
        case Field::Number: {
            auto *spin = qobject_cast<FluentSpinBox *>(field.widget);
            if (!spin)
                break;
            if (field.key == QLatin1String("seedRatio")) {
                // Tenths, see buildSections().
                spin->setValue(qRound(readValue(field.key, 1.0).toDouble() * 10.0));
            } else {
                spin->setValue(readValue(field.key, spin->minimum()).toInt());
            }
            break;
        }
        case Field::Choice: {
            if (auto *combo = qobject_cast<FluentComboBox *>(field.widget)) {
                const QSignalBlocker blocker(combo);
                const int index =
                    combo->findData(readValue(field.key, field.values.value(0)).toString());
                combo->setCurrentIndex(index < 0 ? 0 : index);
            }
            break;
        }
        }
    }
    m_refreshing = false;
}

// ============================================================================
//  Live feedback
// ============================================================================
void SettingsPage::refreshLogs()
{
    if (!m_aria2)
        return;
    if (m_engineConsole)
        syncConsole(m_engineConsole, m_aria2->engineLog());
    if (m_rpcConsole)
        syncConsole(m_rpcConsole, m_aria2->rpcLog());
}

void SettingsPage::syncConsole(QPlainTextEdit *view, const QString &text)
{
    // The manager appends to its log and trims it from the front, so a plain
    // prefix comparison decides between "append the delta" and "start over".
    const QString shown = view->property("shownText").toString();
    if (shown == text)
        return;
    if (text.startsWith(shown)) {
        view->moveCursor(QTextCursor::End);
        view->insertPlainText(text.mid(shown.size()));
    } else {
        view->setPlainText(text);
    }
    view->setProperty("shownText", text);
    if (QScrollBar *bar = view->verticalScrollBar())
        bar->setValue(bar->maximum());
}

void SettingsPage::saveConsole(QPlainTextEdit *view)
{
    const QString suggested = QDir::home().filePath(QStringLiteral("fetchora-log.txt"));
    const QString path = QFileDialog::getSaveFileName(
        this, tr("保存日志"), suggested, tr("文本文件 (*.txt);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit toast(tr("无法写入 %1").arg(path), true);
        return;
    }
    QTextStream stream(&file);
    stream << view->toPlainText();
    file.close();
    emit toast(tr("日志已保存到 %1").arg(path), false);
}

void SettingsPage::refreshBridgeInfo()
{
    if (!m_bridgeBar)
        return;

    const bool listening = m_aria2 && m_aria2->bridgeListening();
    const int port = m_aria2 ? m_aria2->bridgePort() : 0;
    const int clients = m_aria2 ? m_aria2->bridgeClients() : 0;

    m_bridgeBar->setSeverity(listening ? InfoBar::Info : InfoBar::Warning);
    m_bridgeBar->setTitle(listening ? tr("浏览器桥接正在监听") : tr("浏览器桥接未启动"));
    if (!listening) {
        m_bridgeBar->setMessage(tr("启用浏览器集成并重启引擎后，扩展才能把下载转发过来。"));
    } else {
        m_bridgeBar->setMessage(
            tr("端口 %1 · 已连接 %2 个扩展 · 扩展通过 http://127.0.0.1:%1/ping 探测本程序")
                .arg(port)
                .arg(clients));
    }
}

void SettingsPage::refreshRestartBar()
{
    if (!m_restartBar)
        return;
    m_restartBar->setTitle(tr("部分设置需要重启引擎"));
    m_restartBar->setMessage(
        tr("RPC 端口、密钥、会话文件与附加命令行参数只在 aria2 启动时读取一次。"));
    m_restartBar->setActionText(tr("重启引擎"));
}

void SettingsPage::refreshSubtitle()
{
    if (!ui->pageSubtitle)
        return;
    if (m_filter.isEmpty()) {
        ui->pageSubtitle->setText(tr("共 %1 项设置，修改后立即保存").arg(m_fields.size()));
        return;
    }
    int visible = 0;
    for (const Field &field : std::as_const(m_fields)) {
        if (!field.row->isHidden())
            ++visible;
    }
    ui->pageSubtitle->setText(tr("找到 %1 项与“%2”匹配的设置").arg(visible).arg(m_filter));
}

// ============================================================================
//  Search filter and navigation
// ============================================================================
void SettingsPage::applyFilter(const QString &needle)
{
    m_filter = needle.trimmed();
    const bool filtering = !m_filter.isEmpty();

    for (const Field &field : std::as_const(m_fields)) {
        bool match = true;
        if (filtering) {
            match = tr(field.title).contains(m_filter, Qt::CaseInsensitive);
            if (!match && field.description)
                match = tr(field.description).contains(m_filter, Qt::CaseInsensitive);
        }
        // isHidden() (not isVisible()) because rows on other pages of the stack
        // are not visible either, yet must stay marked as matching.
        if (field.row->isHidden() == match)
            field.row->setVisible(match);
    }

    // A card disappears as soon as its last row is filtered out.
    for (QWidget *group : std::as_const(m_groups)) {
        bool any = false;
        for (const Field &field : std::as_const(m_fields)) {
            if (field.group == group && !field.row->isHidden()) {
                any = true;
                break;
            }
        }
        group->setVisible(any);
    }

    // Sections without a single match leave the rail.
    int firstMatch = -1;
    for (int i = 0; i < m_sections.size(); ++i) {
        bool any = false;
        for (const Field &field : std::as_const(m_fields)) {
            if (field.section == i && !field.row->isHidden()) {
                any = true;
                break;
            }
        }
        m_sections.at(i).railRow->setVisible(any);
        if (any && firstMatch < 0)
            firstMatch = i;
    }
    if (filtering && firstMatch >= 0)
        setSection(firstMatch);

    refreshSubtitle();
}

void SettingsPage::setSection(int index)
{
    if (index < 0 || index >= ui->sectionStack->count())
        return;
    ui->sectionStack->setCurrentIndex(index);
    restyle();
}

void SettingsPage::markRestartNeeded()
{
    if (m_refreshing)
        return;
    if (m_restartBar)
        m_restartBar->show();
    if (m_restartButton)
        m_restartButton->setVisible(true);
}

void SettingsPage::restartEngine()
{
    if (!m_aria2)
        return;
    m_aria2->restartEngine();
    if (m_restartBar)
        m_restartBar->hide();
    if (m_restartButton)
        m_restartButton->hide();
    emit toast(tr("正在按新的设置重启 aria2 引擎…"), false);
}

bool SettingsPage::confirm(const QString &title, const QString &message)
{
    return QMessageBox::question(this, title, message, QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No)
        == QMessageBox::Yes;
}

// ============================================================================
//  Language and theme
// ============================================================================
void SettingsPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        retranslate();
    }
}

void SettingsPage::retranslate()
{
    // The rail is re-captioned rather than rebuilt: recreating the buttons would
    // throw away the current selection and the scroll position.
    for (const Section &section : std::as_const(m_sections)) {
        if (section.railButton)
            section.railButton->setText(tr(section.title));
        if (section.titleLabel)
            section.titleLabel->setText(tr(section.title));
        if (section.subtitleLabel)
            section.subtitleLabel->setText(section.subtitle ? tr(section.subtitle) : QString());
    }

    // Every recorded row, in the order it was built.
    for (const Field &field : std::as_const(m_fields)) {
        if (field.titleLabel)
            field.titleLabel->setText(tr(field.title));
        if (field.descLabel && field.description)
            field.descLabel->setText(tr(field.description));

        switch (field.kind) {
        case Field::Toggle: {
            if (auto *sw = qobject_cast<FluentSwitch *>(field.widget)) {
                sw->setHeader(tr(field.title));
                if (field.description)
                    sw->setDescription(tr(field.description));
            }
            break;
        }
        case Field::Text: {
            if (auto *edit = qobject_cast<FluentLineEdit *>(field.widget)) {
                edit->setHeader(tr(field.title));
                if (field.description)
                    edit->setDescription(tr(field.description));
            }
            break;
        }
        case Field::Number: {
            if (auto *spin = qobject_cast<FluentSpinBox *>(field.widget)) {
                spin->setHeader(tr(field.title));
                if (field.description)
                    spin->setDescription(tr(field.description));
            }
            break;
        }
        case Field::Choice: {
            if (auto *combo = qobject_cast<FluentComboBox *>(field.widget)) {
                combo->setHeader(tr(field.title));
                if (field.keepItems) {
                    // The entries are font family names; only the "follow the
                    // system" entry is translated.
                    const QSignalBlocker blocker(combo);
                    const QString current = combo->currentData().toString();
                    if (combo->count() > 0)
                        combo->setItemText(0, tr("跟随系统"));
                    combo->setCurrentIndex(qMax(0, combo->findData(current)));
                } else {
                    rebuildCombo(combo, field);
                }
            }
            break;
        }
        }
    }

    for (const QPair<FluentButton *, const char *> &action : std::as_const(m_actionLabels))
        action.first->setText(tr(action.second));
    for (const Caption &caption : std::as_const(m_captions)) {
        caption.label->setText(caption.arg.isEmpty() ? tr(caption.source)
                                                    : tr(caption.source).arg(caption.arg));
    }

    if (m_search) {
        m_search->setHeader(tr("搜索设置"));
        m_search->setPlaceholderText(tr("搜索设置"));
    }
    if (m_clearSearch)
        m_clearSearch->setTooltipText(tr("清除搜索"));
    if (m_restartButton)
        m_restartButton->setTooltipText(tr("按新的设置重新启动 aria2 引擎"));

    refreshRestartBar();
    refreshBridgeInfo();
    // The captions the filter matches against have changed with the language.
    applyFilter(m_filter);
    restyle();
}

void SettingsPage::restyle()
{
    const FluentTheme *theme = FluentTheme::instance();
    const int current = ui->sectionStack ? ui->sectionStack->currentIndex() : 0;
    for (int i = 0; i < m_sections.size(); ++i) {
        const Section &section = m_sections.at(i);
        if (!section.railRow)
            continue;
        const QString sheet = railRowStyle(theme, i == current, section.rowName);
        // setStyleSheet() re-polishes the widget, so only do it on a change.
        if (section.railRow->styleSheet() != sheet)
            section.railRow->setStyleSheet(sheet);
    }
    if (ui->pageSubtitle) {
        ui->pageSubtitle->setStyleSheet(
            QStringLiteral("QLabel { color: %1; }").arg(theme->textTertiary().name()));
    }
}
