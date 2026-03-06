#ifndef FENCEMANAGER_H
#define FENCEMANAGER_H

#include <QObject>
#include <QList>
#include <QSystemTrayIcon>
#include <QMenu>

class FenceWindow;

/**
 * @brief 围栏管理器
 * 管理所有桌面围栏，提供系统托盘交互
 */
class FenceManager : public QObject
{
    Q_OBJECT

public:
    static FenceManager* instance();

    void initialize();
    void shutdown();

    FenceWindow* createFence(const QString &title = "新建围栏");
    void removeFence(FenceWindow *fence);
    QList<FenceWindow*> fences() const { return m_fences; }

    void saveFences();
    void loadFences();

    void showAllFences();
    void hideAllFences();

private slots:
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void onFenceDeleteRequested(FenceWindow *fence);
    void onNewFenceRequested();
    void onSettingsRequested();
    void onExitRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit FenceManager(QObject *parent = nullptr);
    ~FenceManager();

    void setupTrayIcon();
    QPoint getNewFencePosition() const;

    QList<FenceWindow*> m_fences;
    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;
    bool m_fencesVisible = true;
    bool m_isShutdown = false;
};

#endif // FENCEMANAGER_H
