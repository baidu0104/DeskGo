QT       += core gui widgets winextras concurrent

CONFIG += c++17

# Windows 平台特定设置
win32 {
    LIBS += -ldwmapi -luser32 -ladvapi32 -lgdi32 -lole32 -lshell32
    RC_ICONS = resources/icons/app.ico
}

# 源文件
SOURCES += \
    main.cpp \
    src/ui/fencewindow.cpp \
    src/ui/flowlayout.cpp \
    src/ui/iconwidget.cpp \
    src/core/fencemanager.cpp \
    src/core/configmanager.cpp \
    src/platform/blurhelper.cpp \
    src/platform/desktophelper.cpp \
    src/core/iconhelper.cpp

# 头文件
HEADERS += \
    src/ui/fencewindow.h \
    src/ui/flowlayout.h \
    src/ui/iconwidget.h \
    src/core/fencemanager.h \
    src/core/configmanager.h \
    src/platform/blurhelper.h \
    src/platform/desktophelper.h \
    src/ui/stylehelper.h \
    src/core/iconhelper.h

# 资源文件
RESOURCES += \
    resources/resources.qrc

# 包含路径
INCLUDEPATH += \
    src/ui \
    src/core \
    src/platform

# 输出目录
DESTDIR = $$PWD/bin

# 默认部署规则
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# Trigger Rebuild for Resources - Added PNG

# 国际化翻译文件
TRANSLATIONS += \
    i18n/deskgo_zh_CN.ts \
    i18n/deskgo_en_US.ts
