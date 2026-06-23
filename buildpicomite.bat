@echo off
setlocal enabledelayedexpansion

:: Record start time
set "start_time=%time%"
echo Build started at: %start_time%
echo.

set "fixed_string=V6.03.00RC24"
set "extension=.uf2"
set "directory=../"
set "generator=NMake Makefiles"
set "artifact=PicoMite.uf2"
set "mapfile=PicoMite.elf.map"
set "hexfile=PicoMite.hex"
set "failed_target="
set "active_build_dir="
set "exit_code=0"

:: Recover from a previous interrupted run that left a stray "build" directory
:: (one of buildRP2040L / buildRP2350L was renamed to build but never renamed back)
call :recover_build_dir
if errorlevel 1 (
    endlocal
    exit /b 1
)

:: Target lists: space separated "COMPILE:outputfilename" entries
set "targets_rp2040=PICO:PicoMiteRP2040 PICOMIN:PicoMiteRP2040MIN PICOUSB:PicoMiteRP2040USB VGA:PicoMiteRP2040VGA VGAUSB:PicoMiteRP2040VGAUSB WEB:WebMiteRP2040"
set "targets_rp2350=WEBRP2350:WebMiteRP2350 VGARP2350:PicoMiteRP2350VGA VGAUSBRP2350:PicoMiteRP2350VGAUSB PICORP2350:PicoMiteRP2350 PICOUSBRP2350:PicoMiteRP2350USB PICOBTRP2350:PicoMiteRP2350BT PICOBTHRP2350:PicoMiteRP2350BTH HDMI:PicoMiteHDMI HDMIUSB:PicoMiteHDMIUSB HDMIWEB:PicoMiteHDMIWEB"

:: Parse the optional build target argument
::   (none)   -> build everything
::   rp2040   -> build every RP2040 variant
::   rp2350   -> build every RP2350 variant
::   <COMPILE>-> build a single named variant (e.g. PICO, HDMIWEB)
set "build_arg=%~1"

if /i "%build_arg%"=="rp2040" goto :do_rp2040
if /i "%build_arg%"=="rp2350" goto :do_rp2350
if "%build_arg%"=="" goto :do_all
goto :do_variant

:do_all
echo Building ALL targets
echo.
call :build_group "buildRP2040L" "targets_rp2040" ""
if errorlevel 1 goto :fail
call :build_group "buildRP2350L" "targets_rp2350" ""
if errorlevel 1 goto :fail
goto :summary

:do_rp2040
echo Building all RP2040 targets
echo.
call :build_group "buildRP2040L" "targets_rp2040" ""
if errorlevel 1 goto :fail
goto :summary

:do_rp2350
echo Building all RP2350 targets
echo.
call :build_group "buildRP2350L" "targets_rp2350" ""
if errorlevel 1 goto :fail
goto :summary

:do_variant
:: Validate the requested variant against both lists
set "valid=0"
for %%T in (%targets_rp2040% %targets_rp2350%) do (
    for /f "tokens=1 delims=:" %%A in ("%%T") do (
        if /i "%build_arg%"=="%%A" set "valid=1"
    )
)
if "%valid%"=="0" (
    echo Unknown build target: %build_arg%
    echo.
    echo Usage: buildpicomite.bat [ rp2040 ^| rp2350 ^| ^<variant^> ]
    echo   no argument  - build all targets
    echo   rp2040       - build all RP2040 variants
    echo   rp2350       - build all RP2350 variants
    echo.
    echo Valid variants:
    echo   RP2040: PICO PICOMIN PICOUSB VGA VGAUSB WEB
    echo   RP2350: WEBRP2350 VGARP2350 VGAUSBRP2350 PICORP2350 PICOUSBRP2350 PICOBTRP2350 PICOBTHRP2350 HDMI HDMIUSB HDMIWEB
    endlocal
    exit /b 1
)
echo Building variant %build_arg%
echo.
call :build_group "buildRP2040L" "targets_rp2040" "%build_arg%"
if errorlevel 1 goto :fail
call :build_group "buildRP2350L" "targets_rp2350" "%build_arg%"
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

:recover_build_dir
:: If a leftover "build" directory exists, rename it back to whichever of the
:: real build directories is currently missing.
if not exist "build\" exit /b 0
if not exist "buildRP2040L\" (
    echo Recovering leftover build directory -^> buildRP2040L
    ren build buildRP2040L || exit /b 1
    exit /b 0
)
if not exist "buildRP2350L\" (
    echo Recovering leftover build directory -^> buildRP2350L
    ren build buildRP2350L || exit /b 1
    exit /b 0
)
echo ERROR: a "build" directory exists but both buildRP2040L and buildRP2350L
echo        are also present - cannot determine which one to restore.
exit /b 1

:activate_build_dir
set "active_build_dir=%~1"
ren "%~1" build || exit /b 1
cd build || exit /b 1
exit /b 0

:deactivate_build_dir
cd .. || exit /b 1
:: Dropbox (or an indexer) may briefly hold a handle on files in "build",
:: making the rename fail with "Access is denied". Retry until it succeeds
:: instead of relying on a single fixed wait.
set "ren_attempts=0"
:deactivate_retry
ren build "%~1" 2>nul
if not exist "build\" (
    set "active_build_dir="
    exit /b 0
)
set /a "ren_attempts+=1"
if %ren_attempts% geq 40 (
    echo ERROR: could not rename "build" back to %~1 after %ren_attempts% attempts.
    echo        Something is holding files in the directory ^(try pausing Dropbox^).
    exit /b 1
)
echo Waiting for "build" directory to be released ^(attempt %ren_attempts%^)...
timeout /t 3 /nobreak >nul
goto :deactivate_retry

:build_group
:: %1 = build dir name, %2 = name of target-list variable, %3 = variant filter (empty = all)
set "grp_dir=%~1"
set "grp_list=!%~2!"
set "grp_filter=%~3"

:: Skip the whole group (and its rename/timeout) if nothing in it matches the filter
set "grp_match=0"
for %%T in (%grp_list%) do (
    for /f "tokens=1 delims=:" %%A in ("%%T") do (
        if "!grp_filter!"=="" set "grp_match=1"
        if /i "!grp_filter!"=="%%A" set "grp_match=1"
    )
)
if "%grp_match%"=="0" exit /b 0

call :activate_build_dir "%grp_dir%"
if errorlevel 1 exit /b 1

for %%T in (%grp_list%) do (
    for /f "tokens=1,2 delims=:" %%A in ("%%T") do (
        set "do_build=0"
        if "!grp_filter!"=="" set "do_build=1"
        if /i "!grp_filter!"=="%%A" set "do_build=1"
        if "!do_build!"=="1" (
            call :build_target "%%~A" "%%~B"
            if errorlevel 1 exit /b 1
        )
    )
)

call :deactivate_build_dir "%grp_dir%"
if errorlevel 1 exit /b 1
exit /b 0

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
python ../GetHighestHexAddress.py "%hexfile%" "%mapfile%" || exit /b 1
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
