@echo off
setlocal enabledelayedexpansion

:: Record start time
set "start_time=%time%"
echo Build started at: %start_time%
echo.

set "fixed_string=V6.03.00B2"
set "extension=.uf2"
set "directory=../"
set "generator=NMake Makefiles"
set "artifact=PicoMite.uf2"
set "mapfile=PicoMite.elf.map"
set "hexfile=PicoMite.hex"
set "failed_target="
set "active_build_dir="
set "exit_code=0"

call :activate_build_dir "buildRP2040L"
if errorlevel 1 goto :fail

call :build_targets ^
    "PICO:PicoMiteRP2040" ^
    "PICOMIN:PicoMiteRP2040MIN" ^
    "PICOUSB:PicoMiteRP2040USB" ^
    "VGA:PicoMiteRP2040VGA" ^
    "VGAUSB:PicoMiteRP2040VGAUSB" ^
    "WEB:WebMiteRP2040"
if errorlevel 1 goto :fail

call :deactivate_build_dir "buildRP2040L"
if errorlevel 1 goto :fail

call :activate_build_dir "buildRP2350L"
if errorlevel 1 goto :fail

call :build_targets ^
    "WEBRP2350:WebMiteRP2350" ^
    "VGARP2350:PicoMiteRP2350VGA" ^
    "VGAUSBRP2350:PicoMiteRP2350VGAUSB" ^
    "PICORP2350:PicoMiteRP2350" ^
    "PICOUSBRP2350:PicoMiteRP2350USB" ^
    "HDMI:PicoMiteHDMI" ^
    "HDMIUSB:PicoMiteHDMIUSB"
if errorlevel 1 goto :fail

call :deactivate_build_dir "buildRP2350L"
if errorlevel 1 goto :fail

goto :summary

:fail
set "exit_code=1"
echo.
echo Build failed for target: %failed_target%
if defined active_build_dir if exist build call :deactivate_build_dir "%active_build_dir%" >nul 2>&1

:summary

:: Record end time and calculate elapsed time
set "end_time=%time%"
echo.
echo ========================================
echo Build completed at: %end_time%
echo Build started at:   %start_time%

:: Calculate elapsed time
call :elapsed_time "%start_time%" "%end_time%"

endlocal
exit /b %exit_code%

:activate_build_dir
set "active_build_dir=%~1"
ren "%~1" build || exit /b 1
cd build || exit /b 1
exit /b 0

:deactivate_build_dir
cd .. || exit /b 1
timeout /t 20 /nobreak >nul
ren build "%~1" || exit /b 1
set "active_build_dir="
exit /b 0

:build_targets
if "%~1"=="" exit /b 0
for /f "tokens=1,2 delims=:" %%A in (%1) do call :build_target "%%~A" "%%~B"
if errorlevel 1 exit /b 1
shift
goto :build_targets

:build_target
set "compile=%~1"
set "filename=%~2"
set "failed_target=%compile%"

echo ========================================
echo Building %compile%
echo ========================================

cmake -G "%generator%" -DCOMPILE=%compile% .. || exit /b 1
nmake || exit /b 1

if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "%artifact%" "%directory%%filename%%fixed_string%%extension%" >nul || exit /b 1
echo "%directory%%filename%%fixed_string%%extension%"
findstr /B ".heap" "%mapfile%"
python ../GetHighestHexAddress.py "%hexfile%"
echo.
exit /b 0

:elapsed_time
setlocal
:: Parse start time
set "start=%~1"
for /f "tokens=1-4 delims=:., " %%a in ("%start%") do (
    set /a "start_h=%%a"
    set /a "start_m=%%b"
    set /a "start_s=%%c"
    set /a "start_ms=%%d"
)

:: Parse end time
set "end=%~2"
for /f "tokens=1-4 delims=:., " %%a in ("%end%") do (
    set /a "end_h=%%a"
    set /a "end_m=%%b"
    set /a "end_s=%%c"
    set /a "end_ms=%%d"
)

:: Convert to total centiseconds (1/100th of a second)
set /a "start_cs=(start_h*360000)+(start_m*6000)+(start_s*100)+start_ms"
set /a "end_cs=(end_h*360000)+(end_m*6000)+(end_s*100)+end_ms"

:: Handle midnight rollover
if %end_cs% lss %start_cs% set /a "end_cs+=8640000"

:: Calculate difference
set /a "diff_cs=end_cs-start_cs"

:: Convert back to hours, minutes, seconds
set /a "hours=diff_cs/360000"
set /a "remainder=diff_cs%%360000"
set /a "minutes=remainder/6000"
set /a "remainder=remainder%%6000"
set /a "seconds=remainder/100"
set /a "centisecs=remainder%%100"

:: Format with leading zeros
if %hours% lss 10 set "hours=0%hours%"
if %minutes% lss 10 set "minutes=0%minutes%"
if %seconds% lss 10 set "seconds=0%seconds%"
if %centisecs% lss 10 set "centisecs=0%centisecs%"

echo Elapsed time:       %hours%:%minutes%:%seconds%.%centisecs%
echo ========================================
endlocal
goto :eof