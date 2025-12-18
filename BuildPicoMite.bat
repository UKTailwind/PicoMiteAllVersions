@echo off
setlocal enabledelayedexpansion

:: Record start time
set "start_time=%time%"
echo Build started at: %start_time%
echo.

set "fixed_string=V6.01.00"
set "extension=.uf2"
set "directory=../"
ren buildRP2040L build
cd build
cmake -G "NMake Makefiles" -DCOMPILE=PICO ..
nmake
set "filename=PicoMiteRP2040"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
echo "%directory%%filename%%fixed_string%%extension%"
findstr /B ".heap" PicoMite.elf.map
::
python ../GetHighestHexAddress.py PicoMite.hex
::
cmake -G "NMake Makefiles" -DCOMPILE=PICOUSB ..
nmake
set "filename=PicoMiteRP2040USB"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
echo "%directory%%filename%%fixed_string%%extension%"
findstr /B ".heap" PicoMite.elf.map
::
python ../GetHighestHexAddress.py PicoMite.hex
::
cmake -G "NMake Makefiles" -DCOMPILE=VGA ..
nmake
set "filename=PicoMiteRP2040VGA"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
echo "%directory%%filename%%fixed_string%%extension%"
findstr /B ".heap" PicoMite.elf.map
::
python ../GetHighestHexAddress.py PicoMite.hex
::
cmake -G "NMake Makefiles" -DCOMPILE=VGAUSB ..
nmake
set "filename=PicoMiteRP2040VGAUSB"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
echo "%directory%%filename%%fixed_string%%extension%"
findstr /B ".heap" PicoMite.elf.map
::
python ../GetHighestHexAddress.py PicoMite.hex
::
cmake -G "NMake Makefiles" -DCOMPILE=WEB ..
nmake
set "filename=WebMiteRP2040"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
echo "%directory%%filename%%fixed_string%%extension%"
findstr /B ".heap" PicoMite.elf.map
::
python ../GetHighestHexAddress.py PicoMite.hex
::
cd ..
timeout /t 20 /nobreak
ren build buildRP2040L
ren buildRP2350L build
cd build
::
cmake -G "NMake Makefiles" -DCOMPILE=WEBRP2350 ..
nmake
set "filename=WebMiteRP2350"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
echo "%directory%%filename%%fixed_string%%extension%"
findstr /B ".heap" PicoMite.elf.map
::
python ../GetHighestHexAddress.py PicoMite.hex
::
cmake -G "NMake Makefiles" -DCOMPILE=VGARP2350 ..
nmake
set "filename=PicoMiteRP2350VGA"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
echo "%directory%%filename%%fixed_string%%extension%"
findstr /B ".heap" PicoMite.elf.map
::
python ../GetHighestHexAddress.py PicoMite.hex
::
cmake -G "NMake Makefiles" -DCOMPILE=VGAUSBRP2350 ..
nmake
set "filename=PicoMiteRP2350VGAUSB"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
echo "%directory%%filename%%fixed_string%%extension%"
findstr /B ".heap" PicoMite.elf.map
::
python ../GetHighestHexAddress.py PicoMite.hex
::
cmake -G "NMake Makefiles" -DCOMPILE=PICORP2350 ..
nmake
set "filename=PicoMiteRP2350"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
echo "%directory%%filename%%fixed_string%%extension%"
findstr /B ".heap" PicoMite.elf.map
::
python ../GetHighestHexAddress.py PicoMite.hex
::
cmake -G "NMake Makefiles" -DCOMPILE=PICOUSBRP2350 ..
nmake
set "filename=PicoMiteRP2350USB"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
echo "%directory%%filename%%fixed_string%%extension%"
findstr /B ".heap" PicoMite.elf.map
::
python ../GetHighestHexAddress.py PicoMite.hex
::
cmake -G "NMake Makefiles" -DCOMPILE=HDMI ..
nmake
set "filename=PicoMiteHDMI"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
echo "%directory%%filename%%fixed_string%%extension%"
findstr /B ".heap" PicoMite.elf.map
::
python ../GetHighestHexAddress.py PicoMite.hex
::
cmake -G "NMake Makefiles" -DCOMPILE=HDMIUSB ..
nmake
set "filename=PicoMiteHDMIUSB"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
echo "%directory%%filename%%fixed_string%%extension%"
findstr /B ".heap" PicoMite.elf.map
::
python ../GetHighestHexAddress.py PicoMite.hex
::
cd ..
timeout /t 20 /nobreak
ren build buildRP2350L

:: Record end time and calculate elapsed time
set "end_time=%time%"
echo.
echo ========================================
echo Build completed at: %end_time%
echo Build started at:   %start_time%

:: Calculate elapsed time
call :elapsed_time "%start_time%" "%end_time%"

endlocal
goto :eof

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