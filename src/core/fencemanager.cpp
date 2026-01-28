#include "fencemanager.h"
#include "../ui/fencewindow.h"
#include "configmanager.h"

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
    
    // 如果没有围栏，创建一个默认的
    if (m_fences.isEmpty()) {
        createFence("示例围栏");
    }
    
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
    m_trayMenu->setStyleSheet(R"(
        QMenu {
            background: rgba(43, 43, 48, 250); /* 提高透明度，改善文字抗锯齿 */
            border: 1px solid rgba(255, 255, 255, 0.12);
            border-radius: 12px;
            padding: 8px;
        }
        QMenu::item {
            color: #ffffff;
            font-size: 13px;
            font-weight: bold;
            font-family: "Microsoft YaHei", "Segoe UI";
            padding: 8px 30px;
            border-radius: 6px;
        }
        QMenu::item:selected {
            background: rgba(255, 255, 255, 0.15);
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
            background: rgba(255, 255, 255, 0.15);
        }
        QMenu::separator {
            height: 1px;
            background: rgba(255, 255, 255, 0.1);
            margin: 6px 12px;
        }
    )");

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
        if (m_fencesVisible) hideAllFences(); else showAllFences();
    });

    m_trayMenu->addSeparator();

    // 修复标准项颜色并尽量通过空格平衡视觉
    QAction *autoStartAction = m_trayMenu->addAction("        开机自启动        ");
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
}

bool FenceManager::eventFilter(QObject *watched, QEvent *event)
{
    if (watched->objectName() == "menuItemLabel") {
        if (event->type() == QEvent::MouseButtonRelease) {
            // 当点击自定义标签时，关闭菜单并执行对应的 Action
            m_trayMenu->close();
            QLabel *label = qobject_cast<QLabel*>(watched);
            // 查找到该 Label 关联的 Action
            for (QAction *action : m_trayMenu->actions()) {
                QWidgetAction *wa = qobject_cast<QWidgetAction*>(action);
                if (wa && wa->defaultWidget() == label) {
                    wa->trigger();
                    return true;
                }
            }
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
        
        m_fences.append(fence);
    }
}

void FenceManager::showAllFences()
{
    for (FenceWindow *fence : m_fences) {
        fence->show();
    }
    m_fencesVisible = true;
}

void FenceManager::hideAllFences()
{
    for (FenceWindow *fence : m_fences) {
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
