#include "src/core/fencemanager.h"
#include "src/core/configmanager.h"
#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QLocale>
#include <QSessionManager>
#include <QTranslator>

#ifdef Q_OS_WIN
#include <objbase.h>
#include <windows.h>
#endif
#include <QDir>
#include <QLockFile>
#include <QMessageBox>

int main(int argc, char *argv[]) {
  // 启用高 DPI 缩放，解决 2K/4K 屏幕文字模糊问题
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

  QApplication a(argc, argv);

#ifdef Q_OS_WIN
  // 初始化 COM 环境 (STA 模式，适用于 GUI 应用)
  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
#endif

  // 设置应用程序信息
  a.setApplicationName("DeskGo");
  a.setApplicationVersion("1.7.0");
  a.setOrganizationName("DeskGo");

  QIcon appIcon(":/icons/app.ico");
  if (appIcon.isNull()) {
    appIcon = QIcon(":/icons/app.png");
  }
  if (!appIcon.isNull()) {
    a.setWindowIcon(appIcon);
  }

  // 解析参数
  bool isAutostart = false;
  QStringList args = a.arguments();
  qDebug() << "[Main] Application started with args:" << args;
  if (args.contains("--autostart")) {
    isAutostart = true;
    qDebug() << "[Main] Launch triggered by Autostart.";
  }

  // 加载本地化翻译
  QTranslator translator;
  const QStringList uiLanguages = QLocale::system().uiLanguages();
  for (const QString &locale : uiLanguages) {
    const QString localeName = QLocale(locale).name();
    if (translator.load(QLatin1String(":/i18n/deskgo_") + localeName) ||
        translator.load(QLatin1String(":/i18n/deskgo_") + localeName.left(2))) {
      a.installTranslator(&translator);
      break;
    }
  }

  // 单实例检查
  // 使用系统临时目录下的锁文件
  static QLockFile lockFile(
      QDir::temp().absoluteFilePath("DeskGo_SingleInstance.lock"));
  // 尝试锁定，如果失败则说明已经有实例在运行
  // 设置 stale 锁定时间为 0，但这主要用于 tryLock 检测所有者 PID。
  // tryLock(100) 尝试 100 毫秒
  if (!lockFile.tryLock(100)) {
    if (!isAutostart) {
      QMessageBox::warning(nullptr, "DeskGo",
                           QObject::tr("DeskGo is already running."));
    } else {
      qDebug()
          << "[Main] Already running, autostart instance exiting silently.";
    }
    return 0;
  }

  // 设置不随最后一个窗口关闭而退出（因为我们用托盘）
  a.setQuitOnLastWindowClosed(false);

  // 初始化围栏管理器
  FenceManager::instance()->initialize();

  QObject::connect(&a, &QGuiApplication::commitDataRequest, &a,
                   [](QSessionManager &manager) {
                     Q_UNUSED(manager);
                     FenceManager::instance()->saveFences();
                     ConfigManager::instance()->forceSync();
                   });

  int ret = a.exec();

  // 显式清理资源，确保在 QApplication 析构前完成
  FenceManager::instance()->shutdown();

#ifdef Q_OS_WIN
  CoUninitialize();
#endif

  return ret;
}
