#include "fencemanager.h"
#include "../ui/fencewindow.h"
#include "configmanager.h"
#include "../platform/blurhelper.h"

#include <QApplication>
#include <QScreen>
#include <QJsonArray>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QStyle>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QProcess>
#include <QDateTime>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDebug>
#include <QWidgetAction>
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

    // ── 监听显示器配置变化 ──────────────────────────────────────────
    // 连接已存在屏幕的几何变化信号
    for (QScreen *screen : QApplication::screens()) {
        connect(screen, &QScreen::geometryChanged,
                this, &FenceManager::onScreenConfigChanged);
    }
    // 新接入显示器时，连接其信号并触发一次检查
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
        connect(screen, &QScreen::geometryChanged,
                this, &FenceManager::onScreenConfigChanged);
        onScreenConfigChanged();
    });
    // 拔出显示器时触发检查
    connect(qApp, &QGuiApplication::screenRemoved,
            this, &FenceManager::onScreenConfigChanged);
    // 主屏切换时触发检查
    connect(qApp, &QGuiApplication::primaryScreenChanged,
            this, &FenceManager::onScreenConfigChanged);
}

void FenceManager::shutdown()
{
    if (m_isShutdown) return;
    m_isShutdown = true;    
    // 先停止所有围栏的保存定时器，并立即触发保存
    for (FenceWindow *fence : m_fences) {
        if (fence) {
            fence->flushPendingSave();
        }
    }
    
    // 等待一小段时间，确保所有信号都被处理
    QCoreApplication::processEvents();

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
    
    // 强制同步所有配置到磁盘
    ConfigManager::instance()->sync();
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
    m_trayMenu->setContentsMargins(1, 1, 1, 1);
    m_trayMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);

    // 统一样式表
    const QString kMenuStyle = R"(
        QMenu {
            background-color: rgba(45, 45, 50, 240);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 12px;
            padding: 8px;
        }
        QMenu::item {
            background: transparent;
            color: #ffffff;
            padding: 8px 24px;
            border-radius: 6px;
            margin: 2px 4px;
            font-family: "Microsoft YaHei", "Segoe UI";
            font-size: 13px;
        }
        QMenu::item:selected {
            background-color: rgba(255, 255, 255, 0.1);
        }
        QMenu::separator {
            height: 1px;
            background: rgba(255, 255, 255, 0.1);
            margin: 6px 12px;
        }
        QLabel#actionLabel {
            color: #ffffff;
            font-family: "Microsoft YaHei", "Segoe UI";
            font-size: 13px;
            border-radius: 6px;
            padding: 8px 0px;
            background: transparent;
        }
        QLabel#actionLabel:hover {
            background-color: rgba(255, 255, 255, 0.1);
        }
    )";
    m_trayMenu->setStyleSheet(kMenuStyle);

    // 二级菜单专属样式（减少 padding）
    const QString kSubMenuStyle = kMenuStyle + R"(
        QMenu::item {
            padding: 8px 12px;
        }
    )";

#ifdef Q_OS_WIN
    connect(m_trayMenu, &QMenu::aboutToShow, this, [this]() {
        QTimer::singleShot(10, this, [this]() {
            if (!m_trayMenu) return;
            BlurHelper::enableRoundedCorners(m_trayMenu, 12);
            SetForegroundWindow((HWND)m_trayMenu->winId());
        });
    });
#endif

    // 统一创建居中 QWidgetAction 的助手（无需任何 spacer）
    auto addCenteredAction = [&](const QString &text) -> QLabel* {
        QWidgetAction *wa = new QWidgetAction(m_trayMenu);
        QLabel *lbl = new QLabel(text);
        lbl->setObjectName("actionLabel");
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setCursor(Qt::PointingHandCursor);
        lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        lbl->installEventFilter(this);
        wa->setDefaultWidget(lbl);
        m_trayMenu->addAction(wa);
        return lbl;
    };

    // ---- 围栏管理子菜单 ----
    QMenu *fenceMenu = new QMenu("围栏管理", m_trayMenu);
    fenceMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    fenceMenu->setAttribute(Qt::WA_TranslucentBackground);
    fenceMenu->setAttribute(Qt::WA_NoSystemBackground);
    fenceMenu->setContentsMargins(1, 1, 1, 1);
    fenceMenu->setStyleSheet(kSubMenuStyle);

    QAction *newFenceAction     = fenceMenu->addAction("新建围栏");
    QAction *toggleFencesAction = fenceMenu->addAction(m_fencesVisible ? "隐藏全部围栏" : "显示全部围栏");
    QAction *toggleTextAction   = fenceMenu->addAction(ConfigManager::instance()->iconTextVisible() ? "隐藏图标文字" : "显示图标文字");

    connect(newFenceAction, &QAction::triggered, this, [this]() {
        QTimer::singleShot(10, this, [this]() { onNewFenceRequested(); });
    });
    connect(toggleFencesAction, &QAction::triggered, this, [this, toggleFencesAction]() {
        QTimer::singleShot(50, this, [this, toggleFencesAction]() {
            m_fencesVisible ? hideAllFences() : showAllFences();
            if (toggleFencesAction)
                toggleFencesAction->setText(m_fencesVisible ? "隐藏全部围栏" : "显示全部围栏");
        });
    });
    connect(toggleTextAction, &QAction::triggered, this, []() {
        QTimer::singleShot(10, []() {
            ConfigManager::instance()->setIconTextVisible(!ConfigManager::instance()->iconTextVisible());
        });
    });

#ifdef Q_OS_WIN
    connect(fenceMenu, &QMenu::aboutToShow, this, [fenceMenu, toggleFencesAction, this]() {
        if (toggleFencesAction)
            toggleFencesAction->setText(m_fencesVisible ? "隐藏全部围栏" : "显示全部围栏");
        QTimer::singleShot(10, fenceMenu, [fenceMenu]() {
            if (!fenceMenu) return;
            BlurHelper::enableRoundedCorners(fenceMenu, 12);
            SetForegroundWindow((HWND)fenceMenu->winId());
        });
    });
#endif
    m_trayMenu->addMenu(fenceMenu);
    m_trayMenu->addSeparator();

    // ---- 数据管理子菜单 ----
    QMenu *dataMenu = new QMenu("数据管理", m_trayMenu);
    dataMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    dataMenu->setAttribute(Qt::WA_TranslucentBackground);
    dataMenu->setAttribute(Qt::WA_NoSystemBackground);
    dataMenu->setContentsMargins(1, 1, 1, 1);
    dataMenu->setStyleSheet(kSubMenuStyle);

    QAction *backupAction  = dataMenu->addAction("备份围栏");
    QAction *restoreAction = dataMenu->addAction("还原围栏");
    connect(backupAction,  &QAction::triggered, this, [this]() {
        QTimer::singleShot(10, this, [this]() { onBackupFencesRequested(); });
    });
    connect(restoreAction, &QAction::triggered, this, [this]() {
        QTimer::singleShot(10, this, [this]() { onRestoreFencesRequested(); });
    });

#ifdef Q_OS_WIN
    connect(dataMenu, &QMenu::aboutToShow, this, [dataMenu]() {
        QTimer::singleShot(10, dataMenu, [dataMenu]() {
            if (!dataMenu) return;
            BlurHelper::enableRoundedCorners(dataMenu, 12);
            SetForegroundWindow((HWND)dataMenu->winId());
        });
    });
#endif
    m_trayMenu->addMenu(dataMenu);
    m_trayMenu->addSeparator();

    // ---- 开机自启：三列布局确保文字永远处于菜单正中心 ----
    QWidgetAction *autoStartWa = new QWidgetAction(m_trayMenu);
    QWidget *autoStartWidget = new QWidget();
    autoStartWidget->setObjectName("actionLabel");
    autoStartWidget->setCursor(Qt::PointingHandCursor);
    autoStartWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    autoStartWidget->installEventFilter(this);

    QHBoxLayout *asLayout = new QHBoxLayout(autoStartWidget);
    asLayout->setContentsMargins(0, 8, 0, 8);
    asLayout->setSpacing(0);

    QLabel *checkLbl = new QLabel();
    checkLbl->setFixedWidth(20);
    checkLbl->setAlignment(Qt::AlignCenter);
    checkLbl->setStyleSheet("color: #4CAF50; font-size: 13px; background: transparent;");
    checkLbl->setAttribute(Qt::WA_TransparentForMouseEvents);

    QLabel *autoTextLbl = new QLabel("开机自启");
    autoTextLbl->setAlignment(Qt::AlignCenter);
    autoTextLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    autoTextLbl->setStyleSheet("color: #ffffff; font-family: \"Microsoft YaHei\",\"Segoe UI\"; font-size: 13px; background: transparent;");
    autoTextLbl->setAttribute(Qt::WA_TransparentForMouseEvents);

    QLabel *balanceLbl = new QLabel();
    balanceLbl->setFixedWidth(20);
    balanceLbl->setAttribute(Qt::WA_TransparentForMouseEvents);

    asLayout->addWidget(checkLbl);
    asLayout->addWidget(autoTextLbl, 1);
    asLayout->addWidget(balanceLbl);

    auto updateAutoStartLabel = [checkLbl](bool checked) {
        checkLbl->setText(checked ? "✔" : "");
    };
    updateAutoStartLabel(ConfigManager::instance()->autoStart());
    connect(ConfigManager::instance(), &ConfigManager::autoStartChanged, this, updateAutoStartLabel);

    autoStartWa->setDefaultWidget(autoStartWidget);
    m_trayMenu->addAction(autoStartWa);

    m_trayMenu->addSeparator();
    addCenteredAction("关于 DeskGo");
    addCenteredAction("退出应用");

    m_trayIcon->setContextMenu(m_trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &FenceManager::onTrayIconActivated);

    m_trayIcon->show();

    // 监听配置变化，实时更新所有围栏
    connect(ConfigManager::instance(), &ConfigManager::iconTextVisibleChanged, this, [this, toggleTextAction](bool visible) {
        for (FenceWindow *fence : m_fences) {
            if (fence) fence->setIconTextVisible(visible);
        }
        if (toggleTextAction)
            toggleTextAction->setText(visible ? "隐藏图标文字" : "显示图标文字");
    });
}

bool FenceManager::eventFilter(QObject *watched, QEvent *event)
{
    if (watched->objectName() == "actionLabel") {
        if (event->type() == QEvent::MouseButtonRelease) {
            m_trayMenu->close();

            if (!qobject_cast<QLabel*>(watched)) {
                ConfigManager::instance()->setAutoStart(!ConfigManager::instance()->autoStart());
                return true;
            }

            QLabel *lbl = qobject_cast<QLabel*>(watched);
            QString text = lbl->text();

            if (text.contains("关于 DeskGo")) {
                QTimer::singleShot(10, this, [this]() { onAboutRequested(); });
            } else if (text.contains("退出应用")) {
                QTimer::singleShot(10, this, [this]() { onExitRequested(); });
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

        // 删除该围栏在 fences_storage 下的存储目录（图标快捷方式等）
        QString fenceId = fence->id();
        QString storagePath = ConfigManager::instance()->fencesStoragePath() + "/" + fenceId;
        QDir storageDir(storagePath);
        if (storageDir.exists()) {
            storageDir.removeRecursively();
            qDebug() << "[FenceManager] Removed storage for fence:" << fenceId;
        }

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
        connect(fence, &FenceWindow::firstShowCompleted, this, [fence]() {
        });
        
        m_fences.append(fence);
    }

    // [修复] 首次启动引导：如果没有载入任何围栏，自动创建一个空围栏
    if (m_fences.isEmpty()) {
        qDebug() << "[FenceManager] No fences detected. Creating initial fence for user guidance.";
        createFence("我的围栏");
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

void FenceManager::onAboutRequested()
{
    QMessageBox::about(nullptr, "关于 DeskGo",
        "<div style='text-align: center; font-family: Microsoft YaHei;'>"
        "<h3>DeskGo</h3>"
        "<p>桌面图标管理工具</p>"
        "<p>版本: " + qApp->applicationVersion() + "</p>"
        "</div>");
}

void FenceManager::onExitRequested()
{
    shutdown();
    QApplication::quit();
}

// ─────────────────────────────────────────────────────────────────
// 围栏数据备份
// 将 fencing_config.json 和 fences_storage 目录打包为 .zip
// 依赖 Windows PowerShell Compress-Archive（无需第三方库）
// ─────────────────────────────────────────────────────────────────
void FenceManager::onBackupFencesRequested()
{
    // 先强制保存当前数据，确保备份的是最新状态
    saveFences();
    ConfigManager::instance()->sync();

    // 弹出保存对话框
    QString defaultName = QString("DeskGo_backup_%1.zip")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString savePath = QFileDialog::getSaveFileName(
        nullptr,
        "备份围栏数据",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/" + defaultName,
        "备份文件 (*.zip)"
    );

    if (savePath.isEmpty()) return;
    if (!savePath.endsWith(".zip", Qt::CaseInsensitive)) savePath += ".zip";

    // 删除已存在的目标文件
    if (QFile::exists(savePath)) QFile::remove(savePath);

    QString configPath    = ConfigManager::instance()->fencesStoragePath();
    // fencesStoragePath: ...AppData/Local/DeskGo/fences_storage
    // fencing_config.json 在其上一级
    QDir storageDir(configPath);
    storageDir.cdUp();
    QString appDataDir = storageDir.absolutePath(); // .../AppData/Local/DeskGo

    QString fencesJson   = appDataDir + "/fencing_config.json";
    QString fencesStorage = appDataDir + "/fences_storage";
    QString userSettings = appDataDir + "/user_settings.ini";

    // 构建 PowerShell 命令：把三个核心条目（配置、图标、样式）都打包进 zip
    QString psCmd = QString(
        "$items = @(); "
        "if (Test-Path '%1') { $items += '%1' }; "
        "if (Test-Path '%2') { $items += '%2' }; "
        "if (Test-Path '%3') { $items += '%3' }; "
        "if ($items.Count -gt 0) { Compress-Archive -Path $items -DestinationPath '%4' -Force } "
        "else { Write-Error 'No source files found' }"
    ).arg(fencesJson, fencesStorage, userSettings, savePath);

    QProcess proc;
    proc.setProgram("powershell.exe");
    proc.setArguments({"-NonInteractive", "-NoProfile", "-Command", psCmd});
    proc.start();
    proc.waitForFinished(30000); // 最多等 30 秒

    if (proc.exitCode() == 0 && QFile::exists(savePath)) {
        QMessageBox::information(nullptr, "备份成功", "围栏数据已成功备份");
    } else {
        QString errMsg = QString::fromUtf8(proc.readAllStandardError());
        QMessageBox::critical(nullptr, "备份失败",
            QString("备份围栏数据时出错：\n%1").arg(errMsg.isEmpty() ? "未知错误" : errMsg));
    }
}

// ─────────────────────────────────────────────────────────────────
// 围栏数据还原
// 从 .zip 中解压并覆盖 AppData 目录下的围栏数据，然后重启应用
// ─────────────────────────────────────────────────────────────────
void FenceManager::onRestoreFencesRequested()
{
    QString zipPath = QFileDialog::getOpenFileName(
        nullptr,
        "还原围栏数据",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
        "备份文件 (*.zip)"
    );

    if (zipPath.isEmpty()) return;
    if (!QFile::exists(zipPath)) {
        QMessageBox::warning(nullptr, "文件不存在", "所选备份文件不存在，请重新选择。");
        return;
    }

    int ret = QMessageBox::warning(
        nullptr,
        "确认还原",
        "还原操作将覆盖当前所有围栏数据，应用随后将自动重启。\n\n确定要继续吗？",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    if (ret != QMessageBox::Yes) return;

    QString configPath    = ConfigManager::instance()->fencesStoragePath();
    QDir storageDir(configPath);
    storageDir.cdUp();
    QString appDataDir = storageDir.absolutePath();

    // 先删除旧数据
    QString oldJson    = appDataDir + "/fencing_config.json";
    QString oldStorage = appDataDir + "/fences_storage";

    // 关键修复：阻止应用内正在进行的任何异步保存写入动作
    // 否则它们可能会在 Expand-Archive 解压之后被写入，覆盖掉我们刚刚还原好的数据！
    ConfigManager::instance()->stopSave();

    // PowerShell：解压到 AppData 目录，自动覆盖
    // Expand-Archive 会把 zip 内容解压到目标目录
    QString psCmd = QString(
        "Expand-Archive -Path '%1' -DestinationPath '%2' -Force"
    ).arg(zipPath, appDataDir);

    QProcess proc;
    proc.setProgram("powershell.exe");
    proc.setArguments({"-NonInteractive", "-NoProfile", "-Command", psCmd});
    proc.start();
    proc.waitForFinished(30000);

    if (proc.exitCode() != 0) {
        QString errMsg = QString::fromUtf8(proc.readAllStandardError());
        QMessageBox::critical(nullptr, "还原失败",
            QString("还原围栏数据时出错：\n%1").arg(errMsg.isEmpty() ? "未知错误" : errMsg));
        return;
    }

    // 验证关键文件存在
    bool jsonOk    = QFile::exists(oldJson);
    bool storageOk = QDir(oldStorage).exists();

    if (!jsonOk && !storageOk) {
        QMessageBox::warning(nullptr, "还原警告",
            "备份文件似乎不包含有效的围栏数据（fencing_config.json 和 fences_storage 均未找到）。\n"
            "请确认选择了正确的 DeskGo 备份文件。");
        return;
    }

    QMessageBox::information(nullptr, "还原成功",
        "围栏数据已成功还原！\n应用即将重启以加载新数据。");

    // ⚠️  关键：不能调用 shutdown()！
    //
    // shutdown() 内部会调用 ConfigManager::sync()，把当前内存中的
    // 旧围栏数据写回磁盘，从而覆盖掉刚刚解压好的 fencing_config.json。
    // 修复方案：直接设置 m_isShutdown 旗标，阻止任何待定保存写入，
    // 然后隐藏托盘图标，启动新进程后让当前进程直接退出即可。
    //
    // 进程退出时操作系统会自动回收所有窗口/句柄，无需手动 close()。

    m_isShutdown = true; // 阻止任何后续的自动保存

    // 停止所有围栏的待定保存定时器
    // 注意：不调用 flushPendingSave()，因为它会 emit geometryChanged()
    // 触发 saveFences() 更新 m_fencesData，有潜在覆盖风险。
    // 此处只需停止定时器，阻止任何写盘动作即可。
    for (FenceWindow *fence : m_fences) {
        if (fence) fence->stopSaveTimer(); // 只停计时器，不触发保存
    }

    // 隐藏托盘图标（让任务栏立即干净）
    if (m_trayIcon) {
        m_trayIcon->hide();
    }

    // 启动新实例
    QString appExe = QCoreApplication::applicationFilePath();
    QProcess::startDetached(appExe, QStringList());

    // 直接退出，不写任何数据到磁盘
    QApplication::quit();
}

// ─────────────────────────────────────────────────────────────────────────────
// 显示器配置变化处理（防抖触发）
// ─────────────────────────────────────────────────────────────────────────────
void FenceManager::onScreenConfigChanged()
{
    // 防抖：500ms 内多次调用只执行一次，避免切换动画期间反复触发
    static QTimer *debounceTimer = nullptr;
    if (!debounceTimer) {
        debounceTimer = new QTimer(this);
        debounceTimer->setSingleShot(true);
        debounceTimer->setInterval(500);
        connect(debounceTimer, &QTimer::timeout, this, &FenceManager::ensureFencesInScreen);
    }
    debounceTimer->start();
}

// ─────────────────────────────────────────────────────────────────────────────
// 确保所有围栏在屏幕可用区域内
// 将超出边界的围栏移回最近的屏幕，保留至少 60px 的可见区域
// ─────────────────────────────────────────────────────────────────────────────
void FenceManager::ensureFencesInScreen()
{
    if (m_isShutdown) return;

    const QList<QScreen*> screens = QApplication::screens();
    if (screens.isEmpty()) return;

    for (FenceWindow *fence : m_fences) {
        if (!fence || !fence->isVisible()) continue;

        QRect fenceRect = fence->geometry();
        QPoint center   = fenceRect.center();

        // 优先找包含围栏中心点的屏幕
        QScreen *targetScreen = QApplication::screenAt(center);

        // 如果中心点不在任何屏幕上（围栏已跑出边界），找最近的屏幕
        if (!targetScreen) {
            int minDist = INT_MAX;
            for (QScreen *s : screens) {
                QRect sa = s->availableGeometry();
                int dx = qMax(0, qMax(sa.left() - center.x(), center.x() - sa.right()));
                int dy = qMax(0, qMax(sa.top()  - center.y(), center.y() - sa.bottom()));
                int dist = dx * dx + dy * dy;
                if (dist < minDist) {
                    minDist = dist;
                    targetScreen = s;
                }
            }
        }

        if (!targetScreen) continue;

        QRect  sa     = targetScreen->availableGeometry();
        QPoint newPos = fenceRect.topLeft();
        bool   moved  = false;

        // 至少保留 60px 可见区域，防止围栏完全跑出屏幕导致无法拖回
        const int vis = 60;

        if (newPos.x() + fenceRect.width() < sa.left() + vis) {
            newPos.setX(sa.left());
            moved = true;
        } else if (newPos.x() > sa.right() - vis) {
            newPos.setX(sa.right() - fenceRect.width());
            moved = true;
        }

        if (newPos.y() < sa.top()) {
            newPos.setY(sa.top());
            moved = true;
        } else if (newPos.y() > sa.bottom() - vis) {
            newPos.setY(sa.bottom() - vis);
            moved = true;
        }

        if (moved) {
            fence->move(newPos);
            emit fence->geometryChanged(); // 触发位置保存
        }
    }
}
