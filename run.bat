@echo off
rem Runs Artisan from this folder:
rem   run.bat                 rebuild what changed, then open the editor
rem   run.bat <project>       ... and open that project
rem   run.bat --no-build      just open the last build
rem   run.bat --build-only    rebuild, do not open
rem Needs MSYS2 in C:\msys64 with the packages listed in the README.
setlocal EnableDelayedExpansion
set "HERE=%~dp0"
set "BUILD=%HERE%build"
set "MSYS=C:\msys64\ucrt64\bin"
if not exist "%MSYS%\cmake.exe" (
  echo MSYS2 UCRT64 with cmake not found in C:\msys64 - see "Getting started" in README.md
  pause
  exit /b 1
)
rem Qt and the compiler come from MSYS2.
set "PATH=%MSYS%;%PATH%"

set "BUILDNOW=1"
set "OPEN=1"
set "ARGS="
for %%A in (%*) do (
  if /i "%%~A"=="--no-build" (set "BUILDNOW=0") else if /i "%%~A"=="--build-only" (set "OPEN=0") else set "ARGS=!ARGS! "%%~A""
)

if "%BUILDNOW%"=="1" (
  if not exist "%BUILD%\build.ninja" (
    echo Configuring the build ^(the first time downloads SDL3, Lua and the rest^)...
    cmake -S "%HERE:~0,-1%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release >nul || (
      echo Configuring failed.
      pause
      exit /b 1
    )
  )
  echo Building what changed...
  cmake --build "%BUILD%" || (
    echo The build failed - see the messages above.
    pause
    exit /b 1
  )
)

if "%OPEN%"=="1" (
  if not exist "%BUILD%\bin\artisan.exe" (
    echo No build yet: run run.bat without --no-build first.
    pause
    exit /b 1
  )
  start "" "%BUILD%\bin\artisan.exe" %ARGS%
)
exit /b 0
