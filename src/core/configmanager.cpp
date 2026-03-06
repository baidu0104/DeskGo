#include "configmanager.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QFile>
#include <QStandardPaths>
#include <QDir>

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
{
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configPath);
    m_settingsPath = configPath + "/user_settings.ini";
    m_fencesPath = configPath + "/fencing_config.json";

    // fences_storage 与配置文件同目录（AppConfigLocation 下），统一路径
    m_fencesStoragePath = configPath + "/fences_storage";
    QDir().mkpath(m_fencesStoragePath);

    m_settings = new QSettings(m_settingsPath, QSettings::IniFormat, this);

    load();
}

ConfigManager::~ConfigManager()
{
    // QSettings is a QObject, so it will be deleted when its parent (this) is deleted.
    // No explicit delete m_settings; needed if 'this' is the parent.
    // If 'this' is not the parent, then delete m_settings; would be required.
    // In this case, 'this' is the parent, so it's handled.
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
        save();
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
        save();
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
        save();
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
        save();
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
    save();
}

void ConfigManager::save()
{
    if (!m_settings) return;

    m_settings->setValue("General/AutoStart", m_autoStart);
    m_settings->setValue("General/MinimizeToTray", m_minimizeToTray);

    m_settings->setValue("General/Theme", m_theme);
    m_settings->setValue("General/IconTextVisible", m_iconTextVisible);

    m_settings->setValue("Window/Geometry", m_windowGeometry);
    m_settings->setValue("Window/Maximized", m_windowMaximized);

    // 使用原子写入保存围栏数据到JSON文件
    // 先写入临时文件，然后重命名，确保配置文件的完整性
    QString tempPath = m_fencesPath + ".tmp";
    QFile tempFile(tempPath);
    if (tempFile.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(m_fencesData);
        QByteArray jsonData = doc.toJson(QJsonDocument::Indented);
        qint64 written = tempFile.write(jsonData);
        tempFile.flush(); // 确保数据写入磁盘
        tempFile.close();
        
        // 只有在完整写入后才替换原文件
        if (written == jsonData.size()) {
            // 删除旧文件（如果存在）
            if (QFile::exists(m_fencesPath)) {
                QFile::remove(m_fencesPath);
            }
            // 重命名临时文件为正式文件
            if (!QFile::rename(tempPath, m_fencesPath)) {
                qDebug() << "[ConfigManager] Failed to rename temp file to" << m_fencesPath;
                // 如果重命名失败，尝试复制
                if (QFile::copy(tempPath, m_fencesPath)) {
                    QFile::remove(tempPath);
                }
            }
        } else {
            qDebug() << "[ConfigManager] Failed to write complete data to temp file";
            QFile::remove(tempPath);
        }
    } else {
        qDebug() << "[ConfigManager] Failed to open temp file for writing:" << tempPath;
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

    // 加载围栏数据
    // 优先读新文件名（fencing_config.json），若不存在则尝试旧文件名（fences.json）兼容
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QString legacyPath = configPath + "/fences.json";

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

    bool loaded = tryLoadJson(m_fencesPath);

    if (!loaded && QFile::exists(legacyPath)) {
        // 迁移旧文件名
        qDebug() << "[ConfigManager] Migrating fences data from fences.json to fencing_config.json";
        if (tryLoadJson(legacyPath)) {
            save(); // 立即保存为新格式
        }
    }

    // 向后兼容：迁移旧 bin/fences_storage 到 AppConfig/fences_storage
    QString oldStorageBase = QCoreApplication::applicationDirPath() + "/fences_storage";
    QDir oldStorageDir(oldStorageBase);
    if (oldStorageDir.exists() && QDir::toNativeSeparators(oldStorageBase) != QDir::toNativeSeparators(m_fencesStoragePath)) {
        qDebug() << "[ConfigManager] Checking legacy bin/fences_storage for migration...";
        for (const QString& subDir : oldStorageDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString oldFenceStorage = oldStorageBase + "/" + subDir;
            QString newFenceStorage = m_fencesStoragePath + "/" + subDir;
            QDir newDir(newFenceStorage);
            if (!newDir.exists()) {
                QDir().mkpath(newFenceStorage);
                QDir srcDir(oldFenceStorage);
                bool allOk = true;
                for (const QString& fileName : srcDir.entryList(QDir::Files)) {
                    QString srcFile = oldFenceStorage + "/" + fileName;
                    QString dstFile = newFenceStorage + "/" + fileName;
                    if (!QFile::exists(dstFile)) {
                        if (!QFile::rename(srcFile, dstFile)) {
                            if (!QFile::copy(srcFile, dstFile)) {
                                qDebug() << "[ConfigManager] Failed to migrate:" << srcFile;
                                allOk = false;
                            } else {
                                QFile::remove(srcFile);
                            }
                        }
                    }
                }
                if (allOk) {
                    srcDir.removeRecursively();
                    qDebug() << "[ConfigManager] Migrated fence storage:" << subDir;
                }
            }
        }
    }
}

void ConfigManager::updateAutoStartRegistry(bool enabled)
{
#ifdef Q_OS_WIN
    QString appName = "DeskGo";
    QString appPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());

    HKEY hKey;
    LPCWSTR runPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";

    if (RegOpenKeyExW(HKEY_CURRENT_USER, runPath, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enabled) {
            std::wstring wPath = appPath.toStdWString();
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
