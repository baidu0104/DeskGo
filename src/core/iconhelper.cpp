#include "iconhelper.h"
#include "configmanager.h"
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QRect>
#include <QRgb>
#include <QUuid>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <commoncontrols.h>
#include <QtWin>

// 定义 IImageList 接口 ID (与原生 Windows SDK 一致)
static const GUID IID_IImageList_LOCAL = { 0x46EB5926, 0x582E, 0x4017, { 0x9F, 0xDF, 0xE8, 0x99, 0x8D, 0xAA, 0x09, 0x50 } };

#ifndef SHIL_JUMBO
#define SHIL_JUMBO 0x4
#endif
#ifndef SHIL_EXTRALARGE
#define SHIL_EXTRALARGE 0x2
#endif
#endif

QPixmap IconHelper::getWinIcon(const QString &path)
{
#ifdef Q_OS_WIN
    SHFILEINFOW sfi;
    memset(&sfi, 0, sizeof(sfi));

    
    // 强制使用绝对路径
    QString nativePath = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    
    SHGetFileInfoW((const wchar_t*)nativePath.utf16(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX);

    IImageList *piml = nullptr;
    // 获取超大图标列表 (SHIL_JUMBO 通常是 256x256)
    HRESULT hr = SHGetImageList(SHIL_JUMBO, IID_IImageList_LOCAL, (void**)&piml);
    if (FAILED(hr)) {
        // 退而求其次
        hr = SHGetImageList(SHIL_EXTRALARGE, IID_IImageList_LOCAL, (void**)&piml);
    }

    if (SUCCEEDED(hr) && piml) {
        HICON hIcon = nullptr;
        hr = piml->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &hIcon);
        if (SUCCEEDED(hr) && hIcon) {
            QPixmap pixmap = QtWin::fromHICON(hIcon);
            DestroyIcon(hIcon);
            piml->Release();
            // 获取后进行透明裁剪，紧凑化图标
            return cropTransparent(pixmap);
        }
        piml->Release();
    }
#endif
    return QPixmap();
}

QPixmap IconHelper::cropTransparent(const QPixmap& pixmap)
{
    if (pixmap.isNull()) return pixmap;

    QImage img = pixmap.toImage();
    int minX = img.width();
    int minY = img.height();
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(img.pixel(x, y)) > 0) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    if (maxX == -1) return pixmap; // 全透明或空

    // 增加一点边距
    int margin = 2;
    minX = qMax(0, minX - margin);
    minY = qMax(0, minY - margin);
    maxX = qMin(img.width() - 1, maxX + margin);
    maxY = qMin(img.height() - 1, maxY + margin);

    return pixmap.copy(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

QString IconHelper::toStoragePath(const QString& path, const QString& fenceId)
{
    QString storageRoot = ConfigManager::instance()->fencesStoragePath();
    QString fenceDir = storageRoot + "/" + fenceId;
    fenceDir = QDir::toNativeSeparators(QDir::cleanPath(fenceDir));
    
    if (path.startsWith(fenceDir, Qt::CaseInsensitive)) {
        return "storage:" + QFileInfo(path).fileName();
    }
    return path;
}

QString IconHelper::fromStoragePath(const QString& path, const QString& fenceId)
{
    if (path.startsWith("storage:")) {
        QString fileName = path.mid(8);
        QString storageRoot = ConfigManager::instance()->fencesStoragePath();
        QString fullPath = storageRoot + "/" + fenceId + "/" + fileName;
        return QDir::toNativeSeparators(QDir::cleanPath(fullPath));
    }
    return path;
}
