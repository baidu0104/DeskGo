@echo off
chcp 65001 >nul
echo ========================================
echo    DeskGo 图标恢复工具
echo ========================================
echo.

set "STORAGE_DIR=%~dp0fences_storage"
set "DESKTOP=%USERPROFILE%\Desktop"

echo 正在扫描存储目录: %STORAGE_DIR%
echo 目标桌面: %DESKTOP%
echo.

if not exist "%STORAGE_DIR%" (
    echo [错误] 未找到存储目录: %STORAGE_DIR%
    pause
    exit /b 1
)

echo 找到以下文件:
echo ----------------------------------------
set "FILE_COUNT=0"

for /d %%d in ("%STORAGE_DIR%\*") do (
    for %%f in ("%%d\*.*") do (
        echo   %%~nxf
        set /a FILE_COUNT+=1
    )
)

if %FILE_COUNT%==0 (
    echo [提示] 存储目录中没有找到任何文件。
    pause
    exit /b 0
)

echo ----------------------------------------
echo 共找到 %FILE_COUNT% 个文件。
echo.

set /p CONFIRM="是否将这些文件恢复到桌面? (Y/N): "
if /i not "%CONFIRM%"=="Y" (
    echo 操作已取消。
    pause
    exit /b 0
)

echo.
echo 正在恢复...

set "RESTORED=0"
for /d %%d in ("%STORAGE_DIR%\*") do (
    for %%f in ("%%d\*.*") do (
        echo 正在恢复: %%~nxf
        copy "%%f" "%DESKTOP%\%%~nxf" >nul 2>&1
        if errorlevel 1 (
            echo   [失败] 无法复制 %%~nxf
        ) else (
            set /a RESTORED+=1
            echo   [成功]
        )
    )
)

echo.
echo ========================================
echo 恢复完成! 共恢复 %RESTORED% 个文件到桌面。
echo.
echo 如果确认文件已正确恢复，可以手动删除存储目录:
echo %STORAGE_DIR%
echo ========================================
pause
