#include "configmanager.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QtConcurrent>


#ifdef Q_OS_WIN
#include <windows.h>
#include <winstring.h>

// 递归复制目录
static bool copyDirectory(const QString &src, const QString &dst) {
  QDir srcDir(src);
  if (!srcDir.exists())
    return false;
  QDir().mkpath(dst);

  // 处理文件
  for (const QString &fileName : srcDir.entryList(QDir::Files)) {
    QString srcPath = src + "/" + fileName;
    QString dstPath = dst + "/" + fileName;
    if (QFile::exists(dstPath))
      QFile::remove(dstPath);
    QFile::copy(srcPath, dstPath);
  }

  // 处理子目录
  for (const QString &dirName :
       srcDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    QString srcPath = src + "/" + dirName;
    QString dstPath = dst + "/" + dirName;
    copyDirectory(srcPath, dstPath);
  }
  return true;
}

static bool isMsixPackage() {
  typedef LONG(WINAPI * GetCurrentPackageFullNamePtr)(UINT32 *, PWSTR);
  HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
  GetCurrentPackageFullNamePtr pGetCurrentPackageFullName =
      reinterpret_cast<GetCurrentPackageFullNamePtr>(
          reinterpret_cast<void (*)()>(
              GetProcAddress(hKernel32, "GetCurrentPackageFullName")));
  if (pGetCurrentPackageFullName) {
    UINT32 length = 0;
    LONG rc = pGetCurrentPackageFullName(&length, NULL);
    // ERROR_SUCCESS = 0, ERROR_INSUFFICIENT_BUFFER = 122,
    // APPMODEL_ERROR_NO_PACKAGE = 15700
    bool inPackage = (rc == 0 || rc == 122);
    qDebug() << "[ConfigManager] isMsixPackage check: inPackage =" << inPackage
             << "rc =" << rc;
    return inPackage;
  }
  return false;
}

QMutex ConfigManager::s_logMutex;

void ConfigManager::writeLog(const QString &msg) {
  QMutexLocker locker(&s_logMutex);
  QString appDataPath =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  QDir().mkpath(appDataPath);
  QFile file(appDataPath + "/msix_debug.txt");
  if (file.open(QIODevice::Append | QIODevice::Text)) {
    QTextStream out(&file);
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz ")
        << msg << "\n";
    file.close();
  }
}

const IID IID_IAsyncInfo = {0x00000036,
                            0x0000,
                            0x0000,
                            {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const IID IID_IStartupTask = {0xf75c23c8,
                              0xb5f2,
                              0x4f6c,
                              {0x88, 0xdd, 0x36, 0xcb, 0x1d, 0x59, 0x9d, 0x17}};
const IID IID_IStartupTaskStatics = {
    0xee5b60bd,
    0xa148,
    0x41a7,
    {0xb2, 0x6e, 0xe8, 0xb8, 0x8a, 0x1e, 0x62, 0xf8}};

struct IInspectable_Raw : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG *, IID **) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int *) = 0;
};
struct IAsyncInfo_Raw : public IInspectable_Raw {
  virtual HRESULT STDMETHODCALLTYPE get_Id(unsigned int *id) = 0;
  virtual HRESULT STDMETHODCALLTYPE get_Status(int *status) = 0;
  virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT *errorCode) = 0;
  virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
  virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};
struct IAsyncOperation_StartupTaskState_Raw : public IInspectable_Raw {
  virtual HRESULT STDMETHODCALLTYPE put_Completed(void *handler) = 0;
  virtual HRESULT STDMETHODCALLTYPE get_Completed(void **handler) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetResults(int *results) = 0;
};
// 重要：虚函数表顺序必须与 Windows 原始接口严格一致
struct IStartupTask_Raw : public IInspectable_Raw {
  virtual HRESULT STDMETHODCALLTYPE RequestEnableAsync(
      IAsyncOperation_StartupTaskState_Raw **operation) = 0;        // 偏移量 6
  virtual HRESULT STDMETHODCALLTYPE Disable(void) = 0;              // 偏移量 7
  virtual HRESULT STDMETHODCALLTYPE get_State(int *value) = 0;      // 偏移量 8
  virtual HRESULT STDMETHODCALLTYPE get_TaskId(HSTRING *value) = 0; // 偏移量 9
};
struct IAsyncOperation_StartupTask_Raw : public IInspectable_Raw {
  virtual HRESULT STDMETHODCALLTYPE put_Completed(void *handler) = 0;
  virtual HRESULT STDMETHODCALLTYPE get_Completed(void **handler) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetResults(IStartupTask_Raw **results) = 0;
};
struct IStartupTaskStatics_Raw : public IInspectable_Raw {
  virtual HRESULT STDMETHODCALLTYPE
  GetForCurrentPackageAsync(void **operation) = 0;
  virtual HRESULT STDMETHODCALLTYPE
  GetAsync(HSTRING taskId, IAsyncOperation_StartupTask_Raw **operation) = 0;
};

static bool WaitAsyncInfo(IUnknown *asyncOp, const QString &context) {
  IAsyncInfo_Raw *info = nullptr;
  HRESULT hr = asyncOp->QueryInterface(IID_IAsyncInfo, (void **)&info);
  if (FAILED(hr) || !info) {
    qDebug() << "[ConfigManager] WaitAsyncInfo QI failed." << context
             << "HRESULT:" << Qt::hex << hr;
    return false;
  }
  int status = 0;
  for (int i = 0; i < 100; ++i) {
    hr = info->get_Status(&status);
    if (FAILED(hr)) {
      qDebug() << "[ConfigManager] get_Status failed." << context
               << "HRESULT:" << Qt::hex << hr;
      break;
    }
    if (status != 0)
      break; // 0 = Started
    Sleep(50);
  }
  info->Release();
  bool success = (status == 1); // 1 = Completed
  if (!success) {
    qDebug()
        << "[ConfigManager] WaitAsyncInfo finished with non-success status:"
        << status << context;
  }
  return success;
}

static IStartupTask_Raw *GetMsixStartupTask(const QString &taskIdToFind) {
  // combase.dll 是 Windows 核心组件，使用 static 句柄避免重复装载带来的资源波动
  static HMODULE hComBase = GetModuleHandleW(L"combase.dll");
  if (!hComBase)
    hComBase = LoadLibraryW(L"combase.dll");
  if (!hComBase)
    return nullptr;

  typedef HRESULT(WINAPI * RoGetActivationFactoryFunc)(HSTRING, REFIID,
                                                       void **);
  typedef HRESULT(WINAPI * WindowsCreateStringFunc)(LPCWSTR, UINT32, HSTRING *);
  typedef HRESULT(WINAPI * WindowsDeleteStringFunc)(HSTRING);
  typedef HRESULT(WINAPI * RoInitializeFunc)(int);

  // 静态获取函数指针，提高重复调用性能
  static auto _RoGetActivationFactory =
      reinterpret_cast<RoGetActivationFactoryFunc>(reinterpret_cast<void (*)()>(
          GetProcAddress(hComBase, "RoGetActivationFactory")));
  static auto _WindowsCreateString =
      reinterpret_cast<WindowsCreateStringFunc>(reinterpret_cast<void (*)()>(
          GetProcAddress(hComBase, "WindowsCreateString")));
  static auto _WindowsDeleteString =
      reinterpret_cast<WindowsDeleteStringFunc>(reinterpret_cast<void (*)()>(
          GetProcAddress(hComBase, "WindowsDeleteString")));
  static auto _RoInitialize = reinterpret_cast<RoInitializeFunc>(
      reinterpret_cast<void (*)()>(GetProcAddress(hComBase, "RoInitialize")));

  if (!_RoGetActivationFactory || !_WindowsCreateString ||
      !_WindowsDeleteString)
    return nullptr;

  // 尝试初始化，若已初始化则返回 S_FALSE 或兼容错误，不影响使用
  if (_RoInitialize)
    _RoInitialize(1); // RO_INIT_MULTITHREADED = 1

  HSTRING className = nullptr;
  _WindowsCreateString(L"Windows.ApplicationModel.StartupTask", 36, &className);

  IStartupTaskStatics_Raw *statics = nullptr;
  HRESULT hr = _RoGetActivationFactory(className, IID_IStartupTaskStatics,
                                       (void **)&statics);
  _WindowsDeleteString(className);

  if (FAILED(hr) || !statics) {
    qDebug() << "[ConfigManager] Failed to get StartupTaskStatics. HRESULT:"
             << Qt::hex << hr;
    return nullptr;
  }

  HSTRING hTaskId = nullptr;
  _WindowsCreateString((LPCWSTR)taskIdToFind.utf16(), taskIdToFind.length(),
                       &hTaskId);

  IAsyncOperation_StartupTask_Raw *op = nullptr;
  hr = statics->GetAsync(hTaskId, &op);
  _WindowsDeleteString(hTaskId);
  statics->Release();

  if (FAILED(hr) || !op) {
    qDebug() << "[ConfigManager] GetAsync failed for taskId:" << taskIdToFind
             << "HRESULT:" << Qt::hex << hr;
    return nullptr;
  }

  IStartupTask_Raw *task = nullptr;
  if (WaitAsyncInfo(op, "GetAsync")) {
    hr = op->GetResults(&task);
    if (FAILED(hr)) {
      qDebug() << "[ConfigManager] GetResults for StartupTask failed. HRESULT:"
               << Qt::hex << hr;
    }
  } else {
    qDebug() << "[ConfigManager] WaitAsyncInfo timed out/failed for GetAsync.";
  }
  op->Release();

  if (task) {
    ConfigManager::writeLog(
        QString("[GetMsixStartupTask] Successfully obtained task for %1")
            .arg(taskIdToFind));
  } else {
    ConfigManager::writeLog(
        QString("[GetMsixStartupTask] FAILED to obtain task for %1")
            .arg(taskIdToFind));
  }
  return task;
}
#endif

ConfigManager *ConfigManager::instance() {
  static ConfigManager instance;
  return &instance;
}

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent), m_settings(nullptr),
      m_saveDebounceTimer(new QTimer(this)) {
  m_saveDebounceTimer->setSingleShot(true);
  m_saveDebounceTimer->setInterval(3000); // 3 秒防抖
  connect(m_saveDebounceTimer, &QTimer::timeout, this, &ConfigManager::doSave);

  // 优先使用标准数据目录，但保留应用目录作为迁移源
  // 这是为了支持 Microsoft Store (MSIX) 容器环境，该环境下程序目录是只读的
  QString appDataPath =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
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

  // 迁移 fences_storage 目录
  if (QDir(oldStorage).exists()) {
    QDir dstDir(m_fencesStoragePath);
    // 如果 AppData 目录不存在，或者里面没有子目录/文件，就进行强制迁移复刻
    if (!dstDir.exists() ||
        dstDir.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)
            .isEmpty()) {
      ConfigManager::writeLog(
          "Detected empty storage in AppData, starting migration from " +
          oldStorage);
      copyDirectory(oldStorage, m_fencesStoragePath);
    }
  }

  // 确保存储目录存在
  QDir().mkpath(m_fencesStoragePath);

  m_settings = new QSettings(m_settingsPath, QSettings::IniFormat, this);

  qDebug() << "[ConfigManager] Settings path:" << m_settingsPath;
  qDebug() << "[ConfigManager] Fences path:" << m_fencesPath;

  load();
}

ConfigManager::~ConfigManager() {}

bool ConfigManager::autoStart() const {
  QMutexLocker locker(&m_stateMutex);
  return m_autoStart.load();
}

void ConfigManager::setAutoStart(bool enabled) {
  // 不再根据内存值对比，而是由底层 registry 告诉我们最终是否成功
  bool finalState = updateAutoStartRegistry(enabled);
  bool changed = false;
  {
    QMutexLocker locker(&m_stateMutex);
    changed = (m_autoStart.load() != finalState);
    if (changed) {
      m_autoStart = finalState;
    }
  }
  if (changed) {
    requestSave();
    emit autoStartChanged(finalState);
  } else {
    // 如果状态没变（例如被系统拦截了），也强制发一次信号同步回 UI
    emit autoStartChanged(finalState);
  }
}

bool ConfigManager::minimizeToTray() const {
  QMutexLocker locker(&m_stateMutex);
  return m_minimizeToTray;
}

void ConfigManager::setMinimizeToTray(bool enabled) {
  bool changed = false;
  {
    QMutexLocker locker(&m_stateMutex);
    changed = (m_minimizeToTray != enabled);
    if (changed) {
      m_minimizeToTray = enabled;
    }
  }
  if (changed) {
    requestSave();
  }
}

QString ConfigManager::theme() const {
  QMutexLocker locker(&m_stateMutex);
  return m_theme;
}

void ConfigManager::setTheme(const QString &theme) {
  bool changed = false;
  {
    QMutexLocker locker(&m_stateMutex);
    changed = (m_theme != theme);
    if (changed) {
      m_theme = theme;
    }
  }
  if (changed) {
    requestSave();
    emit themeChanged(theme);
  }
}

bool ConfigManager::iconTextVisible() const {
  QMutexLocker locker(&m_stateMutex);
  return m_iconTextVisible;
}

void ConfigManager::setIconTextVisible(bool visible) {
  bool changed = false;
  {
    QMutexLocker locker(&m_stateMutex);
    changed = (m_iconTextVisible != visible);
    if (changed) {
      m_iconTextVisible = visible;
    }
  }
  if (changed) {
    requestSave();
    emit iconTextVisibleChanged(visible);
  }
}

bool ConfigManager::layoutLocked() const {
  QMutexLocker locker(&m_stateMutex);
  return m_layoutLocked;
}

void ConfigManager::setLayoutLocked(bool locked) {
  bool changed = false;
  {
    QMutexLocker locker(&m_stateMutex);
    changed = (m_layoutLocked != locked);
    if (changed) {
      m_layoutLocked = locked;
    }
  }
  if (changed) {
    requestSave();
    emit layoutLockedChanged(locked);
  }
}

QRect ConfigManager::windowGeometry() const {
  QMutexLocker locker(&m_stateMutex);
  return m_windowGeometry;
}

void ConfigManager::setWindowGeometry(const QRect &geometry) {
  QMutexLocker locker(&m_stateMutex);
  m_windowGeometry = geometry;
}

bool ConfigManager::windowMaximized() const {
  QMutexLocker locker(&m_stateMutex);
  return m_windowMaximized;
}

void ConfigManager::setWindowMaximized(bool maximized) {
  QMutexLocker locker(&m_stateMutex);
  m_windowMaximized = maximized;
}

QJsonObject ConfigManager::fencesData() const {
  QMutexLocker locker(&m_stateMutex);
  return m_fencesData;
}

void ConfigManager::setFencesData(const QJsonObject &data) {
  bool changed = false;
  {
    QMutexLocker locker(&m_stateMutex);
    changed = (m_fencesData != data);
    if (changed) {
      m_fencesData = data;
      m_fencesDirty = true;
    }
  }
  if (changed) {
    requestSave();
  }
}

void ConfigManager::requestSave() {
  QMutexLocker locker(&m_stateMutex);
  if (m_saveDisabled)
    return;
  m_saveDebounceTimer->start();
}

void ConfigManager::doSave() {
  // 异步执行保存逻辑
  QtConcurrent::run([this]() { this->sync(); });
}

void ConfigManager::stopSave() {
  QMutexLocker locker(&m_stateMutex);
  m_saveDisabled = true;
  if (m_saveDebounceTimer->isActive()) {
    m_saveDebounceTimer->stop();
  }
}

void ConfigManager::resumeSave() {
  QMutexLocker locker(&m_stateMutex);
  m_saveDisabled = false;
}

bool ConfigManager::sync() {
  return syncInternal(false);
}

bool ConfigManager::forceSync() {
  return syncInternal(true);
}

bool ConfigManager::waitForIdleAndForceSync() {
  stopSave();
  return forceSync();
}

bool ConfigManager::syncInternal(bool ignoreSaveDisabled) {
  QMutexLocker syncLocker(&m_syncMutex);

  QString settingsPath;
  QString fencesPath;
  bool autoStartValue = false;
  bool minimizeToTray = true;
  QString theme = "dark";
  bool iconTextVisible = true;
  bool layoutLocked = false;
  QRect windowGeometry;
  bool windowMaximized = false;
  QJsonObject fencesData;
  bool fencesDirty = false;

  {
    QMutexLocker stateLocker(&m_stateMutex);
    if (!m_settings)
      return false;
    if (m_saveDisabled && !ignoreSaveDisabled)
      return false;

    settingsPath = m_settingsPath;
    fencesPath = m_fencesPath;
    autoStartValue = m_autoStart.load();
    minimizeToTray = m_minimizeToTray;
    theme = m_theme;
    iconTextVisible = m_iconTextVisible;
    layoutLocked = m_layoutLocked;
    windowGeometry = m_windowGeometry;
    windowMaximized = m_windowMaximized;
    fencesData = m_fencesData;
    fencesDirty = m_fencesDirty;
  }

  QSettings localSettings(settingsPath, QSettings::IniFormat);
  localSettings.setValue("General/AutoStart", autoStartValue);
  localSettings.setValue("General/MinimizeToTray", minimizeToTray);
  localSettings.setValue("General/Theme", theme);
  localSettings.setValue("General/IconTextVisible", iconTextVisible);
  localSettings.setValue("General/LayoutLocked", layoutLocked);
  localSettings.setValue("Window/Geometry", windowGeometry);
  localSettings.setValue("Window/Maximized", windowMaximized);
  localSettings.sync();
  if (localSettings.status() != QSettings::NoError) {
    qDebug() << "[ConfigManager] Sync FATAL: failed to write settings";
    return false;
  }

  if (fencesDirty) {
    QSaveFile fencesFile(fencesPath);
    if (fencesFile.open(QIODevice::WriteOnly)) {
      QJsonDocument doc(fencesData);
      QByteArray jsonData = doc.toJson(QJsonDocument::Indented);
      if (fencesFile.write(jsonData) != jsonData.size()) {
        qDebug()
            << "[ConfigManager] Sync FATAL: short write when saving fences file";
        fencesFile.cancelWriting();
        return false;
      }
      if (!fencesFile.commit()) {
        qDebug() << "[ConfigManager] Sync FATAL: failed to commit fences file";
        return false;
      }
      {
        QMutexLocker stateLocker(&m_stateMutex);
        if (m_fencesData == fencesData) {
          m_fencesDirty = false;
        }
      }
      qDebug() << "[ConfigManager] Sync success via QSaveFile";
    } else {
      qDebug()
          << "[ConfigManager] Sync FATAL: failed to open fences file for writing";
      return false;
    }
  }

  return true;
}

ConfigManager::LoadResult ConfigManager::tryLoadJson(const QString &path) {
  QFile file(path);
  if (!file.exists()) {
    return LoadResult::NotExist;
  }

  if (!file.open(QIODevice::ReadOnly)) {
    return LoadResult::IOError;
  }

  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
  file.close();

  if (doc.isNull() || error.error != QJsonParseError::NoError ||
      !doc.isObject()) {
    return LoadResult::ParseError;
  }

  const QJsonObject object = doc.object();

  // 显式校验 fences 键存在且类型为数组；
  // QJsonValue::toArray() 在错误类型下会静默返回空数组，导致结构损坏的文件被当作 EmptyData
  const QJsonValue fencesVal = object.value("fences");
  if (!fencesVal.isArray()) {
    return LoadResult::ParseError;
  }

  {
    QMutexLocker locker(&m_stateMutex);
    m_fencesData = object;
  }

  return fencesVal.toArray().isEmpty() ? LoadResult::EmptyData
                                       : LoadResult::Success;
}

void ConfigManager::load() {
  if (!m_settings)
    return;

  {
    QMutexLocker locker(&m_stateMutex);
    m_autoStart = m_settings->value("General/AutoStart", false).toBool();
    m_minimizeToTray =
        m_settings->value("General/MinimizeToTray", true).toBool();
    m_theme = m_settings->value("General/Theme", "dark").toString();
    m_iconTextVisible =
        m_settings->value("General/IconTextVisible", true).toBool();
    m_layoutLocked =
        m_settings->value("General/LayoutLocked", false).toBool();
    m_windowGeometry = m_settings->value("Window/Geometry", QRect()).toRect();
    m_windowMaximized = m_settings->value("Window/Maximized", false).toBool();
    m_fencesData = QJsonObject();
    m_lastLoadResult = LoadResult::NotExist;
  }

  const LoadResult loadResult = tryLoadJson(m_fencesPath);
  {
    QMutexLocker locker(&m_stateMutex);
    m_lastLoadResult = loadResult;
  }

  switch (loadResult) {
  case LoadResult::NotExist:
    qDebug() << "[ConfigManager] No fencing config found at" << m_fencesPath;
    break;
  case LoadResult::IOError:
    qWarning() << "[ConfigManager] Failed to open fencing config:"
               << m_fencesPath;
    break;
  case LoadResult::ParseError: {
    qWarning() << "[ConfigManager] Failed to parse fencing config:"
               << m_fencesPath;
    // 将损坏文件另存为 .corrupt，防止静默丢失用户数据
    const QString corruptPath = m_fencesPath + ".corrupt";
    if (QFile::exists(corruptPath))
      QFile::remove(corruptPath);
    if (QFile::copy(m_fencesPath, corruptPath)) {
      qWarning() << "[ConfigManager] Corrupt config backed up to:" << corruptPath;
      writeLog("[load] ParseError: corrupt backup saved to " + corruptPath);
    } else {
      writeLog("[load] ParseError: failed to save corrupt backup to " +
               corruptPath);
    }
    break;
  }
  case LoadResult::EmptyData:
    qDebug() << "[ConfigManager] Fencing config loaded with an empty fences "
                "array.";
    break;
  case LoadResult::Success:
    qDebug() << "[ConfigManager] Fencing config loaded successfully.";
    break;
  }

  // 启动时从系统读取真实的各种情况 (异步执行，避免阻塞主线程响应)
  QtConcurrent::run([this]() { this->syncAutoStartWithSystem(); });
}

ConfigManager::LoadResult ConfigManager::lastLoadResult() const {
  QMutexLocker locker(&m_stateMutex);
  return m_lastLoadResult;
}

bool ConfigManager::updateAutoStartRegistry(bool enabled) {
#ifdef Q_OS_WIN
  bool isMsix = isMsixPackage();
  ConfigManager::writeLog(
      QString("[updateAutoStartRegistry] Begin: enabled=%1, isMsix=%2")
          .arg(enabled)
          .arg(isMsix));

  if (isMsix) {
    static const QString kTaskId = "DeskGoStartupTask";
    IStartupTask_Raw *task = GetMsixStartupTask(kTaskId);
    bool winrtSuccess = false;

    if (task) {
      int state = 0;
      if (SUCCEEDED(task->get_State(&state))) {
        ConfigManager::writeLog(
            QString("[updateAutoStartRegistry] WinRT current state=%1")
                .arg(state));
        if (enabled) {
          if (state == 1) { // DisabledByUser
            ConfigManager::writeLog(
                "[updateAutoStartRegistry] WinRT: DisabledByUser. Alerting.");
            emit autoStartStatusMessage(
                tr("Cannot enable startup"),
                tr("You have previously disabled this app in Task Manager. "
                   "Please go to Task Manager > Startup tab and set it to "
                   "\"Enabled\" first."));
            task->Release();
            return false;
          } else if (state == 2 || state == 4) {
            winrtSuccess = true;
          } else {
            IAsyncOperation_StartupTaskState_Raw *op = nullptr;
            HRESULT hr = task->RequestEnableAsync(&op);
            if (SUCCEEDED(hr) && op) {
              if (WaitAsyncInfo(op, "RequestEnableAsync")) {
                int newState = 0;
                if (SUCCEEDED(op->GetResults(&newState))) {
                  ConfigManager::writeLog(
                      QString("[updateAutoStartRegistry] RequestEnableAsync "
                              "result=%1")
                          .arg(newState));
                  winrtSuccess = (newState == 2 || newState == 4);
                }
              }
              op->Release();
            } else {
              ConfigManager::writeLog(
                  QString("[updateAutoStartRegistry] WinRT RequestEnableAsync "
                          "returned NULL op. HRESULT=%1")
                      .arg(QString::number(hr, 16)));
              // 此处不返回失败，尝试走下方的注册表回退
            }
          }
        } else {
          task->Disable();
          ConfigManager::writeLog("[updateAutoStartRegistry] WinRT disabled.");
          winrtSuccess = false;
          // MSIX 下关闭时，我们也顺便清理注册表以保持一致
        }
      }
      task->Release();
    }

    // 如果 WinRT 路径成功或者是关闭操作，我们直接返回
    if (!enabled) {
      // 继续执行下方的注册表清理逻辑
    } else if (winrtSuccess) {
      ConfigManager::writeLog("[updateAutoStartRegistry] WinRT path success.");
      return true;
    } else {
      ConfigManager::writeLog("[updateAutoStartRegistry] WinRT path failed or "
                              "skipped, trying Registry fallback...");
    }
  }

  // 方案 2: 注册表方案 (作为普通版的主方案，以及 MSIX 版的回退方案)
  QString appName = "DeskGo";
  QString appPath =
      QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
  HKEY hKey;
  LPCWSTR runPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
  bool regSuccess = false;

  if (RegOpenKeyExW(HKEY_CURRENT_USER, runPath, 0, KEY_ALL_ACCESS, &hKey) ==
      ERROR_SUCCESS) {
    if (enabled) {
      std::wstring wPath = L"\"" + appPath.toStdWString() + L"\"";
      LONG res = RegSetValueExW(
          hKey, appName.toStdWString().c_str(), 0, REG_SZ,
          reinterpret_cast<const BYTE *>(wPath.c_str()),
          static_cast<DWORD>((wPath.length() + 1) * sizeof(wchar_t)));
      regSuccess = (res == ERROR_SUCCESS);
      ConfigManager::writeLog(
          QString("[updateAutoStartRegistry] Registry set %1. Result=%2")
              .arg(enabled)
              .arg(res));
    } else {
      LONG res = RegDeleteValueW(hKey, appName.toStdWString().c_str());
      regSuccess = false; // 关闭操作返回 false
      ConfigManager::writeLog(
          QString("[updateAutoStartRegistry] Registry deleted. Result=%1")
              .arg(res));
    }
    RegCloseKey(hKey);
  } else {
    ConfigManager::writeLog(
        "[updateAutoStartRegistry] Failed to open registry key for writing.");
  }

  return enabled ? regSuccess : false;
#else
  return enabled;
#endif
}

void ConfigManager::syncAutoStartWithSystem() {
#ifdef Q_OS_WIN
  bool actualSystemState = false;
  bool msixDetected = isMsixPackage();

  if (msixDetected) {
    static const QString kTaskId = "DeskGoStartupTask";
    IStartupTask_Raw *task = GetMsixStartupTask(kTaskId);
    if (task) {
      int state = 0;
      if (SUCCEEDED(task->get_State(&state))) {
        actualSystemState = (state == 2 || state == 4);
      }
      task->Release();
    }
  }

  // 如果 WinRT 没查到开启，再查一遍注册表（双重保险）
  if (!actualSystemState) {
    QSettings registrySettings(
        "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        QSettings::NativeFormat);
    actualSystemState = registrySettings.contains("DeskGo");
  }

  if (m_autoStart != actualSystemState) {
    ConfigManager::writeLog(QString("[syncAutoStartWithSystem] State mismatch! "
                                    "System=%1, Memory=%2. Updating.")
                                .arg(actualSystemState)
                                .arg(m_autoStart.load()));
    {
      QMutexLocker locker(&m_stateMutex);
      m_autoStart = actualSystemState;
    }
    QSettings localSettings(m_settingsPath, QSettings::IniFormat);
    localSettings.setValue("General/AutoStart", actualSystemState);
    localSettings.sync();
  }
  emit autoStartChanged(actualSystemState);
#endif
}
