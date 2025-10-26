set "fixed_string=V6.01.00RC8"
set "extension=.uf2"
set "directory=../"
ren buildRP2040F build
cd build
cmake -G "NMake Makefiles" -DCOMPILE=PICO ..
nmake
set "filename=PicoMiteRP2040"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
::
cmake -G "NMake Makefiles" -DCOMPILE=PICOUSB ..
nmake
set "filename=PicoMiteRP2040USB"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
::
cmake -G "NMake Makefiles" -DCOMPILE=VGA ..
nmake
set "filename=PicoMiteRP2040VGA"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
::
cmake -G "NMake Makefiles" -DCOMPILE=VGAUSB ..
nmake
set "filename=PicoMiteRP2040VGAUSB"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
::
cmake -G "NMake Makefiles" -DCOMPILE=WEB ..
nmake
set "filename=WebMiteRP2040"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
::
cd ..
timeout /t 20 /nobreak
ren build buildRP2040F
ren buildRP2350F build
cd build
::
cmake -G "NMake Makefiles" -DCOMPILE=WEBRP2350 ..
nmake
set "filename=WebMiteRP2350"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
::
cmake -G "NMake Makefiles" -DCOMPILE=VGARP2350 ..
nmake
set "filename=PicoMiteRP2350VGA"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
::
cmake -G "NMake Makefiles" -DCOMPILE=VGAUSBRP2350 ..
nmake
set "filename=PicoMiteRP2350VGAUSB"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
::
cmake -G "NMake Makefiles" -DCOMPILE=PICORP2350 ..
nmake
set "filename=PicoMiteRP2350"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
::
cmake -G "NMake Makefiles" -DCOMPILE=PICOUSBRP2350 ..
nmake
set "filename=PicoMiteRP2350USB"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
::
cmake -G "NMake Makefiles" -DCOMPILE=HDMI ..
nmake
set "filename=PicoMiteHDMI"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
::
cmake -G "NMake Makefiles" -DCOMPILE=HDMIUSB ..
nmake
set "filename=PicoMiteHDMIUSB"
if exist "%filename%%fixed_string%%extension%" del "%filename%%fixed_string%%extension%"
copy "PicoMite.uf2" "%directory%%filename%%fixed_string%%extension%"
cd ..
timeout /t 20 /nobreak
ren build buildRP2350F




