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
    
    // 注意：不要在这里再次保存，因为可能没有足够的时间完成
    // 所有的更改都应该在实时保存中完成

    
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

    // ---- 围栏管理 ----
    QMenu *fenceMenu = new QMenu("      围栏管理      ", m_trayMenu);
    fenceMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    fenceMenu->setAttribute(Qt::WA_TranslucentBackground);
    fenceMenu->setAttribute(Qt::WA_NoSystemBackground);
    fenceMenu->setContentsMargins(1, 1, 1, 1);
    
    fenceMenu->setStyleSheet(R"(
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
            padding: 8px 16px;
            border-radius: 6px;
            margin: 2px 4px;
        }
        QMenu::item:selected {
            background-color: rgba(255, 255, 255, 0.1);
        }
    )");

    QAction *newFenceAction = fenceMenu->addAction("  新建围栏  ");
    connect(newFenceAction, &QAction::triggered, this, [this](){
        QTimer::singleShot(10, this, [this]() {
            onNewFenceRequested();
        });
    });

    QAction *toggleFencesAction = fenceMenu->addAction(m_fencesVisible ? "  隐藏全部围栏  " : "  显示全部围栏  ");
    connect(toggleFencesAction, &QAction::triggered, this, [this, toggleFencesAction](){
        QTimer::singleShot(50, this, [this, toggleFencesAction]() {
            if (m_fencesVisible) {
                hideAllFences();
            } else {
                showAllFences();
            }
            if (toggleFencesAction) {
                toggleFencesAction->setText(m_fencesVisible ? "  隐藏全部围栏  " : "  显示全部围栏  ");
            }
        });
    });

    QAction *toggleTextAction = fenceMenu->addAction(ConfigManager::instance()->iconTextVisible() ? "  隐藏图标文字  " : "  显示图标文字  ");
    connect(toggleTextAction, &QAction::triggered, this, [](){
        QTimer::singleShot(10, []() {
            bool current = ConfigManager::instance()->iconTextVisible();
            ConfigManager::instance()->setIconTextVisible(!current);
        });
    });

    connect(fenceMenu, &QMenu::aboutToShow, this, [fenceMenu, toggleFencesAction, this]() {
        if (toggleFencesAction) {
            toggleFencesAction->setText(m_fencesVisible ? "  隐藏全部围栏  " : "  显示全部围栏  ");
        }
#ifdef Q_OS_WIN
        QTimer::singleShot(10, fenceMenu, [fenceMenu]() {
            if (!fenceMenu) return;
            BlurHelper::enableRoundedCorners(fenceMenu, 12);
            HWND hMenu = (HWND)fenceMenu->winId();
            if (hMenu) {
                SetForegroundWindow(hMenu);
            }
        });
#endif
    });

    m_trayMenu->addMenu(fenceMenu);

    m_trayMenu->addSeparator();

    // ---- 围栏数据备份/还原 ----
    // 由于是二级菜单，通过添加前导和后置空格实现近似居中效果
    QMenu *dataMenu = new QMenu("      数据管理      ", m_trayMenu);
    
    // 修复二级菜单的黑角问题
    dataMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    dataMenu->setAttribute(Qt::WA_TranslucentBackground);
    dataMenu->setAttribute(Qt::WA_NoSystemBackground);
    dataMenu->setContentsMargins(1, 1, 1, 1);
    
    // 为二级菜单应用和主菜单一致的独立圆角风格
    dataMenu->setStyleSheet(R"(
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
            padding: 8px 16px;
            border-radius: 6px;
            margin: 2px 4px;
        }
        QMenu::item:selected {
            background-color: rgba(255, 255, 255, 0.1);
        }
    )");

#ifdef Q_OS_WIN
    connect(dataMenu, &QMenu::aboutToShow, this, [dataMenu]() {
        QTimer::singleShot(10, dataMenu, [dataMenu]() {
            if (!dataMenu) return;
            BlurHelper::enableRoundedCorners(dataMenu, 12);
            HWND hMenu = (HWND)dataMenu->winId();
            if (hMenu) {
                SetForegroundWindow(hMenu);
            }
        });
    });
#endif
    
    // 为二级菜单项也添加空格保持居中对齐感
    QAction *backupAction = dataMenu->addAction("  备份围栏  ");
    connect(backupAction, &QAction::triggered, this, [this](){
        QTimer::singleShot(10, this, [this]() {
            onBackupFencesRequested();
        });
    });
    
    QAction *restoreAction = dataMenu->addAction("  还原围栏  ");
    connect(restoreAction, &QAction::triggered, this, [this](){
        QTimer::singleShot(10, this, [this]() {
            onRestoreFencesRequested();
        });
    });
    
    m_trayMenu->addMenu(dataMenu);

    m_trayMenu->addSeparator();

    // 修复标准项颜色并尽量通过空格平衡视觉
    QAction *autoStartAction = m_trayMenu->addAction("开机自启");
    autoStartAction->setCheckable(true);
    autoStartAction->setChecked(ConfigManager::instance()->autoStart());
    connect(autoStartAction, &QAction::toggled, [](bool checked) {
        ConfigManager::instance()->setAutoStart(checked);
    });

    m_trayMenu->addSeparator();

    addCenteredAction("关于 DeskGo", [this]() { onAboutRequested(); });
    addCenteredAction("退出应用", [this](){ onExitRequested(); });

    m_trayIcon->setContextMenu(m_trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, 
            this, &FenceManager::onTrayIconActivated);

    m_trayIcon->show();

    // 监听配置变化，实时更新所有围栏
    // 监听配置变化，实时更新所有围栏
    connect(ConfigManager::instance(), &ConfigManager::iconTextVisibleChanged, this, [this, toggleTextAction](bool visible) {
        for (FenceWindow *fence : m_fences) {
            if (fence) fence->setIconTextVisible(visible);
        }
        
        // 更新菜单项文字
        if (toggleTextAction) {
            toggleTextAction->setText(visible ? "  隐藏图标文字  " : "  显示图标文字  ");
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
            if (text == "关于 DeskGo") {
                QTimer::singleShot(10, this, [this]() {
                    onAboutRequested();
                });
            } else if (text == "退出应用") {
                QTimer::singleShot(10, this, [this]() {
                    onExitRequested();
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
        connect(fence, &FenceWindow::firstShowCompleted, this, [fence]() {
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

    // 构建 PowerShell 命令：把两个条目都打包进 zip
    // Compress-Archive 支持多路径输入（数组）
    QString psCmd = QString(
        "$items = @(); "
        "if (Test-Path '%1') { $items += '%1' }; "
        "if (Test-Path '%2') { $items += '%2' }; "
        "if ($items.Count -gt 0) { Compress-Archive -Path $items -DestinationPath '%3' -Force } "
        "else { Write-Error 'No source files found' }"
    ).arg(fencesJson, fencesStorage, savePath);

    QProcess proc;
    proc.setProgram("powershell.exe");
    proc.setArguments({"-NonInteractive", "-NoProfile", "-Command", psCmd});
    proc.start();
    proc.waitForFinished(30000); // 最多等 30 秒

    if (proc.exitCode() == 0 && QFile::exists(savePath)) {
        QMessageBox::information(nullptr, "备份成功", "围栏数据已成功备份。");
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
