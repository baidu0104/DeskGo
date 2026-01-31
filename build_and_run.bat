@echo off
echo Building DeskGo...
mingw32-make clean
mingw32-make

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful! Running DeskGo...
    echo =====================================
    echo.
    cd release
    DeskGo.exe
) else (
    echo.
    echo Build failed!
    pause
)
