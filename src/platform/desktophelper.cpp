#include "desktophelper.h"
#include <windows.h>
#include <commctrl.h>
#include <QDebug>
#include <QFileInfo>
#include <QTimer>
#include <QDir>
#include <shlobj.h>

// LVITEM 结构体的特定版本，用于跨进程读取
struct LVITEM_XP {
    UINT mask;
    int iItem;
    int iSubItem;
    UINT state;
    UINT stateMask;
    LPWSTR pszText;
    int cchTextMax;
    int iImage;
    LPARAM lParam;
    int iIndent;
    int iGroupId;
    UINT cColumns;
    UINT puColumns;
};

// 检查当前进程架构与系统架构是否匹配
// 32位程序在64位系统上运行时（WOW64），无法直接读写64位 Explorer 的内存
bool isArchCompatible() {
#ifdef Q_OS_WIN64
    // 64位构建运行在64位系统，总是兼容
    return true;
#else
    // 32位构建
    BOOL isWow64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &isWow64);
    if (isWow64) {
        // 32位程序运行在64位系统上 -> 不兼容 Explorer 操作
        return false;
    }
    // 32位程序运行在32位系统上 -> 兼容
    return true;
#endif
}

void* DesktopHelper::getDesktopListView()
{
    if (!isArchCompatible()) {
        static bool warned = false;
        if (!warned) {
            qDebug() << "DeskGo (32-bit) running on 64-bit OS: Desktop integration disabled.";
            warned = true;
        }
        return NULL;
    }

    HWND hShell = FindWindow(L"Progman", NULL);
    HWND hDefView = FindWindowEx(hShell, NULL, L"SHELLDLL_DefView", NULL);
    
    if (!hDefView) {
        // 如果 Progman 下没找到，尝试在 WorkerW 中查找
        // 这通常发生在 Windows 切换壁纸或者 Windows 10/11 上
        HWND hWorkerW = NULL;
        while ((hWorkerW = FindWindowEx(NULL, hWorkerW, L"WorkerW", NULL)) != NULL) {
             hDefView = FindWindowEx(hWorkerW, NULL, L"SHELLDLL_DefView", NULL);
             if (hDefView) break;
        }
    }
    
    if (hDefView) {
        return FindWindowEx(hDefView, NULL, L"SysListView32", NULL);
    }
    return NULL;
}

QPoint DesktopHelper::getIconPosition(const QString &filePath)
{
    qDebug() << "[getIconPosition] Looking for:" << filePath;
    
    HWND hListView = (HWND)getDesktopListView();
    if (!hListView) {
        qDebug() << "  Failed to get desktop ListView";
        return QPoint(-1, -1);
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hListView, &processId);

    HANDLE hProcess = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, processId);
    if (!hProcess) {
        qDebug() << "  Failed to open process";
        return QPoint(-1, -1);
    }

    int count = (int)::SendMessage(hListView, LVM_GETITEMCOUNT, 0, 0);
    qDebug() << "  Desktop icon count:" << count;
    QPoint pos(-1, -1);

    // 在目标进程分配内存
    const int maxLen = 512;
    void *pText = VirtualAllocEx(hProcess, NULL, maxLen * sizeof(WCHAR), MEM_COMMIT, PAGE_READWRITE);
    void *pItem = VirtualAllocEx(hProcess, NULL, sizeof(LVITEM), MEM_COMMIT, PAGE_READWRITE);
    void *pPoint = VirtualAllocEx(hProcess, NULL, sizeof(POINT), MEM_COMMIT, PAGE_READWRITE);

    if (!pText || !pItem || !pPoint) {
        if (pText) VirtualFreeEx(hProcess, pText, 0, MEM_RELEASE);
        if (pItem) VirtualFreeEx(hProcess, pItem, 0, MEM_RELEASE);
        if (pPoint) VirtualFreeEx(hProcess, pPoint, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        qDebug() << "  Failed to allocate memory in target process";
        return pos;
    }

    WCHAR localBuffer[maxLen];
    QString targetFileName = QFileInfo(filePath).fileName();      // 完整文件名，如 "db.lnk"
    QString targetBaseName = QFileInfo(filePath).completeBaseName(); // 不带扩展名，如 "db"
    qDebug() << "  targetFileName:" << targetFileName << ", targetBaseName:" << targetBaseName;

    for (int i = 0; i < count; ++i) {
        LVITEM item = {0};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.iSubItem = 0;
        item.pszText = (LPWSTR)pText;
        item.cchTextMax = maxLen;

        WriteProcessMemory(hProcess, pItem, &item, sizeof(LVITEM), NULL);
        ::SendMessage(hListView, LVM_GETITEMTEXT, i, (LPARAM)pItem);
        ReadProcessMemory(hProcess, pText, localBuffer, maxLen * sizeof(WCHAR), NULL);

        QString itemText = QString::fromWCharArray(localBuffer);
        
        // 尝试匹配：完全相等，或者等于不带后缀的文件名
        // 桌面图标显示的文字通常不带扩展名
        if (itemText == targetFileName || itemText == targetBaseName) {
            qDebug() << "  MATCH FOUND at index" << i << "itemText:" << itemText;
            // 找到文件，获取位置
            if (::SendMessage(hListView, LVM_GETITEMPOSITION, i, (LPARAM)pPoint)) {
                POINT pt;
                ReadProcessMemory(hProcess, pPoint, &pt, sizeof(POINT), NULL);
                pos = QPoint(pt.x, pt.y);
                qDebug() << "  Position:" << pos;
            }
            break;
        }
    }

    if (pos.x() == -1) {
        qDebug() << "  No match found!";
    }

    VirtualFreeEx(hProcess, pText, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, pItem, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, pPoint, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    return pos;
}

void DesktopHelper::setIconPosition(const QString &filePath, const QPoint &pos, int retryCount)
{
    // 设置位置稍微复杂，因为我们需要知道 index
    if (pos.x() < 0 || pos.y() < 0) {
        return;
    }

    HWND hListView = (HWND)getDesktopListView();
    if (!hListView) {
        return;
    }

    // 获取进程句柄
    DWORD processId = 0;
    GetWindowThreadProcessId(hListView, &processId);
    
    HANDLE hProcess = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, processId);
    if (!hProcess) {
        return;
    }

    // 分配内存
    const int maxLen = 512;
    void *pText = VirtualAllocEx(hProcess, NULL, maxLen * sizeof(WCHAR), MEM_COMMIT, PAGE_READWRITE);
    void *pItem = VirtualAllocEx(hProcess, NULL, sizeof(LVITEM), MEM_COMMIT, PAGE_READWRITE);

    if (!pText || !pItem) {
        if (pText) VirtualFreeEx(hProcess, pText, 0, MEM_RELEASE);
        if (pItem) VirtualFreeEx(hProcess, pItem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return;
    }

    int count = (int)::SendMessage(hListView, LVM_GETITEMCOUNT, 0, 0);
    WCHAR localBuffer[maxLen];
    QString targetFileName = QFileInfo(filePath).fileName();
    QString targetBaseName = QFileInfo(filePath).completeBaseName();
    int targetIndex = -1;

    for (int i = 0; i < count; ++i) {
        LVITEM item = {0};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.iSubItem = 0;
        item.pszText = (LPWSTR)pText;
        item.cchTextMax = maxLen;

        if (!WriteProcessMemory(hProcess, pItem, &item, sizeof(LVITEM), NULL)) continue;
        
        ::SendMessage(hListView, LVM_GETITEMTEXT, i, (LPARAM)pItem);
        
        if (!ReadProcessMemory(hProcess, pText, localBuffer, maxLen * sizeof(WCHAR), NULL)) continue;

        QString itemText = QString::fromWCharArray(localBuffer);
        
        if (itemText == targetFileName || itemText == targetBaseName) {
            targetIndex = i;
            break;
        }
    }

    VirtualFreeEx(hProcess, pText, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, pItem, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    if (targetIndex != -1) {
        ::SendMessage(hListView, LVM_SETITEMPOSITION, targetIndex, MAKELPARAM(pos.x(), pos.y()));
        ::SendMessage(hListView, LVM_UPDATE, targetIndex, 0);
    } else if (retryCount < 3) {
        // 没找到，等待后重试（最多重试3次）
        QTimer::singleShot(500, [filePath, pos, retryCount]() {
            setIconPosition(filePath, pos, retryCount + 1);
        });
    }
}

void DesktopHelper::refreshDesktop()
{
    // 只通知桌面目录更新，避免过度刷新
    wchar_t desktopPath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_DESKTOP, NULL, 0, desktopPath) == S_OK) {
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, desktopPath, NULL);
    }
}

void DesktopHelper::notifyFileRemoved(const QString &filePath)
{
    // 方法1：直接从 ListView 删除图标项（更快）
    HWND hListView = (HWND)getDesktopListView();
    if (hListView) {
        DWORD processId = 0;
        GetWindowThreadProcessId(hListView, &processId);
        
        HANDLE hProcess = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, processId);
        if (hProcess) {
            const int maxLen = 512;
            void *pText = VirtualAllocEx(hProcess, NULL, maxLen * sizeof(WCHAR), MEM_COMMIT, PAGE_READWRITE);
            void *pItem = VirtualAllocEx(hProcess, NULL, sizeof(LVITEM), MEM_COMMIT, PAGE_READWRITE);
            
            if (pText && pItem) {
                int count = (int)::SendMessage(hListView, LVM_GETITEMCOUNT, 0, 0);
                WCHAR localBuffer[maxLen];
                QString targetFileName = QFileInfo(filePath).fileName();
                QString targetBaseName = QFileInfo(filePath).completeBaseName();
                
                for (int i = 0; i < count; ++i) {
                    LVITEM item = {0};
                    item.mask = LVIF_TEXT;
                    item.iItem = i;
                    item.iSubItem = 0;
                    item.pszText = (LPWSTR)pText;
                    item.cchTextMax = maxLen;
                    
                    WriteProcessMemory(hProcess, pItem, &item, sizeof(LVITEM), NULL);
                    ::SendMessage(hListView, LVM_GETITEMTEXT, i, (LPARAM)pItem);
                    ReadProcessMemory(hProcess, pText, localBuffer, maxLen * sizeof(WCHAR), NULL);
                    
                    QString itemText = QString::fromWCharArray(localBuffer);
                    
                    if (itemText == targetFileName || itemText == targetBaseName) {
                        // 找到了，直接刷新这个项（让它消失）
                        ::SendMessage(hListView, LVM_UPDATE, i, 0);
                        break;
                    }
                }
            }
            
            if (pText) VirtualFreeEx(hProcess, pText, 0, MEM_RELEASE);
            if (pItem) VirtualFreeEx(hProcess, pItem, 0, MEM_RELEASE);
            CloseHandle(hProcess);
        }
    }
    
    // 方法2：Shell 通知（确保系统同步）
    QString nativePath = QDir::toNativeSeparators(filePath);
    SHChangeNotify(SHCNE_DELETE, SHCNF_PATHW, nativePath.toStdWString().c_str(), NULL);
}

void DesktopHelper::notifyFileAdded(const QString &filePath)
{
    // 通知系统特定文件被添加
    QString nativePath = QDir::toNativeSeparators(filePath);
    SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW, nativePath.toStdWString().c_str(), NULL);
}
