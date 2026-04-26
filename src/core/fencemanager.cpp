#include "fencemanager.h"
#include "../ui/fencewindow.h"
#include "configmanager.h"
#include "iconhelper.h"
#include "../platform/blurhelper.h"

#include <QApplication>
#include <QScreen>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
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
#include <QCryptographicHash>
#include <QDebug>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QWidgetAction>
#include <QEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QPainter>
#include <QPixmap>
#include <QPen>
#include <QColor>
#include <QPointF>


#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
const char *kBackupManifestFileName = "external_icon_manifest.json";
const char *kBackupExternalDirName = "external_icons";

QString normalizeNativePath(const QString &path)
{
    return QDir::toNativeSeparators(QDir::cleanPath(path));
}

bool shouldTreatEntryAsFile(const QFileInfo &entry)
{
    const QString suffix = entry.suffix().toLower();
    if (suffix == "lnk" || suffix == "url") {
        return true;
    }

    return entry.isSymLink();
}

bool copyFileReplacing(const QString &sourcePath, const QString &targetPath, QString *errorMessage)
{
    const QString normalizedSource = normalizeNativePath(sourcePath);
    const QString normalizedTarget = normalizeNativePath(targetPath);

    QFileInfo targetInfo(normalizedTarget);
    QDir().mkpath(targetInfo.absolutePath());

    if (QFile::exists(normalizedTarget) && !QFile::remove(normalizedTarget)) {
        if (errorMessage) {
            *errorMessage = QString("无法覆盖文件：%1").arg(normalizedTarget);
        }
        return false;
    }

    if (!QFile::copy(normalizedSource, normalizedTarget)) {
        if (errorMessage) {
            *errorMessage = QString("复制文件失败：%1 -> %2").arg(normalizedSource, normalizedTarget);
        }
        return false;
    }

    return true;
}

bool copyDirectoryRecursively(const QString &sourceDirPath, const QString &targetDirPath, QString *errorMessage)
{
    QDir sourceDir(normalizeNativePath(sourceDirPath));
    if (!sourceDir.exists()) {
        if (errorMessage) {
            *errorMessage = QString("源目录不存在：%1").arg(sourceDir.absolutePath());
        }
        return false;
    }

    QDir targetDir(normalizeNativePath(targetDirPath));
    if (!targetDir.exists() && !QDir().mkpath(targetDir.absolutePath())) {
        if (errorMessage) {
            *errorMessage = QString("无法创建目录：%1").arg(targetDir.absolutePath());
        }
        return false;
    }

    const QFileInfoList entries = sourceDir.entryInfoList(
        QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System);

    for (const QFileInfo &entry : entries) {
        const QString sourcePath = entry.absoluteFilePath();
        const QString targetPath = normalizeNativePath(targetDir.absoluteFilePath(entry.fileName()));

        if (entry.isDir() && !shouldTreatEntryAsFile(entry)) {
            if (!copyDirectoryRecursively(sourcePath, targetPath, errorMessage)) {
                return false;
            }
            continue;
        }

        if (!copyFileReplacing(sourcePath, targetPath, errorMessage)) {
            return false;
        }
    }

    return true;
}

bool writeJsonObjectToFile(const QString &path, const QJsonObject &jsonObject, QString *errorMessage)
{
    QSaveFile file(normalizeNativePath(path));
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = QString("无法写入文件：%1").arg(path);
        }
        return false;
    }

    const QByteArray jsonData = QJsonDocument(jsonObject).toJson(QJsonDocument::Indented);
    if (file.write(jsonData) != jsonData.size()) {
        if (errorMessage) {
            *errorMessage = QString("写入 JSON 失败：%1").arg(path);
        }
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = QString("提交 JSON 文件失败：%1").arg(path);
        }
        return false;
    }

    return true;
}

bool readJsonObjectFromFile(const QString &path, QJsonObject *jsonObject, QString *errorMessage)
{
    QFile file(normalizeNativePath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QString("无法读取文件：%1").arg(path);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QString("解析 JSON 失败：%1").arg(path);
        }
        return false;
    }

    if (jsonObject) {
        *jsonObject = document.object();
    }
    return true;
}

QString uniqueStorageFileName(const QString &directoryPath, const QString &preferredFileName)
{
    QFileInfo preferredInfo(preferredFileName);
    const QString baseName = preferredInfo.completeBaseName().isEmpty() ? "icon" : preferredInfo.completeBaseName();
    const QString suffix = preferredInfo.suffix();

    QString candidate = preferredInfo.fileName();
    if (candidate.isEmpty()) {
        candidate = suffix.isEmpty() ? baseName : QString("%1.%2").arg(baseName, suffix);
    }

    QDir directory(normalizeNativePath(directoryPath));
    if (!directory.exists()) {
        QDir().mkpath(directory.absolutePath());
    }

    int index = 1;
    while (QFile::exists(normalizeNativePath(directory.absoluteFilePath(candidate)))) {
        candidate = suffix.isEmpty()
            ? QString("%1_%2").arg(baseName).arg(index)
            : QString("%1_%2.%3").arg(baseName).arg(index).arg(suffix);
        ++index;
    }

    return candidate;
}

bool prepareBackupBundle(const QString &appDataDir, const QString &bundleDir, QString *errorMessage, int *bundledExternalCount, int *missingExternalCount)
{
    const QString normalizedAppDataDir = normalizeNativePath(appDataDir);
    const QString normalizedBundleDir = normalizeNativePath(bundleDir);
    const QString fencesJsonPath = normalizeNativePath(normalizedAppDataDir + "/fencing_config.json");
    const QString fencesStoragePath = normalizeNativePath(normalizedAppDataDir + "/fences_storage");
    const QString userSettingsPath = normalizeNativePath(normalizedAppDataDir + "/user_settings.ini");

    if (bundledExternalCount) {
        *bundledExternalCount = 0;
    }
    if (missingExternalCount) {
        *missingExternalCount = 0;
    }

    if (QFile::exists(fencesJsonPath) &&
        !copyFileReplacing(fencesJsonPath, normalizedBundleDir + "/fencing_config.json", errorMessage)) {
        return false;
    }

    if (QDir(fencesStoragePath).exists() &&
        !copyDirectoryRecursively(fencesStoragePath, normalizedBundleDir + "/fences_storage", errorMessage)) {
        return false;
    }

    if (QFile::exists(userSettingsPath) &&
        !copyFileReplacing(userSettingsPath, normalizedBundleDir + "/user_settings.ini", errorMessage)) {
        return false;
    }

    QJsonObject fencesData;
    if (!QFile::exists(fencesJsonPath)) {
        return true;
    }
    if (!readJsonObjectFromFile(fencesJsonPath, &fencesData, errorMessage)) {
        return false;
    }

    const QString storageRoot = normalizeNativePath(ConfigManager::instance()->fencesStoragePath());
    const QString storageRootPrefix = storageRoot.endsWith(QDir::separator())
        ? storageRoot
        : storageRoot + QDir::separator();
    const QJsonArray fencesArray = fencesData.value("fences").toArray();
    QJsonArray manifestArray;

    for (const QJsonValue &fenceValue : fencesArray) {
        const QJsonObject fenceObject = fenceValue.toObject();
        const QString fenceId = fenceObject.value("id").toString();
        const QJsonArray iconsArray = fenceObject.value("icons").toArray();

        for (const QJsonValue &iconValue : iconsArray) {
            const QJsonObject iconObject = iconValue.toObject();
            const QString savedPath = iconObject.value("path").toString();
            if (savedPath.isEmpty() || savedPath.startsWith("storage:", Qt::CaseInsensitive)) {
                continue;
            }

            const QString resolvedPath = normalizeNativePath(IconHelper::fromStoragePath(savedPath, fenceId));
            if (resolvedPath.startsWith(storageRootPrefix, Qt::CaseInsensitive)) {
                continue;
            }

            QFileInfo fileInfo(resolvedPath);
            if (!fileInfo.exists() || !fileInfo.isFile()) {
                if (missingExternalCount) {
                    ++(*missingExternalCount);
                }
                continue;
            }

            const QByteArray hash = QCryptographicHash::hash(
                resolvedPath.toUtf8(), QCryptographicHash::Sha1).toHex();
            const QString bundleFileName = QString("%1_%2").arg(QString::fromLatin1(hash.left(12)), fileInfo.fileName());
            const QString bundleRelativePath = QString("%1/%2").arg(kBackupExternalDirName, bundleFileName);
            const QString bundleAbsolutePath = normalizeNativePath(normalizedBundleDir + "/" + bundleRelativePath);

            if (!copyFileReplacing(resolvedPath, bundleAbsolutePath, errorMessage)) {
                return false;
            }

            QJsonObject manifestObject;
            manifestObject["fenceId"] = fenceId;
            manifestObject["savedPath"] = savedPath;
            manifestObject["bundledPath"] = bundleRelativePath;
            manifestObject["fileName"] = fileInfo.fileName();
            manifestArray.append(manifestObject);

            if (bundledExternalCount) {
                ++(*bundledExternalCount);
            }
        }
    }

    if (manifestArray.isEmpty()) {
        return true;
    }

    QJsonObject manifestRoot;
    manifestRoot["version"] = 1;
    manifestRoot["externalIcons"] = manifestArray;
    return writeJsonObjectToFile(normalizedBundleDir + "/" + kBackupManifestFileName, manifestRoot, errorMessage);
}

bool materializeBundledIconsIntoStorage(const QString &bundleRootDir, QString *errorMessage)
{
    const QString manifestPath = normalizeNativePath(bundleRootDir + "/" + kBackupManifestFileName);
    if (!QFile::exists(manifestPath)) {
        return true;
    }

    const QString fencesJsonPath = normalizeNativePath(bundleRootDir + "/fencing_config.json");
    if (!QFile::exists(fencesJsonPath)) {
        return true;
    }

    QJsonObject manifestRoot;
    if (!readJsonObjectFromFile(manifestPath, &manifestRoot, errorMessage)) {
        return false;
    }

    QJsonObject fencesData;
    if (!readJsonObjectFromFile(fencesJsonPath, &fencesData, errorMessage)) {
        return false;
    }

    QHash<QString, QJsonObject> manifestByKey;
    const QJsonArray manifestArray = manifestRoot.value("externalIcons").toArray();
    for (const QJsonValue &value : manifestArray) {
        const QJsonObject object = value.toObject();
        const QString key = object.value("fenceId").toString() + "|" + object.value("savedPath").toString();
        manifestByKey.insert(key, object);
    }

    const QString storageRoot = normalizeNativePath(bundleRootDir + "/fences_storage");
    QDir().mkpath(storageRoot);

    QJsonArray fencesArray = fencesData.value("fences").toArray();
    for (int fenceIndex = 0; fenceIndex < fencesArray.size(); ++fenceIndex) {
        QJsonObject fenceObject = fencesArray.at(fenceIndex).toObject();
        const QString fenceId = fenceObject.value("id").toString();
        QJsonArray iconsArray = fenceObject.value("icons").toArray();

        for (int iconIndex = 0; iconIndex < iconsArray.size(); ++iconIndex) {
            QJsonObject iconObject = iconsArray.at(iconIndex).toObject();
            const QString savedPath = iconObject.value("path").toString();
            const QString key = fenceId + "|" + savedPath;
            if (!manifestByKey.contains(key)) {
                continue;
            }

            const QJsonObject manifestObject = manifestByKey.value(key);
            const QString bundledPath = normalizeNativePath(bundleRootDir + "/" + manifestObject.value("bundledPath").toString());
            QFileInfo bundledInfo(bundledPath);
            if (!bundledInfo.exists() || !bundledInfo.isFile()) {
                if (errorMessage) {
                    *errorMessage = QString("备份内缺少图标文件：%1").arg(bundledPath);
                }
                return false;
            }

            const QString fenceStorageDir = normalizeNativePath(storageRoot + "/" + fenceId);
            const QString preferredName = manifestObject.value("fileName").toString().isEmpty()
                ? bundledInfo.fileName()
                : manifestObject.value("fileName").toString();
            const QString uniqueFileName = uniqueStorageFileName(fenceStorageDir, preferredName);
            const QString storageTargetPath = normalizeNativePath(fenceStorageDir + "/" + uniqueFileName);

            if (!copyFileReplacing(bundledPath, storageTargetPath, errorMessage)) {
                return false;
            }

            iconObject["path"] = QString("storage:%1").arg(uniqueFileName);
            iconsArray.replace(iconIndex, iconObject);
        }

        fenceObject["icons"] = iconsArray;
        fencesArray.replace(fenceIndex, fenceObject);
    }

    fencesData["fences"] = fencesArray;
    return writeJsonObjectToFile(fencesJsonPath, fencesData, errorMessage);
}
}

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

    // 先停止所有围栏的保存定时器，并立即触发保存
    for (FenceWindow *fence : m_fences) {
        if (fence) {
            fence->flushPendingSave();
        }
    }

    // 强制再执行一次整体保存，确保所有变动落盘
    saveFences();

    m_isShutdown = true;

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
        QLabel#actionLabel, QWidget#actionLabel {
            color: #ffffff;
            font-family: "Microsoft YaHei", "Segoe UI";
            font-size: 13px;
            border-radius: 6px;
            padding: 8px 0px;
            background: transparent;
        }
        QLabel#actionLabel:hover, QWidget#actionLabel:hover {
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

    auto addMainRow = [&](const QString &text, const QString &role) -> QWidgetAction* {
        QWidgetAction *action = new QWidgetAction(m_trayMenu);
        QWidget *widget = new QWidget();
        widget->setObjectName("actionLabel");
        widget->setProperty("actionRole", role);
        widget->setCursor(Qt::PointingHandCursor);
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        widget->installEventFilter(this);

        QHBoxLayout *layout = new QHBoxLayout(widget);
        layout->setContentsMargins(8, 8, 12, 8);
        layout->setSpacing(4);

        QLabel *check = new QLabel();
        check->setFixedSize(16, 16);
        check->setAttribute(Qt::WA_TransparentForMouseEvents);

        QLabel *label = new QLabel(text);
        label->setStyleSheet("color: #ffffff; font-family: \"Microsoft YaHei\",\"Segoe UI\"; font-size: 13px; background: transparent;");
        label->setAttribute(Qt::WA_TransparentForMouseEvents);

        layout->addWidget(check);
        layout->addWidget(label);
        layout->addStretch();

        action->setDefaultWidget(widget);
        m_trayMenu->addAction(action);
        return action;
    };

    const QPixmap checkedPixmap = []() {
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#4CAF50"), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(3.0, 8.5), QPointF(6.5, 12.0));
        painter.drawLine(QPointF(6.5, 12.0), QPointF(13.0, 4.0));
        return pixmap;
    }();
    // ---- 围栏管理子菜单 ----
    QMenu *fenceMenu = new QMenu("围栏管理", m_trayMenu);
    fenceMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    fenceMenu->setAttribute(Qt::WA_TranslucentBackground);
    fenceMenu->setAttribute(Qt::WA_NoSystemBackground);
    fenceMenu->setContentsMargins(1, 1, 1, 1);
    fenceMenu->setStyleSheet(kSubMenuStyle);

    auto addFenceRow = [&](const QString &text, const QString &role, QLabel **checkLabel, QLabel **textLabel, QWidget **rowWidget) -> QWidgetAction* {
        QWidgetAction *action = new QWidgetAction(fenceMenu);
        QWidget *widget = new QWidget();
        widget->setObjectName("actionLabel");
        widget->setProperty("actionRole", role);
        widget->setCursor(Qt::PointingHandCursor);
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        widget->installEventFilter(this);

        QHBoxLayout *layout = new QHBoxLayout(widget);
        layout->setContentsMargins(8, 8, 12, 8);
        layout->setSpacing(4);

        QLabel *check = new QLabel();
        check->setFixedSize(16, 16);
        check->setAlignment(Qt::AlignCenter);
        check->setAttribute(Qt::WA_TransparentForMouseEvents);

        QLabel *label = new QLabel(text);
        label->setStyleSheet("color: #ffffff; font-family: \"Microsoft YaHei\",\"Segoe UI\"; font-size: 13px; background: transparent;");
        label->setAttribute(Qt::WA_TransparentForMouseEvents);

        layout->addWidget(check);
        layout->addWidget(label);
        layout->addStretch();

        action->setDefaultWidget(widget);
        fenceMenu->addAction(action);

        if (checkLabel)
            *checkLabel = check;
        if (textLabel)
            *textLabel = label;
        if (rowWidget)
            *rowWidget = widget;
        return action;
    };

    QLabel *hideAllCheckLbl = nullptr;
    QLabel *lockCheckLbl = nullptr;
    QWidget *newFenceWidget = nullptr;

    QWidgetAction *newFenceAction = addFenceRow("新建围栏", "newFence", nullptr, nullptr, &newFenceWidget);
    addFenceRow("隐藏全部围栏", "toggleFencesAndText", &hideAllCheckLbl, nullptr, nullptr);
    addFenceRow("锁定布局", "layoutLock", &lockCheckLbl, nullptr, nullptr);

    auto updateHideAllAction = [hideAllCheckLbl, checkedPixmap](bool hidden) {
        if (hideAllCheckLbl)
            hideAllCheckLbl->setPixmap(hidden ? checkedPixmap : QPixmap());
    };
    updateHideAllAction(!m_fencesVisible && !ConfigManager::instance()->iconTextVisible());

    auto updateLockAction = [lockCheckLbl, checkedPixmap](bool locked) {
        if (lockCheckLbl)
            lockCheckLbl->setPixmap(locked ? checkedPixmap : QPixmap());
    };
    updateLockAction(ConfigManager::instance()->layoutLocked());
    auto updateNewFenceEnabled = [newFenceAction, newFenceWidget](bool enabled) {
        if (newFenceAction)
            newFenceAction->setEnabled(enabled);
        if (newFenceWidget)
            newFenceWidget->setEnabled(enabled);
    };
    updateNewFenceEnabled(!ConfigManager::instance()->layoutLocked());
#ifdef Q_OS_WIN
    connect(fenceMenu, &QMenu::aboutToShow, this, [fenceMenu, updateHideAllAction, updateLockAction, updateNewFenceEnabled, this]() {
        updateHideAllAction(!m_fencesVisible && !ConfigManager::instance()->iconTextVisible());
        updateLockAction(ConfigManager::instance()->layoutLocked());
        updateNewFenceEnabled(!ConfigManager::instance()->layoutLocked());
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

    // ---- 开机自启 ----
    QWidgetAction *autoStartWa = new QWidgetAction(m_trayMenu);
    QWidget *autoStartWidget = new QWidget();
    autoStartWidget->setObjectName("actionLabel");
    autoStartWidget->setProperty("actionRole", "autoStart");
    autoStartWidget->setCursor(Qt::PointingHandCursor);
    autoStartWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    autoStartWidget->installEventFilter(this);

    QHBoxLayout *asLayout = new QHBoxLayout(autoStartWidget);
    asLayout->setContentsMargins(8, 8, 12, 8);
    asLayout->setSpacing(4);

    QLabel *checkLbl = new QLabel();
    checkLbl->setFixedSize(16, 16);
    checkLbl->setAlignment(Qt::AlignCenter);
    checkLbl->setStyleSheet("background: transparent;");
    checkLbl->setAttribute(Qt::WA_TransparentForMouseEvents);

    QLabel *autoTextLbl = new QLabel("开机自启");
    autoTextLbl->setStyleSheet("color: #ffffff; font-family: \"Microsoft YaHei\",\"Segoe UI\"; font-size: 13px; background: transparent;");
    autoTextLbl->setAttribute(Qt::WA_TransparentForMouseEvents);

    asLayout->addWidget(checkLbl);
    asLayout->addWidget(autoTextLbl);
    asLayout->addStretch();

    auto updateAutoStartLabel = [checkLbl, checkedPixmap](bool checked) {
        checkLbl->setPixmap(checked ? checkedPixmap : QPixmap());
    };
    updateAutoStartLabel(ConfigManager::instance()->autoStart());
    connect(ConfigManager::instance(), &ConfigManager::autoStartChanged, this, updateAutoStartLabel);

    autoStartWa->setDefaultWidget(autoStartWidget);
    m_trayMenu->addAction(autoStartWa);

    m_trayMenu->addSeparator();
    addMainRow("关于", "about");
    addMainRow("退出应用", "exit");

    m_trayIcon->setContextMenu(m_trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &FenceManager::onTrayIconActivated);

    m_trayIcon->show();

    // 监听配置变化，实时更新所有围栏
    connect(ConfigManager::instance(), &ConfigManager::iconTextVisibleChanged, this, [this, updateHideAllAction](bool visible) {
        for (FenceWindow *fence : m_fences) {
            if (fence) fence->setIconTextVisible(visible);
        }
        updateHideAllAction(!m_fencesVisible && !visible);
    });
    connect(ConfigManager::instance(), &ConfigManager::layoutLockedChanged, this, [updateLockAction, updateNewFenceEnabled](bool locked) {
        updateLockAction(locked);
        updateNewFenceEnabled(!locked);
    });
}

bool FenceManager::eventFilter(QObject *watched, QEvent *event)
{
    if (watched->objectName() == "actionLabel") {
        if (event->type() == QEvent::MouseButtonRelease) {
            if (QWidget *widget = qobject_cast<QWidget*>(watched))
                widget->window()->close();
            else
                m_trayMenu->close();

            const QString actionRole = watched->property("actionRole").toString();

            if (actionRole == "autoStart") {
                ConfigManager::instance()->setAutoStart(!ConfigManager::instance()->autoStart());
                return true;
            }
            if (actionRole == "newFence") {
                QTimer::singleShot(10, this, [this]() { onNewFenceRequested(); });
                return true;
            }
            if (actionRole == "toggleFencesAndText") {
                QTimer::singleShot(50, this, [this]() {
                    const bool shouldHide = m_fencesVisible || ConfigManager::instance()->iconTextVisible();
                    shouldHide ? hideAllFences() : showAllFences();
                    ConfigManager::instance()->setIconTextVisible(!shouldHide);
                });
                return true;
            }
            if (actionRole == "layoutLock") {
                ConfigManager::instance()->setLayoutLocked(!ConfigManager::instance()->layoutLocked());
                return true;
            }
            if (actionRole == "about") {
                QTimer::singleShot(10, this, [this]() { onAboutRequested(); });
                return true;
            }
            if (actionRole == "exit") {
                QTimer::singleShot(10, this, [this]() { onExitRequested(); });
                return true;
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
    if (ConfigManager::instance()->layoutLocked()) return;

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
    if (m_isShutdown) return;

    for (FenceWindow *fence : m_fences) {
        if (fence && fence->isRestoringFromJson()) {
            ConfigManager::writeLog("[saveFences] Skipped while fence is still restoring from JSON: " + fence->title());
            return;
        }
    }

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
    if (ConfigManager::instance()->layoutLocked()) return;
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
// 将 fencing_config.json、fences_storage 以及围栏引用的外部图标文件打包为 .zip
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

    QTemporaryDir bundleDir;
    if (!bundleDir.isValid()) {
        QMessageBox::critical(nullptr, "备份失败", "无法创建临时备份目录。");
        return;
    }

    QString prepareError;
    int bundledExternalCount = 0;
    int missingExternalCount = 0;
    if (!prepareBackupBundle(appDataDir, bundleDir.path(), &prepareError, &bundledExternalCount, &missingExternalCount)) {
        QMessageBox::critical(nullptr, "备份失败",
            QString("准备备份数据时出错：\n%1").arg(prepareError.isEmpty() ? "未知错误" : prepareError));
        return;
    }

    const QString bundleRoot = normalizeNativePath(bundleDir.path());
    QString psCmd = QString(
        "Compress-Archive -Path '%1\\*' -DestinationPath '%2' -Force"
    ).arg(bundleRoot, savePath);

    QProcess proc;
    proc.setProgram("powershell.exe");
    proc.setArguments({"-NonInteractive", "-NoProfile", "-Command", psCmd});
    proc.start();
    proc.waitForFinished(30000); // 最多等 30 秒

    if (proc.exitCode() == 0 && QFile::exists(savePath)) {
        QString message = "围栏数据已成功备份。";
        if (bundledExternalCount > 0) {
            message += QString("\n已额外打包 %1 个围栏引用的外部图标文件。").arg(bundledExternalCount);
        }
        if (missingExternalCount > 0) {
            message += QString("\n另有 %1 个外部图标文件当前已丢失，无法收入本次备份。").arg(missingExternalCount);
        }
        QMessageBox::information(nullptr, "备份成功", message);
    } else {
        QString errMsg = QString::fromUtf8(proc.readAllStandardError());
        QMessageBox::critical(nullptr, "备份失败",
            QString("备份围栏数据时出错：\n%1").arg(errMsg.isEmpty() ? "未知错误" : errMsg));
    }
}

// ─────────────────────────────────────────────────────────────────
// 围栏数据还原
// 从 .zip 中解压、补齐外部图标文件并覆盖 AppData 目录下的围栏数据，然后重启应用
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
    QString oldSettings = appDataDir + "/user_settings.ini";

    // 关键修复：阻止应用内正在进行的任何异步保存写入动作
    // 否则它们可能会在 Expand-Archive 解压之后被写入，覆盖掉我们刚刚还原好的数据！
    ConfigManager::instance()->stopSave();

    QTemporaryDir extractDir;
    if (!extractDir.isValid()) {
        ConfigManager::instance()->resumeSave();
        QMessageBox::critical(nullptr, "还原失败", "无法创建临时还原目录。");
        return;
    }

    // 先解压到临时目录，校验并补齐外部图标文件后再整体覆盖正式数据目录
    QString psCmd = QString(
        "Expand-Archive -Path '%1' -DestinationPath '%2' -Force"
    ).arg(zipPath, normalizeNativePath(extractDir.path()));

    QProcess proc;
    proc.setProgram("powershell.exe");
    proc.setArguments({"-NonInteractive", "-NoProfile", "-Command", psCmd});
    proc.start();
    proc.waitForFinished(30000);

    if (proc.exitCode() != 0) {
        ConfigManager::instance()->resumeSave();
        QString errMsg = QString::fromUtf8(proc.readAllStandardError());
        QMessageBox::critical(nullptr, "还原失败",
            QString("还原围栏数据时出错：\n%1").arg(errMsg.isEmpty() ? "未知错误" : errMsg));
        return;
    }

    QString patchError;
    if (!materializeBundledIconsIntoStorage(extractDir.path(), &patchError)) {
        ConfigManager::instance()->resumeSave();
        QMessageBox::critical(nullptr, "还原失败",
            QString("整理备份中的图标文件时出错：\n%1").arg(patchError.isEmpty() ? "未知错误" : patchError));
        return;
    }

    const QString extractedJson = normalizeNativePath(extractDir.path() + "/fencing_config.json");
    const QString extractedStorage = normalizeNativePath(extractDir.path() + "/fences_storage");
    const QString extractedSettings = normalizeNativePath(extractDir.path() + "/user_settings.ini");

    // 验证关键文件存在
    bool jsonOk    = QFile::exists(extractedJson);
    bool storageOk = QDir(extractedStorage).exists();

    if (!jsonOk && !storageOk) {
        ConfigManager::instance()->resumeSave();
        QMessageBox::warning(nullptr, "还原警告",
            "备份文件似乎不包含有效的围栏数据（fencing_config.json 和 fences_storage 均未找到）。\n"
            "请确认选择了正确的 DeskGo 备份文件。");
        return;
    }

    if (QFile::exists(oldJson) && !QFile::remove(oldJson)) {
        ConfigManager::instance()->resumeSave();
        QMessageBox::critical(nullptr, "还原失败", QString("无法删除旧配置文件：\n%1").arg(oldJson));
        return;
    }
    if (QDir(oldStorage).exists() && !QDir(oldStorage).removeRecursively()) {
        ConfigManager::instance()->resumeSave();
        QMessageBox::critical(nullptr, "还原失败", QString("无法删除旧图标存储目录：\n%1").arg(oldStorage));
        return;
    }
    if (QFile::exists(oldSettings) && !QFile::remove(oldSettings)) {
        ConfigManager::instance()->resumeSave();
        QMessageBox::critical(nullptr, "还原失败", QString("无法删除旧设置文件：\n%1").arg(oldSettings));
        return;
    }

    QString deployError;
    if (jsonOk && !copyFileReplacing(extractedJson, oldJson, &deployError)) {
        ConfigManager::instance()->resumeSave();
        QMessageBox::critical(nullptr, "还原失败",
            QString("写入围栏配置失败：\n%1").arg(deployError.isEmpty() ? "未知错误" : deployError));
        return;
    }
    if (storageOk && !copyDirectoryRecursively(extractedStorage, oldStorage, &deployError)) {
        ConfigManager::instance()->resumeSave();
        QMessageBox::critical(nullptr, "还原失败",
            QString("写入图标存储目录失败：\n%1").arg(deployError.isEmpty() ? "未知错误" : deployError));
        return;
    }
    if (QFile::exists(extractedSettings) && !copyFileReplacing(extractedSettings, oldSettings, &deployError)) {
        ConfigManager::instance()->resumeSave();
        QMessageBox::critical(nullptr, "还原失败",
            QString("写入设置文件失败：\n%1").arg(deployError.isEmpty() ? "未知错误" : deployError));
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
