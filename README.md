# PicoMiteRP2350
This contains files to build MMbasic V6.00.00RC17 to run on both RP2040 and RP2350<br>
Compile with GCC 13.3.1 arm-none-eabi<br>
Replace pico-sdk\src\rp2_common\pico_stdio_usb\stdio_usb_descriptors.c with the version from the development branch of the sdk<br>
Replace  pico-sdk\src\rp2_common\pico_multicore\multicore.c and  pico-sdk\src\rp2_common\pico_multicore\include\pico\multicore.h with the version from the development branch of the sdk<br>

Change CMakeLists.txt line 4 to determine which variant to build<br>
<br>
RP2040<br>
set(COMPILE PICO)<br>
set(COMPILE VGA)<br>
set(COMPILE PICOUSB)<br>
set(COMPILE VGAUSB)<br>
set(COMPILE WEB)<br>
<br>
RP2350<br>
set(COMPILE PICORP2350)<br>
set(COMPILE VGARP2350)<br>
set(COMPILE PICOUSBRP2350)<br>
set(COMPILE VGAUSBRP2350)<br>
set(COMPILE HDMI)<br>
set(COMPILE HDMIUSB)<br>
<br>
Any of the RP2350 variants or the RP2040 variants can be built by simply changing the set(COMPILE aaaa)<br>
However, to swap between a rp2040 build and a rp2350 build (or visa versa) needs a different build directory.
The process for doing this is as follows:<br>
Close VSCode<br>
Rename the current build directory - e.g. build -> buildrp2040<br>
Rename the inactive build directory - e.g. buildrp2350 -> build<br>
edit CMakeLists.txt to choose a setting for the other chip and save it - e.g.  set(COMPILE PICO) -> set(COMPILE PICORP2350)<br>
Restart VSCode<br>

