#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QVBoxLayout>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QList>

class TitleBar;
class FenceWidget;

/**
 * @brief 主窗口
 * 无边框毛玻璃效果窗口，包含围栏容器
 */
class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private slots:
    void onMinimizeRequested();
    void onMaximizeRequested();
    void onCloseRequested();
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void onNewFenceRequested();
    void onSettingsRequested();
    void onFenceDeleteRequested();

private:
    void setupUi();
    void setupTrayIcon();
    void setupBlurEffect();
    void loadFences();
    void saveFences();
    void saveWindowState();
    void restoreWindowState();
    void updateStatusLabel();

    TitleBar *m_titleBar;
    QWidget *m_centralWidget;
    QVBoxLayout *m_mainLayout;

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;

    QList<FenceWidget*> m_fences;
};

#endif // MAINWINDOW_H
