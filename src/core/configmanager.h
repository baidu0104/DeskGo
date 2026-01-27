#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QJsonObject>
#include <QSettings>
#include <QRect>

/**
 * @brief 配置管理器
 * 管理应用设置和围栏布局数据的持久化
 */
class ConfigManager : public QObject
{
    Q_OBJECT

public:
    static ConfigManager* instance();

    // 通用设置
    bool autoStart() const;
    void setAutoStart(bool enabled);

    bool minimizeToTray() const;
    void setMinimizeToTray(bool enabled);

    QString theme() const;
    void setTheme(const QString &theme);

    // 窗口状态
    QRect windowGeometry() const;
    void setWindowGeometry(const QRect &geometry);

    bool windowMaximized() const;
    void setWindowMaximized(bool maximized);

    // 围栏数据
    QJsonObject fencesData() const;
    void setFencesData(const QJsonObject &data);

    // 保存/加载
    void save();
    void load();

signals:
    void autoStartChanged(bool enabled);
    void themeChanged(const QString &theme);

private:
    explicit ConfigManager(QObject *parent = nullptr);
    ~ConfigManager() = default;

    void updateAutoStartRegistry(bool enabled);

    QSettings m_settings;
    
    bool m_autoStart = false;
    bool m_minimizeToTray = true;
    QString m_theme = "dark";
    QRect m_windowGeometry;
    bool m_windowMaximized = false;
    QJsonObject m_fencesData;
};

#endif // CONFIGMANAGER_H
