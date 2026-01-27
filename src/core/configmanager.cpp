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
    , m_settings(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat)
{
    load();
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
    m_settings.setValue("General/AutoStart", m_autoStart);
    m_settings.setValue("General/MinimizeToTray", m_minimizeToTray);
    m_settings.setValue("General/Theme", m_theme);

    m_settings.setValue("Window/Geometry", m_windowGeometry);
    m_settings.setValue("Window/Maximized", m_windowMaximized);

    // 保存围栏数据到JSON文件
    // 保存围栏数据到JSON文件
    QString dataPath = QCoreApplication::applicationDirPath();
    // QDir().mkpath(dataPath); // 程序目录肯定存在
    QString filePath = dataPath + "/fences.json";

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(m_fencesData);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
}

void ConfigManager::load()
{
    m_autoStart = m_settings.value("General/AutoStart", false).toBool();
    m_minimizeToTray = m_settings.value("General/MinimizeToTray", true).toBool();
    m_theme = m_settings.value("General/Theme", "dark").toString();

    m_windowGeometry = m_settings.value("Window/Geometry", QRect()).toRect();
    m_windowMaximized = m_settings.value("Window/Maximized", false).toBool();

    // 加载围栏数据
    // 加载围栏数据
    QString dataPath = QCoreApplication::applicationDirPath();
    QString filePath = dataPath + "/fences.json";

    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        m_fencesData = doc.object();
        file.close();
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
