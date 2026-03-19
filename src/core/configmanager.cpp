#include "configmanager.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QtConcurrent>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winstring.h>

static bool isMsixPackage() {
    typedef LONG (WINAPI *GetCurrentPackageFullNamePtr)(UINT32*, PWSTR);
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    GetCurrentPackageFullNamePtr pGetCurrentPackageFullName = reinterpret_cast<GetCurrentPackageFullNamePtr>(reinterpret_cast<void(*)()>(GetProcAddress(hKernel32, "GetCurrentPackageFullName")));
    if (pGetCurrentPackageFullName) {
        UINT32 length = 0;
        LONG rc = pGetCurrentPackageFullName(&length, NULL);
        return rc != 15700L; // APPMODEL_ERROR_NO_PACKAGE
    }
    return false;
}

const IID IID_IAsyncInfo = { 0x00000036, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
const IID IID_IStartupTask = { 0xf75c23c8, 0xb5f2, 0x4f6c, { 0x88, 0xdd, 0x36, 0xcb, 0x1d, 0x59, 0x9d, 0x17 } };
const IID IID_IStartupTaskStatics = { 0xee5b60bd, 0xa148, 0x41a7, { 0xb2, 0x6e, 0xe8, 0xb8, 0x8a, 0x1e, 0x62, 0xf8 } };

struct IInspectable_Raw : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int*) = 0;
};
struct IAsyncInfo_Raw : public IInspectable_Raw {
    virtual HRESULT STDMETHODCALLTYPE get_Id(unsigned int* id) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(int* status) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* errorCode) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};
struct IAsyncOperation_StartupTaskState_Raw : public IInspectable_Raw {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(int* results) = 0;
};
struct IStartupTask_Raw : public IInspectable_Raw {
    virtual HRESULT STDMETHODCALLTYPE RequestEnableAsync(IAsyncOperation_StartupTaskState_Raw** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE Disable(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_State(int* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TaskId(HSTRING* value) = 0;
};
struct IAsyncOperation_StartupTask_Raw : public IInspectable_Raw {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(IStartupTask_Raw** results) = 0;
};
struct IStartupTaskStatics_Raw : public IInspectable_Raw {
    virtual HRESULT STDMETHODCALLTYPE GetForCurrentPackageAsync(void** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAsync(HSTRING taskId, IAsyncOperation_StartupTask_Raw** operation) = 0;
};

static bool WaitAsyncInfo(IUnknown* asyncOp) {
    IAsyncInfo_Raw* info = nullptr;
    HRESULT hr = asyncOp->QueryInterface(IID_IAsyncInfo, (void**)&info);
    if (FAILED(hr) || !info) {
        return false;
    }
    int status = 0;
    for (int i = 0; i < 100; ++i) {
        hr = info->get_Status(&status);
        if (FAILED(hr)) break;
        if (status != 0) break;
        Sleep(50);
    }
    info->Release();
    return status == 1; // Completed
}

static IStartupTask_Raw* GetMsixStartupTask(const QString& taskIdToFind) {
    HMODULE hComBase = LoadLibraryW(L"combase.dll");
    if (!hComBase) return nullptr;

    typedef HRESULT (WINAPI *RoGetActivationFactoryFunc)(HSTRING, REFIID, void**);
    typedef HRESULT (WINAPI *WindowsCreateStringFunc)(LPCWSTR, UINT32, HSTRING*);
    typedef HRESULT (WINAPI *WindowsDeleteStringFunc)(HSTRING);
    typedef HRESULT (WINAPI *RoInitializeFunc)(int);
    
    RoGetActivationFactoryFunc _RoGetActivationFactory = reinterpret_cast<RoGetActivationFactoryFunc>(reinterpret_cast<void(*)()>(GetProcAddress(hComBase, "RoGetActivationFactory")));
    WindowsCreateStringFunc _WindowsCreateString = reinterpret_cast<WindowsCreateStringFunc>(reinterpret_cast<void(*)()>(GetProcAddress(hComBase, "WindowsCreateString")));
    WindowsDeleteStringFunc _WindowsDeleteString = reinterpret_cast<WindowsDeleteStringFunc>(reinterpret_cast<void(*)()>(GetProcAddress(hComBase, "WindowsDeleteString")));
    RoInitializeFunc _RoInitialize = reinterpret_cast<RoInitializeFunc>(reinterpret_cast<void(*)()>(GetProcAddress(hComBase, "RoInitialize")));

    if (!_RoGetActivationFactory || !_WindowsCreateString || !_WindowsDeleteString) return nullptr;

    if (_RoInitialize) _RoInitialize(1); // RO_INIT_MULTITHREADED = 1

    HSTRING className = nullptr;
    _WindowsCreateString(L"Windows.ApplicationModel.StartupTask", 36, &className);

    IStartupTaskStatics_Raw* statics = nullptr;
    HRESULT hr = _RoGetActivationFactory(className, IID_IStartupTaskStatics, (void**)&statics);
    _WindowsDeleteString(className);

    if (FAILED(hr) || !statics) return nullptr;

    HSTRING hTaskId = nullptr;
    _WindowsCreateString((LPCWSTR)taskIdToFind.utf16(), taskIdToFind.length(), &hTaskId);

    IAsyncOperation_StartupTask_Raw* op = nullptr;
    hr = statics->GetAsync(hTaskId, &op);
    _WindowsDeleteString(hTaskId);
    statics->Release();

    if (FAILED(hr) || !op) return nullptr;

    IStartupTask_Raw* task = nullptr;
    if (WaitAsyncInfo(op)) op->GetResults(&task);
    op->Release();
    
    return task;
}
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
        bool finalState = updateAutoStartRegistry(enabled);
        m_autoStart = finalState;
        requestSave();
        emit autoStartChanged(m_autoStart);
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

    // 启动时从系统读取真实的各种情况
    syncAutoStartWithSystem();
}

bool ConfigManager::updateAutoStartRegistry(bool enabled)
{
#ifdef Q_OS_WIN
    if (isMsixPackage()) {
        static const QString kTaskId = "DeskGoStartupTask";
        IStartupTask_Raw* task = GetMsixStartupTask(kTaskId);
        if (!task) {
            return enabled;
        }

        if (enabled) {
            IAsyncOperation_StartupTaskState_Raw* op = nullptr;
            HRESULT hr = task->RequestEnableAsync(&op);
            if (SUCCEEDED(hr) && op) {
                if (WaitAsyncInfo(op)) {
                    int finalState = 0;
                    if (SUCCEEDED(op->GetResults(&finalState))) {
                    }
                }
                op->Release();
            }
        } else {
            task->Disable();
        }
        task->Release();
        return enabled;
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
    return enabled;
#else
    Q_UNUSED(enabled)
    return enabled;
#endif
}

void ConfigManager::syncAutoStartWithSystem()
{
#ifdef Q_OS_WIN
    bool actualSystemState = false;

    if (isMsixPackage()) {
        static const QString kTaskId = "DeskGoStartupTask";
        IStartupTask_Raw* task = GetMsixStartupTask(kTaskId);
        if (task) {
            int state = 0;
            if (SUCCEEDED(task->get_State(&state))) {
                // 2 = Enabled, 3 = EnabledByPolicy
                actualSystemState = (state == 2 || state == 3);
            }
            task->Release();
        }
    } else {
        QSettings registrySettings("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
        QString appName = "DeskGo";
        actualSystemState = registrySettings.contains(appName);
    }

    if (m_autoStart != actualSystemState) {
        m_autoStart = actualSystemState;
        
        if (m_settings) {
            m_settings->setValue("General/AutoStart", m_autoStart);
            m_settings->sync();
        }
        emit autoStartChanged(m_autoStart);
    }
#endif
}
