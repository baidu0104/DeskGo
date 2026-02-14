#include "fencemanager.h"
#include "../ui/fencewindow.h"
#include "configmanager.h"
#include "../platform/desktophelper.h"
#include "../platform/blurhelper.h"

#include <QApplication>
#include <QScreen>
#include <QJsonArray>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QStyle>
#include <QFile>
#include <QCoreApplication>
#include <QDebug>
#include <QWidgetAction>
#include <QLabel>
#include <QEvent>
#include <QMouseEvent>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

FenceManager* FenceManager::instance()
{
    static FenceManager *instance = new FenceManager();
    return instance;
}

FenceManager::FenceManager(QObject *parent)
    : QObject(parent)
    , m_trayIcon(nullptr)
    , m_trayMenu(nullptr)
{
}

FenceManager::~FenceManager()
{
    shutdown();
}

void FenceManager::initialize()
{
    setupTrayIcon();
    loadFences();
    
    showAllFences();
}

void FenceManager::shutdown()
{
    if (m_isShutdown) return;
    m_isShutdown = true;
    
    saveFences();
    
    // 关闭所有围栏
    for (FenceWindow *fence : m_fences) {
        if (fence) {
            fence->close();
            fence->deleteLater();
        }
    }
    m_fences.clear();
    
    // 清理托盘图标
    if (m_trayIcon) {
        m_trayIcon->hide();
        m_trayIcon->deleteLater();
        m_trayIcon = nullptr;
    }
}

void FenceManager::setupTrayIcon()
{
    m_trayIcon = new QSystemTrayIcon(this);
    
    // 获取应用程序目录
    QString appDir = QCoreApplication::applicationDirPath();
    
    // 构建可能的图标路径列表
    QStringList iconPaths = {
        ":/icons/app.png",           
        "app.png",                   
        appDir + "/app.png",         
        appDir + "/../resources/icons/app.png", 
        appDir + "/../../resources/icons/app.png",
        ":/icons/app.ico"            
    };
    
    QIcon icon;
    for (const QString &path : iconPaths) {
        if (QFile::exists(path)) {
            QIcon tempIcon(path);
            if (!tempIcon.isNull()) {
                icon = tempIcon;
                break;
            }
        }
    }
    
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip("DeskGo - 桌面围栏");
    
    m_trayMenu = new QMenu();
    m_trayMenu->setAttribute(Qt::WA_TranslucentBackground);
    m_trayMenu->setAttribute(Qt::WA_NoSystemBackground);
    // 给边框留出 1px 的边距，防止边缘毛刺
    m_trayMenu->setContentsMargins(1, 1, 1, 1);
    
    m_trayMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    
    m_trayMenu->setStyleSheet(R"(
        QMenu {
            background-color: rgba(45, 45, 50, 240);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 12px;
            padding: 8px;
            font-family: "Microsoft YaHei", "Segoe UI";
            font-size: 13px;
        }
        QMenu::item {
            background: transparent;
            color: #ffffff;
            padding: 6px 16px;
            border-radius: 6px;
            margin: 2px 4px;
        }
        QMenu::item:selected {
            background-color: rgba(255, 255, 255, 0.1);
        }
        QLabel#menuItemLabel {
            color: #ffffff;
            padding: 8px 20px;
            border-radius: 6px;
            font-size: 13px;
            font-weight: bold;
            font-family: "Microsoft YaHei", "Segoe UI";
        }
        QLabel#menuItemLabel:hover {
            background: rgba(255, 255, 255, 0.1);
        }
        QMenu::separator {
            height: 1px;
            background: rgba(255, 255, 255, 0.1);
            margin: 6px 12px;
        }
    )");

#ifdef Q_OS_WIN
    connect(m_trayMenu, &QMenu::aboutToShow, this, [this]() {
        QTimer::singleShot(10, this, [this]() {
            if (!m_trayMenu) return;
            // 物理裁剪圆角 (解决黑点问题)
            BlurHelper::enableRoundedCorners(m_trayMenu, 12);
            
            HWND hMenu = (HWND)m_trayMenu->winId();
            if (hMenu) {
                SetForegroundWindow(hMenu);
            }
        });
    });
#endif

    auto addCenteredAction = [this](const QString &text, const std::function<void()> &callback) {
        QWidgetAction *action = new QWidgetAction(m_trayMenu);
        QLabel *label = new QLabel(text);
        label->setObjectName("menuItemLabel");
        label->setAlignment(Qt::AlignCenter);
        label->setCursor(Qt::PointingHandCursor);
        action->setDefaultWidget(label);
        
        connect(action, &QAction::triggered, this, callback);
        label->installEventFilter(this);
        
        m_trayMenu->addAction(action);
        return action;
    };

    addCenteredAction("新建围栏", [this](){ onNewFenceRequested(); });
    addCenteredAction("显示/隐藏全部围栏", [this](){
        // 延迟执行，确保菜单关闭动画完成后再操作窗口
        QTimer::singleShot(50, this, [this]() {
            if (m_fencesVisible) {
                hideAllFences();
            } else {
                showAllFences();
            }
        });
    });

    addCenteredAction(ConfigManager::instance()->iconTextVisible() ? "隐藏图标文字" : "显示图标文字", [this]() {
         // 这里的 label 文本会在 ConfigManager 信号回调中自动更新
         bool current = ConfigManager::instance()->iconTextVisible();
         ConfigManager::instance()->setIconTextVisible(!current);
    });

    m_trayMenu->addSeparator();

    // 修复标准项颜色并尽量通过空格平衡视觉
    QAction *autoStartAction = m_trayMenu->addAction("开机自启动");
    autoStartAction->setCheckable(true);
    autoStartAction->setChecked(ConfigManager::instance()->autoStart());
    connect(autoStartAction, &QAction::toggled, [](bool checked) {
        ConfigManager::instance()->setAutoStart(checked);
    });

    m_trayMenu->addSeparator();

    addCenteredAction("关于 DeskGo", [this]() {
        QMessageBox::about(nullptr, "关于 DeskGo",
            "<div style='text-align: center; font-family: Microsoft YaHei;'>"
            "<h3>DeskGo</h3>"
            "<p>桌面围栏管理工具</p>"
            "<p>版本: 1.0.1</p>"
            "</div>");
    });

    addCenteredAction("退出应用", [this](){ onExitRequested(); });

    m_trayIcon->setContextMenu(m_trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, 
            this, &FenceManager::onTrayIconActivated);

    m_trayIcon->show();

    // 监听配置变化，实时更新所有围栏
    connect(ConfigManager::instance(), &ConfigManager::iconTextVisibleChanged, this, [this](bool visible) {
        for (FenceWindow *fence : m_fences) {
            if (fence) fence->setIconTextVisible(visible);
        }
        
        // 更新菜单项文字 (如果菜单正在显示，虽然通常点击后菜单就关了)
        // 遍历 action 找到对应的并更新文字
        QList<QAction*> actions = m_trayMenu->actions();
        for (QAction *action : actions) {
            QWidgetAction *wa = qobject_cast<QWidgetAction*>(action);
            if (wa) {
                QLabel *label = qobject_cast<QLabel*>(wa->defaultWidget());
                if (label) {
                    if (label->text().contains("图标文字")) {
                         label->setText(visible ? "隐藏图标文字" : "显示图标文字");
                    }
                }
            }
        }
    });
}

bool FenceManager::eventFilter(QObject *watched, QEvent *event)
{
    if (watched->objectName() == "menuItemLabel") {
        if (event->type() == QEvent::MouseButtonRelease) {
            QLabel *label = qobject_cast<QLabel*>(watched);
            if (!label) return QObject::eventFilter(watched, event);
            
            QString text = label->text();
            
            // 先关闭菜单
            m_trayMenu->close();
            
            // 根据文本直接执行对应操作
            if (text == "新建围栏") {
                QTimer::singleShot(10, this, [this]() {
                    onNewFenceRequested();
                });
            } else if (text == "显示/隐藏全部围栏") {
                QTimer::singleShot(10, this, [this]() {
                    if (m_fencesVisible) {
                        hideAllFences();
                    } else {
                        showAllFences();
                    }
                });
            } else if (text == "关于 DeskGo") {
                QTimer::singleShot(10, this, []() {
                    QMessageBox::about(nullptr, "关于 DeskGo",
                        "<div style='text-align: center; font-family: Microsoft YaHei;'>"
                        "<h3>DeskGo</h3>"
                        "<p>桌面围栏管理工具</p>"
                        "<p>版本: 1.0.1</p>"
                        "</div>");
                });
            } else if (text == "退出应用") {
                QTimer::singleShot(10, this, [this]() {
                    onExitRequested();
                });

            } else if (text.contains("图标文字")) {
                // 处理图标文字显示的切换
                QTimer::singleShot(10, this, []() {
                    bool current = ConfigManager::instance()->iconTextVisible();
                    ConfigManager::instance()->setIconTextVisible(!current);
                });
            }
            
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

FenceWindow* FenceManager::createFence(const QString &title)
{
    FenceWindow *fence = new FenceWindow(title);
    fence->setWindowIcon(m_trayIcon->icon()); // 设置窗口图标
    fence->move(getNewFencePosition());
    
    connect(fence, &FenceWindow::deleteRequested, 
            this, &FenceManager::onFenceDeleteRequested);
    connect(fence, &FenceWindow::geometryChanged, 
            this, &FenceManager::saveFences);
    connect(fence, &FenceWindow::titleChanged,
            this, &FenceManager::saveFences);
    connect(fence, &FenceWindow::collapsedChanged,
            this, &FenceManager::saveFences);
    
    m_fences.append(fence);
    fence->show();
    
    saveFences();
    return fence;
}

void FenceManager::removeFence(FenceWindow *fence)
{
    if (fence && m_fences.contains(fence)) {
        // 先归还所有图标到桌面
        fence->restoreAllIcons();
        
        m_fences.removeOne(fence);
        fence->close();
        fence->deleteLater();
        saveFences();
    }
}

void FenceManager::saveFences()
{
    QJsonArray fencesArray;
    for (FenceWindow *fence : m_fences) {
        fencesArray.append(fence->toJson());
    }

    QJsonObject data;
    data["fences"] = fencesArray;
    ConfigManager::instance()->setFencesData(data);
}

void FenceManager::loadFences()
{
    QJsonObject data = ConfigManager::instance()->fencesData();
    QJsonArray fencesArray = data["fences"].toArray();

    for (const QJsonValue &val : fencesArray) {
        QJsonObject fenceObj = val.toObject();
        FenceWindow *fence = FenceWindow::fromJson(fenceObj);
        
        // Debug output for geometry
        qDebug() << "Loaded fence:" << fence->title() 
                 << "Geometry:" << fence->geometry() 
                 << "JSON:" << fenceObj["x"].toInt() << fenceObj["y"].toInt();

        // 预防坐标为 (0,0) 的情况（可能是保存失败或错误的初值）
        if (fence->x() == 0 && fence->y() == 0) {
            fence->move(getNewFencePosition());
            saveFences(); // 保存修正后的位置
        }
        
        // 关键：恢复时同样要设置图标
        if (m_trayIcon) {
            fence->setWindowIcon(m_trayIcon->icon());
        }
        
        connect(fence, &FenceWindow::deleteRequested, 
                this, &FenceManager::onFenceDeleteRequested);
        connect(fence, &FenceWindow::geometryChanged, 
                this, &FenceManager::saveFences);
        connect(fence, &FenceWindow::titleChanged,
                this, &FenceManager::saveFences);
        connect(fence, &FenceWindow::collapsedChanged,
                this, &FenceManager::saveFences);
        
        // 连接首次显示完成信号
        connect(fence, &FenceWindow::firstShowCompleted, this, [this, fence]() {
            qDebug() << "[FenceManager] First show completed for:" << fence->title();
        });
        
        m_fences.append(fence);
    }
}

void FenceManager::showAllFences()
{
    for (FenceWindow *fence : m_fences) {
        fence->setUserHidden(false);
        fence->show();
    }
    m_fencesVisible = true;
}

void FenceManager::hideAllFences()
{
    for (FenceWindow *fence : m_fences) {
        fence->setUserHidden(true);
        fence->hide();
    }
    m_fencesVisible = false;
}

QPoint FenceManager::getNewFencePosition() const
{
    // 计算新围栏位置（错开显示）
    int offset = m_fences.count() * 40;
    QScreen *screen = QApplication::primaryScreen();
    QRect screenRect = screen->availableGeometry();
    
    int x = 100 + (offset % (screenRect.width() - 400));
    int y = 100 + (offset % (screenRect.height() - 300));
    
    return QPoint(x, y);
}

void FenceManager::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    Q_UNUSED(reason)
    // 双击托盘图标不再切换围栏显示/隐藏，避免误操作
    // 如需隐藏围栏，请使用右键菜单
}

void FenceManager::onFenceDeleteRequested(FenceWindow *fence)
{
    removeFence(fence);
}

void FenceManager::onNewFenceRequested()
{
    createFence("新围栏");
}

void FenceManager::onSettingsRequested()
{
    // 设置对话框（可扩展）
}

void FenceManager::onExitRequested()
{
    shutdown();
    QApplication::quit();
}
