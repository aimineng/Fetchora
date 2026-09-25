#ifndef ABOUTPAGE_H
#define ABOUTPAGE_H

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
 */
class AboutPage : public QWidget
{
    Q_OBJECT
public:
    explicit AboutPage(Aria2Manager *aria2, QWidget *parent = nullptr);
    ~AboutPage() override;

protected:
    void changeEvent(QEvent *event) override;

private:
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

    /// 建一张信息卡，把标题行放进卡片并返回卡片本体。
    FluentCard *addCard(const QChar &glyph, QLabel **titleOut);

    /// 打开随程序分发的文档；找不到时回退到 fallbackUrl，再不行就提示。
    void openDocument(const QStringList &fileNames, const QString &fallbackUrl,
                      const QString &missingHint);
    void openProjectHome();
    void openLicense();
    void openConfigFolder();
    void checkForUpdates();

    void setFact(int index, const QString &value, const QString &tone);
    void applyFactTone(QLabel *dot) const;

    void refresh();
    void retranslate();
    void restyle();

    Ui::AboutPage *ui = nullptr;
    Aria2Manager *m_aria2 = nullptr;

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
};

#endif // ABOUTPAGE_H
