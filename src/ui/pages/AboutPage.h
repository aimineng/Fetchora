#ifndef ABOUTPAGE_H
#define ABOUTPAGE_H

#include "UpdateChecker.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class Aria2Manager;
class FluentButton;
class FluentCard;
class FluentIcon;
class InfoBar;
class QLabel;
class SettingsManager;

namespace Ui {
class AboutPage;
}

/**
 * AboutPage - hero plate with the live engine facts, the feature list, the
 * licence, the credits and the shell actions.
 *
 * Layout and static text live in AboutPage.ui; the cards, the badges, the
 * buttons that carry a glyph and every live value are built here, because a
 * .ui file can express neither the runtime theme nor the manager state.
 *
 * 检查更新由 UpdateChecker 驱动，本页只是它的状态机前端（见 UpdateState）：
 * 同一个按钮在「检查 → 可下载 → 正在下载 → 可安装」之间换图标、换文案、换可用性，
 * 结果除了写进 InfoBar，还会落到运行状态卡片的「最新版本 / 更新通道」两行上。
 */
class AboutPage : public QWidget
{
    Q_OBJECT
public:
    /// settings 是可选的；为空时回退到 aria2->settings()。
    explicit AboutPage(Aria2Manager *aria2, QWidget *parent = nullptr,
                       SettingsManager *settings = nullptr);
    ~AboutPage() override;

signals:
    /// 轻提示，由主窗口转发到 ToastHost（与其它页面同一套约定）。
    void toast(const QString &message, bool isError);

protected:
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    /// 检查更新的状态机：按钮的图标、文案与是否可点全部由它决定。
    enum class UpdateState {
        Idle,           ///< 还没查过：按钮 = 检查更新
        Checking,       ///< 正在向 GitHub 要发布列表
        UpToDate,       ///< 已是最新：按钮 = 检查更新
        Available,      ///< 有新版本：按钮 = 下载并安装
        Downloading,    ///< 正在下载安装包：按钮显示百分数
        ReadyToInstall, ///< 安装包已就绪：按钮 = 重启并安装
    };

    /// InfoBar 此刻该显示哪一条结果。
    ///
    /// 通知是「一次结果」的：内容是算出来的，不是存下来的。把它记成一个小枚举，
    /// 切换语言时才能用新语言把同一条通知原样再渲染一遍，而不是留下一屏旧语言
    /// 的文字。None 表示还没出过结果，此时不动 InfoBar。
    enum class NoticeKind {
        None,
        Checking,
        UpToDate,
        Available,
        Downloading,
        ReadyToInstall,
        CheckFailed,
        DownloadFailed,
        NoAsset,
        InstallFailed,
    };

    /// 运行状态卡片里的一行：色点 + 名称 + 值。
    struct FactRow {
        QLabel *label = nullptr;
        QLabel *dot = nullptr;
        QLabel *value = nullptr;
    };

    void buildHero();
    void buildBadges();
    void buildCards();
    void buildActions();
    void wireManager();
    void wireUpdateChecker();

    /// 建一张信息卡，把标题行放进卡片并返回卡片本体。
    FluentCard *addCard(const QChar &glyph, QLabel **titleOut);

    /// 打开随程序分发的文档；找不到时回退到 fallbackUrl，再不行就提示。
    void openDocument(const QStringList &fileNames, const QString &fallbackUrl,
                      const QString &missingHint);
    void openProjectHome();
    void openLicense();
    void openConfigFolder();
    /// 检查更新按钮的唯一入口，按当前状态分派。
    void checkForUpdates();

    // ------------------------------------------------------------ 更新检查
    void beginUpdateCheck();
    void beginUpdateDownload();
    void installReadyUpdate();
    void openReleasePage();
    void onUpdateCheckFinished(const UpdateChecker::Release &release, bool updateAvailable);
    void onUpdateCheckFailed(const QString &reason);
    void onUpdateProgress(qint64 received, qint64 total);
    void onUpdateDownloaded(const QString &path);
    void onUpdateDownloadFailed(const QString &reason);
    /// 按当前状态重设按钮的图标、文案、提示与可用性（含下载百分数）。
    void updateButtonForState();
    /// 重写「最新版本 / 更新通道」两行事实。
    void updateUpdateFacts();
    /// 新版本的一行摘要：标签 + 发布日期 + 正文前 300 字。
    QString releaseSummary(const UpdateChecker::Release &release) const;

    /// 按 m_noticeKind 重建 InfoBar 上的那一条通知。
    void renderNotice();

    void setFact(int index, const QString &value, const QString &tone);
    void applyFactTone(QLabel *dot) const;

    void refresh();
    void retranslate();
    void restyle();

    Ui::AboutPage *ui = nullptr;
    Aria2Manager *m_aria2 = nullptr;
    SettingsManager *m_settings = nullptr;

    FluentIcon *m_heroGlyph = nullptr;
    QList<QLabel *> m_badges;

    InfoBar *m_notice = nullptr;

    QList<FluentCard *> m_cards;
    QList<QLabel *> m_cardTitles;
    QList<FluentIcon *> m_cardIcons;
    QList<QLabel *> m_featureItems;
    QList<QLabel *> m_creditItems;
    QLabel *m_licenseBody = nullptr;
    QLabel *m_licenseHint = nullptr;
    FluentButton *m_licenseButton = nullptr;
    QList<FactRow> m_facts;

    FluentButton *m_updateButton = nullptr;
    FluentButton *m_homeButton = nullptr;
    FluentButton *m_configButton = nullptr;

    // ------------------------------------------------------------ 更新检查
    UpdateChecker *m_updates = nullptr;
    UpdateState m_updateState = UpdateState::Idle;
    NoticeKind m_noticeKind = NoticeKind::None;
    /// 当前提供给用户的那一个版本（按钮文案与发布页面都取自它）。
    UpdateChecker::Release m_release;
    /// downloadFinished() 交回来的安装包路径。
    QString m_installerPath;
    /// 正在下载的安装包文件名。
    QString m_downloadName;
    /// 最后一次失败的原因；来自 UpdateChecker 的已经是翻译好的文本。
    QString m_lastError;
    /// 下载进度百分数；<0 表示还不知道总大小。
    int m_downloadPercent = -1;
};

#endif // ABOUTPAGE_H
