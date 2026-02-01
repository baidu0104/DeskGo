#include "mainwindow.h"
#include "titlebar.h"
#include "fencewidget.h"
#include "../platform/blurhelper.h"
#include "../core/configmanager.h"

#include <QApplication>
#include <QCloseEvent>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QJsonArray>
#include <QMessageBox>
#include <QStyle>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    setupTrayIcon();
    setupBlurEffect();
    restoreWindowState();
    loadFences();
}

MainWindow::~MainWindow()
{
    saveFences();
    saveWindowState();
}

void MainWindow::setupUi()
{
    // 无边框窗口
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumSize(600, 400);
    resize(900, 650);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // 标题栏
    m_titleBar = new TitleBar(this);
    m_titleBar->setTitle("DeskGo - 桌面图标管理");
    connect(m_titleBar, &TitleBar::minimizeRequested, this, &MainWindow::onMinimizeRequested);
    connect(m_titleBar, &TitleBar::maximizeRequested, this, &MainWindow::onMaximizeRequested);
    connect(m_titleBar, &TitleBar::closeRequested, this, &MainWindow::onCloseRequested);
    m_mainLayout->addWidget(m_titleBar);

    // 中央区域
    m_centralWidget = new QWidget(this);
    m_centralWidget->setObjectName("centralWidget");
    m_mainLayout->addWidget(m_centralWidget, 1);

    // 工具栏区域
    auto *toolbarLayout = new QHBoxLayout();
    toolbarLayout->setContentsMargins(16, 8, 16, 8);
    toolbarLayout->setSpacing(12);

    auto *btnNewFence = new QPushButton("+ 新建围栏", this);
    btnNewFence->setObjectName("btnNewFence");
    btnNewFence->setFixedHeight(36);
    btnNewFence->setCursor(Qt::PointingHandCursor);
    connect(btnNewFence, &QPushButton::clicked, this, &MainWindow::onNewFenceRequested);
    toolbarLayout->addWidget(btnNewFence);

    auto *btnSettings = new QPushButton("⚙ 设置", this);
    btnSettings->setObjectName("btnSettings");
    btnSettings->setFixedHeight(36);
    btnSettings->setCursor(Qt::PointingHandCursor);
    connect(btnSettings, &QPushButton::clicked, this, &MainWindow::onSettingsRequested);
    toolbarLayout->addWidget(btnSettings);

    toolbarLayout->addStretch();

    auto *statusLabel = new QLabel("", this);
    statusLabel->setObjectName("statusLabel");
    toolbarLayout->addWidget(statusLabel);

    auto *bottomBar = new QWidget(this);
    bottomBar->setLayout(toolbarLayout);
    bottomBar->setFixedHeight(52);
    m_mainLayout->addWidget(bottomBar);

    // 样式表
    setStyleSheet(R"(
        #centralWidget {
            background: transparent;
        }
        #btnNewFence, #btnSettings {
            background: rgba(255, 255, 255, 0.08);
            border: 1px solid rgba(255, 255, 255, 0.12);
            border-radius: 8px;
            color: #ffffff;
            font-size: 13px;
            padding: 0 16px;
        }
        #btnNewFence:hover, #btnSettings:hover {
            background: rgba(255, 255, 255, 0.15);
            border-color: rgba(255, 255, 255, 0.2);
        }
        #btnNewFence:pressed, #btnSettings:pressed {
            background: rgba(255, 255, 255, 0.05);
        }
        #statusLabel {
            color: rgba(255, 255, 255, 0.5);
            font-size: 12px;
        }
    )");
}

void MainWindow::setupTrayIcon()
{
    m_trayIcon = new QSystemTrayIcon(this);
    
    // 获取应用程序目录 (例如 .../bin)
    QString appDir = QCoreApplication::applicationDirPath();
    
    // 构建可能的图标路径列表
    QStringList iconPaths = {
        ":/icons/app.png",           // 1. 优先尝试资源 (最快，如果编译成功)
        "app.png",                   // 2. 工作目录
        appDir + "/app.png",         // 3. exe 同级目录
        appDir + "/../resources/icons/app.png", // 4. 开发环境常见结构 (bin/../resources)
        appDir + "/../../resources/icons/app.png", // 5. Shadow build 常见结构
        ":/icons/app.ico"            // 6. 备用 ICO 资源
    };
    
    QString debugInfo = "Debug Info:\n";
    debugInfo += "AppDir: " + appDir + "\n";
    
    QIcon icon;
    for (const QString &path : iconPaths) {
        bool exists = QFile::exists(path);
        debugInfo += "Path: " + path + " [" + (exists ? "FOUND" : "MISSING") + "]";
        
        if (exists) {
            QIcon tempIcon(path);
            if (!tempIcon.isNull()) {
                icon = tempIcon;
                debugInfo += " -> LOADED OK\n";
                break;
            } else {
                debugInfo += " -> LOAD FAIL\n";
            }
        } else {
            debugInfo += "\n";
        }
    }
    
    if (icon.isNull()) {
        qDebug() << "All custom icons failed, using default system icon.";
        icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
        debugInfo += "Fallback to System Icon";
    }
    
    // 显示调试信息 (调试完成后可删除)
    QMessageBox::information(this, "Icon Debug", debugInfo);
    
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip("DeskGo - 桌面图标管理");
    
    // 同时设置应用程序窗口图标和标题栏图标
    setWindowIcon(icon);
    if (m_titleBar) {
        m_titleBar->setIcon(icon.pixmap(24, 24));
    }

    m_trayMenu = new QMenu(this);
    // 启用透明背景
    m_trayMenu->setAttribute(Qt::WA_TranslucentBackground);
    m_trayMenu->setAttribute(Qt::WA_NoSystemBackground);
    // 给边框留出 1px 的边距，防止边缘毛刺
    m_trayMenu->setContentsMargins(1, 1, 1, 1);
    
    // 使用标准 Popup
    m_trayMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    
    // 半透明背景及样式
    m_trayMenu->setStyleSheet(R"(
        QMenu {
            background-color: rgba(45, 45, 50, 240);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 8px;
            padding: 4px;
            font-family: "Microsoft YaHei", "Segoe UI";
            font-size: 13px;
        }
        QMenu::item {
            background: transparent;
            color: #ffffff;
            padding: 6px 16px;
            border-radius: 4px;
            margin: 2px 4px;
        }
        QMenu::item:selected {
            background-color: rgba(255, 255, 255, 0.1);
        }
        QMenu::separator {
            height: 1px;
            background: rgba(255, 255, 255, 0.1);
            margin: 4px 8px;
        }
    )");

#ifdef Q_OS_WIN
    connect(m_trayMenu, &QMenu::aboutToShow, this, [this]() {
        // 延迟处理以确保窗口句柄已创建
        QTimer::singleShot(10, this, [this]() {
            if (!m_trayMenu) return;

            // 1. 物理裁剪圆角 (解决黑点问题)
            BlurHelper::enableRoundedCorners(m_trayMenu, 8);
            
            // 也是为了解决托盘菜单点击外部不消失的经典 Bug
            // 确保菜单窗口（或其拥有者）在前台
            HWND hMenu = (HWND)m_trayMenu->winId();
            if (hMenu) {
                SetForegroundWindow(hMenu);
            }
        });
    });
#endif

    QAction *showAction = m_trayMenu->addAction("显示主窗口");
    connect(showAction, &QAction::triggered, this, [this]() {
        showNormal();
        activateWindow();
        raise();
    });

    m_trayMenu->addSeparator();

    QAction *autoStartAction = m_trayMenu->addAction("开机自启动");
    autoStartAction->setCheckable(true);
    autoStartAction->setChecked(ConfigManager::instance()->autoStart());
    connect(autoStartAction, &QAction::toggled, [](bool checked) {
        ConfigManager::instance()->setAutoStart(checked);
    });

    m_trayMenu->addSeparator();

    QAction *exitAction = m_trayMenu->addAction("退出");
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

    m_trayIcon->setContextMenu(m_trayMenu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, 
            this, &MainWindow::onTrayIconActivated);

    m_trayIcon->show();
}

void MainWindow::setupBlurEffect()
{
    // 启用毛玻璃效果
    BlurHelper::enableBlur(this, QColor(30, 30, 35, 200), BlurHelper::Acrylic);
}

void MainWindow::loadFences()
{
    QJsonObject data = ConfigManager::instance()->fencesData();
    QJsonArray fencesArray = data["fences"].toArray();

    if (fencesArray.isEmpty()) {
        // 创建默认围栏
        auto *fence = new FenceWidget("示例围栏", m_centralWidget);
        fence->move(50, 50);
        fence->show();
        connect(fence, &FenceWidget::deleteRequested, this, &MainWindow::onFenceDeleteRequested);
        m_fences.append(fence);
    } else {
        for (const QJsonValue &val : fencesArray) {
            QJsonObject fenceObj = val.toObject();
            auto *fence = new FenceWidget(fenceObj["title"].toString(), m_centralWidget);
            fence->setGeometry(
                fenceObj["x"].toInt(),
                fenceObj["y"].toInt(),
                fenceObj["width"].toInt(),
                fenceObj["height"].toInt()
            );
            fence->setCollapsed(fenceObj["collapsed"].toBool());
            fence->show();
            connect(fence, &FenceWidget::deleteRequested, this, &MainWindow::onFenceDeleteRequested);
            m_fences.append(fence);
        }
    }

    updateStatusLabel();
}

void MainWindow::saveFences()
{
    QJsonArray fencesArray;
    for (FenceWidget *fence : m_fences) {
        QJsonObject fenceObj;
        fenceObj["title"] = fence->title();
        fenceObj["x"] = fence->x();
        fenceObj["y"] = fence->y();
        fenceObj["width"] = fence->width();
        fenceObj["height"] = fence->height();
        fenceObj["collapsed"] = fence->isCollapsed();
        fencesArray.append(fenceObj);
    }

    QJsonObject data;
    data["fences"] = fencesArray;
    ConfigManager::instance()->setFencesData(data);
}

void MainWindow::saveWindowState()
{
    ConfigManager::instance()->setWindowGeometry(geometry());
    ConfigManager::instance()->setWindowMaximized(isMaximized());
    ConfigManager::instance()->save();
}

void MainWindow::restoreWindowState()
{
    QRect savedGeometry = ConfigManager::instance()->windowGeometry();
    if (savedGeometry.isValid()) {
        setGeometry(savedGeometry);
    }
    if (ConfigManager::instance()->windowMaximized()) {
        showMaximized();
    }
}

void MainWindow::updateStatusLabel()
{
    int iconCount = 0;
    for (FenceWidget *fence : m_fences) {
        iconCount += fence->icons().count();
    }

    QString status = QString("已管理: %1 个图标 | %2 个围栏")
                         .arg(iconCount)
                         .arg(m_fences.count());
    
    if (auto *label = findChild<QLabel*>("statusLabel")) {
        label->setText(status);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (ConfigManager::instance()->minimizeToTray() && m_trayIcon->isVisible()) {
        hide();
        event->ignore();
    } else {
        saveFences();
        saveWindowState();
        event->accept();
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        // 窗口状态改变时更新最大化按钮图标
    }
    QWidget::changeEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 确保毛玻璃效果生效
    setupBlurEffect();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
}

void MainWindow::onMinimizeRequested()
{
    showMinimized();
}

void MainWindow::onMaximizeRequested()
{
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
}

void MainWindow::onCloseRequested()
{
    close();
}

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick) {
        if (isVisible()) {
            hide();
        } else {
            showNormal();
            activateWindow();
            raise();
        }
    }
}

void MainWindow::onNewFenceRequested()
{
    auto *fence = new FenceWidget("新围栏", m_centralWidget);
    
    // 计算新围栏位置（错开显示）
    int offset = m_fences.count() * 30;
    fence->move(80 + offset, 80 + offset);
    fence->show();
    
    connect(fence, &FenceWidget::deleteRequested, this, &MainWindow::onFenceDeleteRequested);
    m_fences.append(fence);
    
    updateStatusLabel();
    saveFences();
}

void MainWindow::onSettingsRequested()
{
    // 简易设置菜单
    QMenu menu(this);
    // 启用透明背景
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setAttribute(Qt::WA_NoSystemBackground);
    // 给边框留出 1px 的边距，防止边缘毛刺
    menu.setContentsMargins(1, 1, 1, 1);
    
    // 使用标准 Popup
    menu.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    
    // 复用样式
    menu.setStyleSheet(m_trayMenu->styleSheet());

#ifdef Q_OS_WIN
    connect(&menu, &QMenu::aboutToShow, this, [this, &menu]() {
        // 延迟处理
        QTimer::singleShot(10, this, [this, &menu]() {
            // 1. 物理裁剪圆角 (解决黑点问题)
            BlurHelper::enableRoundedCorners(&menu, 8);
            
            // 确保能点击外部关闭
            HWND hMenu = (HWND)menu.winId();
            if (hMenu) {
                SetForegroundWindow(hMenu);
            }
        });
    });
#endif

    QAction *autoStartAction = menu.addAction("开机自启动");
    autoStartAction->setCheckable(true);
    autoStartAction->setChecked(ConfigManager::instance()->autoStart());
    connect(autoStartAction, &QAction::toggled, [](bool checked) {
        ConfigManager::instance()->setAutoStart(checked);
    });

    QAction *minimizeToTrayAction = menu.addAction("关闭时最小化到托盘");
    minimizeToTrayAction->setCheckable(true);
    minimizeToTrayAction->setChecked(ConfigManager::instance()->minimizeToTray());
    connect(minimizeToTrayAction, &QAction::toggled, [](bool checked) {
        ConfigManager::instance()->setMinimizeToTray(checked);
    });

    menu.addSeparator();

    QAction *aboutAction = menu.addAction("关于 DeskGo");
    connect(aboutAction, &QAction::triggered, [this]() {
        QMessageBox::about(this, "关于 DeskGo",
            "<h3>DeskGo</h3>"
            "<p>桌面图标管理工具</p>"
            "<p>版本: 1.0.0</p>"
            "<p>一款美观、高效的 Windows 桌面图标管理工具</p>");
    });

    // 获取设置按钮位置
    if (auto *btn = findChild<QPushButton*>("btnSettings")) {
        menu.exec(btn->mapToGlobal(QPoint(0, -menu.sizeHint().height() - 8)));
    }
}

void MainWindow::onFenceDeleteRequested()
{
    FenceWidget *fence = qobject_cast<FenceWidget*>(sender());
    if (fence) {
        m_fences.removeOne(fence);
        fence->deleteLater();
        updateStatusLabel();
        saveFences();
    }
}
