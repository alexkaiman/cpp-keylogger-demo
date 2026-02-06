@echo off
chcp 1251 >nul
setlocal EnableDelayedExpansion

:: ----------------------------------------------------------------
:: Показ помощи, если передан -h или --help
:: ----------------------------------------------------------------

if /i "%~1"=="-h" goto :show_help
if /i "%~1"=="/h" goto :show_help
if /i "%~1"=="--help" goto :show_help
if /i "%~1"=="-?" goto :show_help
if /i "%~1"=="/?" goto :show_help

goto :skip_help

:show_help
echo.
echo г============================================================¬
echo ¦                  Сборка проектов (build.bat)               ¦
echo L============================================================-
echo.
echo Использование:
echo   build.bat [имя_проекта] [действие]
echo.
echo Примеры:
echo   build.bat                          - обычная сборка (по умолчанию keylogger)
echo   build.bat clean                    - только очистить папку build
echo   build.bat run                      - собрать и сразу запустить программу
echo   build.bat myproject                - собрать проект с именем myproject
echo   build.bat myproject clean          - очистить сборку для проекта myproject
echo   build.bat myproject run            - собрать и запустить myproject.exe
echo.
echo Доступные действия:
echo   (без аргумента)    - полная сборка + копирование .exe в корень
echo   clean              - удалить папку build (очистка предыдущей сборки)
echo   run                - сборка + автоматический запуск .exe после успеха
echo.
echo Дополнительно:
echo   -h  или  --help  или  /?           - показать эту справку
echo.
echo Примеры запуска:
echo   build.bat                          > keylogger.exe
echo   build.bat tetris run               > собрать и запустить tetris.exe
echo   build.bat spacegame clean          > очистить сборку spacegame
echo.
pause
exit /b 0

:skip_help

:: ----------------------------------------------------------------
:: Настройки по умолчанию
:: ----------------------------------------------------------------

set DEFAULT_PROJECT_NAME=keylogger
set MAKE_JOBS=4
set BUILD_DIR=build

:: ----------------------------------------------------------------
:: Обработка аргументов
:: ----------------------------------------------------------------

set PROJECT_NAME=%DEFAULT_PROJECT_NAME%
set ACTION=build

if "%~1"=="" goto :process

:: Первый аргумент — либо имя проекта, либо команда
set ARG1=%~1

if /i "%ARG1%"=="clean" (
    set ACTION=clean
) else if /i "%ARG1%"=="run" (
    set ACTION=run
) else (
    set PROJECT_NAME=%ARG1%
    echo [INFO] Имя проекта: %PROJECT_NAME%
)

:: Второй аргумент (если есть) — команда
if "%~2"=="" goto :process
set ARG2=%~2

if /i "%ARG2%"=="clean" (
    set ACTION=clean
) else if /i "%ARG2%"=="run" (
    set ACTION=run
)

:process

set EXE_NAME=%PROJECT_NAME%.exe

echo.
echo г============================================================¬
echo ¦     Сборка проекта %PROJECT_NAME% (CMake + MinGW)          ¦
echo ¦     Действие: %ACTION%                                     ¦
echo L============================================================-
echo.

:: ----------------------------------------------------------------
:: Проверка инструментов
:: ----------------------------------------------------------------

where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ОШИБКА] CMake не найден
    pause
    exit /b 1
)

where mingw32-make >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ОШИБКА] mingw32-make не найден
    pause
    exit /b 1
)

cd /d "%~dp0"

:: ----------------------------------------------------------------
:: Очистка (если выбрано clean или обычная сборка)
:: ----------------------------------------------------------------

if "!ACTION!"=="clean" goto :do_clean
if exist %BUILD_DIR% (
    echo [INFO] Очистка предыдущей сборки...
    rd /s /q %BUILD_DIR%
)

:do_clean
if "!ACTION!"=="clean" (
    if exist %BUILD_DIR% (
        rd /s /q %BUILD_DIR%
        echo [УСПЕХ] Папка %BUILD_DIR% удалена
    ) else (
        echo [INFO] Папка %BUILD_DIR% уже отсутствует
    )
    echo.
    echo Очистка завершена.
    pause
    exit /b 0
)

:: ----------------------------------------------------------------
:: Обычная сборка
:: ----------------------------------------------------------------

mkdir %BUILD_DIR%
cd %BUILD_DIR%

echo [INFO] Генерация Makefile...
cmake .. -G "MinGW Makefiles"

if %ERRORLEVEL% neq 0 (
    echo [ОШИБКА] CMake завершился с ошибкой
    pause
    exit /b 1
)

echo [INFO] Компиляция (%MAKE_JOBS% потоков)...
mingw32-make -j%MAKE_JOBS%

if %ERRORLEVEL% neq 0 (
    echo [ОШИБКА] Ошибка компиляции
    pause
    exit /b 1
)

echo.
echo г============================================================¬
echo ¦               Сборка завершена успешно                     ¦
echo L============================================================-
echo.

:: Переходим в bin (если есть) или остаёмся
cd bin 2>nul || cd ..

:: Копируем exe в корень
if exist %EXE_NAME% (
    echo [INFO] Копируем %EXE_NAME% в корень проекта...
    copy /Y %EXE_NAME% "..\..\%EXE_NAME%" >nul
    if !ERRORLEVEL! equ 0 (
        echo [УСПЕХ] Скопировано: %EXE_NAME%
        for %%F in (%EXE_NAME%) do echo     Размер: %%~zF байт
    ) else (
        echo [ОШИБКА] Не удалось скопировать
    )
) else (
    echo [ПРЕДУПРЕЖДЕНИЕ] %EXE_NAME% не найден
)

:: ----------------------------------------------------------------
:: Запуск, если выбрано run
:: ----------------------------------------------------------------

if "!ACTION!"=="run" (
    echo.
    echo [INFO] Запуск программы...
    if exist "..\..\%EXE_NAME%" (
        "..\..\%EXE_NAME%"
    ) else if exist %EXE_NAME% (
        %EXE_NAME%
    ) else (
        echo [ОШИБКА] Исполняемый файл не найден
    )
)

echo.
echo Готово. Нажмите любую клавишу...
pause >nul