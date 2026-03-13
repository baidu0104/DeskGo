#include "configmanager.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QtConcurrent>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

ConfigManager* ConfigManager::instance()
{
    static ConfigManager instance;
    return &instance;
}

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
    , m_settings(nullptr)
    , m_saveDebounceTimer(new QTimer(this))
{
    m_saveDebounceTimer->setSingleShot(true);
    m_saveDebounceTimer->setInterval(3000); // 3 秒防抖
    connect(m_saveDebounceTimer, &QTimer::timeout, this, &ConfigManager::doSave);
    
    // 优先使用标准数据目录，但保留应用目录作为迁移源
    // 这是为了支持 Microsoft Store (MSIX) 容器环境，该环境下程序目录是只读的
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(appDataPath);
    
    QString appDirPath = QCoreApplication::applicationDirPath();
    
    // 检查是否有旧配置需要从程序目录迁移
    QString oldSettings = appDirPath + "/user_settings.ini";
    QString oldFences = appDirPath + "/fencing_config.json";
    QString oldStorage = appDirPath + "/fences_storage";
    
    m_settingsPath = appDataPath + "/user_settings.ini";
    m_fencesPath = appDataPath + "/fencing_config.json";
    m_fencesStoragePath = appDataPath + "/fences_storage";

    // 迁移逻辑：AppData 为空且程序目录有旧配置时执行
    if (!QFile::exists(m_settingsPath) && QFile::exists(oldSettings)) {
        QFile::copy(oldSettings, m_settingsPath);
    }
    if (!QFile::exists(m_fencesPath) && QFile::exists(oldFences)) {
        QFile::copy(oldFences, m_fencesPath);
    }
    
    // 确保存储目录存在
    QDir().mkpath(m_fencesStoragePath);

    m_settings = new QSettings(m_settingsPath, QSettings::IniFormat, this);
    
    qDebug() << "[ConfigManager] Settings path:" << m_settingsPath;
    qDebug() << "[ConfigManager] Fences path:" << m_fencesPath;

    load();
}

ConfigManager::~ConfigManager()
{
}

bool ConfigManager::autoStart() const
{
    return m_autoStart;
}

void ConfigManager::setAutoStart(bool enabled)
{
    if (m_autoStart != enabled) {
        m_autoStart = enabled;
        updateAutoStartRegistry(enabled);
        requestSave();
        emit autoStartChanged(enabled);
    }
}

bool ConfigManager::minimizeToTray() const
{
    return m_minimizeToTray;
}

void ConfigManager::setMinimizeToTray(bool enabled)
{
    if (m_minimizeToTray != enabled) {
        m_minimizeToTray = enabled;
        requestSave();
    }
}

QString ConfigManager::theme() const
{
    return m_theme;
}

void ConfigManager::setTheme(const QString &theme)
{
    if (m_theme != theme) {
        m_theme = theme;
        requestSave();
        emit themeChanged(theme);
    }
}

bool ConfigManager::iconTextVisible() const
{
    return m_iconTextVisible;
}

void ConfigManager::setIconTextVisible(bool visible)
{
    if (m_iconTextVisible != visible) {
        m_iconTextVisible = visible;
        requestSave();
        emit iconTextVisibleChanged(visible);
    }
}

QRect ConfigManager::windowGeometry() const
{
    return m_windowGeometry;
}

void ConfigManager::setWindowGeometry(const QRect &geometry)
{
    m_windowGeometry = geometry;
}

bool ConfigManager::windowMaximized() const
{
    return m_windowMaximized;
}

void ConfigManager::setWindowMaximized(bool maximized)
{
    m_windowMaximized = maximized;
}

QJsonObject ConfigManager::fencesData() const
{
    return m_fencesData;
}

void ConfigManager::setFencesData(const QJsonObject &data)
{
    m_fencesData = data;
    requestSave();
}

void ConfigManager::requestSave()
{
    m_saveDebounceTimer->start();
}

void ConfigManager::doSave()
{
    // 异步执行保存逻辑
    QtConcurrent::run([this]() {
        this->sync();
    });
}

void ConfigManager::stopSave()
{
    m_saveDisabled = true;
    if (m_saveDebounceTimer->isActive()) {
        m_saveDebounceTimer->stop();
    }
}

void ConfigManager::sync()
{
    if (!m_settings || m_saveDisabled) return;

    // 停止防抖定时器，防止在同步保存过程中再次触发
    if (m_saveDebounceTimer->isActive()) {
        m_saveDebounceTimer->stop();
    }

    // 1. 保存设置项 (QSettings)
    m_settings->setValue("General/AutoStart", m_autoStart);
    m_settings->setValue("General/MinimizeToTray", m_minimizeToTray);
    m_settings->setValue("General/Theme", m_theme);
    m_settings->setValue("General/IconTextVisible", m_iconTextVisible);
    m_settings->setValue("Window/Geometry", m_windowGeometry);
    m_settings->setValue("Window/Maximized", m_windowMaximized);
    m_settings->sync(); // 强制写入磁盘

    // 2. 保存围栏数据 (JSON)
    QString fencesPath = m_fencesPath;
    QJsonObject fencesData = m_fencesData;

    if (fencesData.isEmpty() || !fencesData.contains("fences")) {
        // 如果数据为空，通常不应该直接覆盖现有配置，除非确定是清空
        // 但在这里我们信任内存中的数据
    }

    QString tempPath = fencesPath + ".tmp";
    QFile tempFile(tempPath);
    if (tempFile.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(fencesData);
        QByteArray jsonData = doc.toJson(QJsonDocument::Indented);
        tempFile.write(jsonData);
        tempFile.flush();
        tempFile.close();
        
        if (QFile::exists(fencesPath)) {
            QFile::remove(fencesPath);
        }
        if (!QFile::rename(tempPath, fencesPath)) {
            // 如果重命名失败（可能在某些 Windows 环境下存在冲突），尝试复制
            if (QFile::copy(tempPath, fencesPath)) {
                QFile::remove(tempPath);
                qDebug() << "[ConfigManager] Sync success via copy";
            } else {
                qDebug() << "[ConfigManager] Sync FATAL: failed to write fences file";
            }
        } else {
            qDebug() << "[ConfigManager] Sync success via rename";
        }
    } else {
        qDebug() << "[ConfigManager] Sync FATAL: failed to open temp file for writing";
    }
}

void ConfigManager::load()
{
    if (!m_settings) return;

    m_autoStart = m_settings->value("General/AutoStart", false).toBool();
    m_minimizeToTray = m_settings->value("General/MinimizeToTray", true).toBool();
    m_theme = m_settings->value("General/Theme", "dark").toString();
    m_iconTextVisible = m_settings->value("General/IconTextVisible", true).toBool();
    m_windowGeometry = m_settings->value("Window/Geometry", QRect()).toRect();
    m_windowMaximized = m_settings->value("Window/Maximized", false).toBool();

    auto tryLoadJson = [this](const QString& path) -> bool {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            m_fencesData = doc.object();
            file.close();
            return !m_fencesData.isEmpty();
        }
        return false;
    };

    tryLoadJson(m_fencesPath);

    // 启动时确保注册表状态与配置一致
    updateAutoStartRegistry(m_autoStart);
}

void ConfigManager::updateAutoStartRegistry(bool enabled)
{
#ifdef Q_OS_WIN
    // 方案 1: 检查是否运行在 MSIX 容器中
    // MSIX 应用无法直接修改注册表 Run 键，必须使用 StartupTask API
    bool isPackaged = false;
    
    // 动态加载 GetPackageFamilyName 以兼容旧版编译器和 MinGW
    typedef LONG (WINAPI *GetPackageFamilyNamePtr)(HANDLE, UINT32*, PWSTR);
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (hKernel32) {
        GetPackageFamilyNamePtr pGetPackageFamilyName = (GetPackageFamilyNamePtr)(void*)GetProcAddress(hKernel32, "GetPackageFamilyName");
        if (pGetPackageFamilyName) {
            UINT32 length = 0;
            LONG rc = pGetPackageFamilyName(GetCurrentProcess(), &length, NULL);
            // 如果返回 ERROR_INSUFFICIENT_BUFFER，说明应用处于包容器中
            isPackaged = (rc == 122L); // 122 是 ERROR_INSUFFICIENT_BUFFER 的值
        }
    }

    if (isPackaged) {
        // MSIX 容器环境：动态加载 Windows Runtime 库调用 StartupTask API
        // 注意：这需要 link 对应的 WindowsApp.lib 或直接动态加载扩展
        // 为了简化依赖，这里主要提供逻辑说明。在实际现代 C++/WinRT 环境中，代码如下：
        /*
        using namespace winrt::Windows::ApplicationModel;
        auto task = StartupTask::GetAsync(L"DeskGoStartupTask").get();
        if (enabled) task.RequestEnableAsync().get();
        else task.Disable();
        */
        
        // 鉴于本项目目前使用原生 Windows API 且可能未配置 WinRT 投影
        // 我们通过 PowerShell 或外部辅助方式来实现，或者在 AppxManifest 中声明。
        // 由于已经在 AppxManifest.xml 中添加了 StartupTask，
        // 实际上在应用商店环境下，用户通过“任务管理器-启动”控制即可。
        // 但为了实现代码控制，我们可以输出一个日志提醒，或者在支持的环境下调用 powershell。
        
        QString psCmd = QString("Get-AppxPackage *DeskGo* | Get-AppxPackageManifest | "
                                "Select-Xml -XPath \"//desktop:StartupTask\" -Namespace @{desktop=\"http://schemas.microsoft.com/appx/manifest/desktop/windows10\"} | "
                                "ForEach-Object { $_.Node.Enabled = \"%1\" }").arg(enabled ? "true" : "false");
        // 注意：MSIX 内部权限受限，通常无法通过自写代码修改 Manifest，
        // 需调用特定的 Windows 10 SDK 接口。
        qDebug() << "[ConfigManager] App is running in MSIX container. Auto-start is managed via AppxManifest StartupTask.";
        return;
    }

    // 方案 2: 普通 Win32 环境（注册表方案）
    QString appName = "DeskGo";
    QString appPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    HKEY hKey;
    LPCWSTR runPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, runPath, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enabled) {
            std::wstring wPath = L"\"" + appPath.toStdWString() + L"\""; // 建议加引号防止空格路径问题
            RegSetValueExW(hKey, appName.toStdWString().c_str(), 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(wPath.c_str()),
                          static_cast<DWORD>((wPath.length() + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(hKey, appName.toStdWString().c_str());
        }
        RegCloseKey(hKey);
    }
#else
    Q_UNUSED(enabled)
#endif
}
