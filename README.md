# PicoCalc

## Try MMBasic in your browser

**[jvanderberg.github.io/PicoMiteAllVersions](https://jvanderberg.github.io/PicoMiteAllVersions/)**

The full PicoMite runtime — interpreter + bytecode VM — compiled to
WebAssembly and running on a dedicated Web Worker with a shared-memory
framebuffer. Includes a persistent `/sd/` drive (IDBFS-backed, with
drag-and-drop import, per-file download, and an in-browser CodeMirror
editor for `.bas` files), `PLAY` via Web Audio, and all of the bundled
demos. Source under [`host/web/`](host/web/); see
[`docs/web-host-plan.md`](docs/web-host-plan.md) for architecture notes.

---

Information Command | Decription
:--- | :---
MM.INFO(BATTERY) | <ins>PICOCALC ONLY</ins> <br/> Returns the current battery level percentage (0-100).
MM.INFO(CHARGING) | <ins>PICOCALC ONLY</ins> <br/> Returns 1 if battery is charging on external power, 0 if battery is not charging.

Option Command | Decription
:--- | :---
OPTION BACKLIGHT KB brightness | <ins>PICOCALC ONLY</ins> <br/> Sets the brightness of the keyboard backlight. 'brightness' is a value between 0 (backlight off) and 255 (maximum brightness).

NEW PICOCALC USERS
------------------
Please download and install the [newest release](https://github.com/madcock/PicoMiteAllVersions/releases).
If installing on a brand new PicoCalc, your keyboard firmware/bios is probably out of date. There is no easy way to tell which firmware is already installed, but if it's the "old" one, the PicoCalc specific ``MM.INFO()`` commands listed above won't work, and you'll probably get constant i2c keyboard disconnect errors which will make the device unusable. _It is highly recommended to update your keyboard firmware!_

Use the [official guide to update your keyboard firmware](https://github.com/clockworkpi/PicoCalc/wiki/Setting-Up-Arduino-Development-for-PicoCalc-keyboard). There's a lot of extra information there which you can ignore unless you want to develop your own keyboard firmware. All you need to do is download ``STM32CubeProgrammer``, the newest keyboard firmware binary ([currently 1.4](https://github.com/clockworkpi/PicoCalc/blob/master/Bin/PicoCalc_BIOS_v1.4.bin) but please check to make sure there isn't anything newer), install it as described in the document using the dipswitch, and then reassemble everything carefully. Make sure you've put the dipswitch back in its original position after flashing the BIOS update. You'll only ever need to do this once, or perhaps again if another critical update is released. But regular PicoMite firmware updates do not require this keyboard BIOS update and it won't be lost if your batteries are removed, etc.

Any assembly/disassembly of the PicoCalc risks damaging the extremely fragile screen. Once it's damaged, there's no way to fix it, and a replacement will be needed. if necessary, contact [alex@clockworkpi.com](mailto:alex@clockworkpi.com) for a replacement, and give him your original order invoice details and (usually picture) proof of screen damage. Be _very_ careful the display is seated properly when assembling! I recommend taping the screen down as [described in this post](https://forum.clockworkpi.com/t/before-replacing-the-pico-read-this-to-avoid-cracked-screen/16666/10). Electrical tape and kapton tape have both proven to work. The important thing is to never reattach the back with screws unless you are certain the screen is seated properly.

INSTALL PICO SDK
----------------
```bash
sudo apt update && sudo apt install -y cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential git

mkdir -p ~/pico && cd ~/pico
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git checkout tags/2.2.0 -b sdk2.2.0
git submodule update --init

echo 'export PICO_SDK_PATH=~/pico/pico-sdk' >> ~/.bashrc
source ~/.bashrc
```

SETUP PICOCALC FIRMWARE
-----------------------
```bash
mkdir -p ~/picocalc && cd ~/picocalc
git clone https://github.com/madcock/PicoMiteAllVersions.git
cd PicoMiteAllVersions
```

(No pico-sdk patching is required. The RAM-resident GPIO IRQ dispatcher
that previously needed an `hardware_gpio/gpio.c` swap now lives in
`picomite_gpio_irq.c` and is linked in by the build.)

EDIT ``~/picocalc/PicoMiteAllVersions/CMakeLists.txt`` TO CHOOSE TARGET
-----------------------------------------------------------------------
```makefile
set(PICOCALC true)

# Compile for PICO 1 Board
#set(COMPILE PICO)

# Compile for PICO 2 Board
#set(COMPILE PICORP2350)
set(COMPILE WEBRP2350)
```

BUILD PICOCALC FIRMWARE
-----------------------
```bash
cd ~/picocalc/PicoMiteAllVersions
mkdir build
cd build
cmake ..
make
```

LOCAL VM PROTOTYPE BUILD/TEST LOOP
----------------------------------
This workspace currently uses a host oracle plus bytecode VM harness for BASIC regression testing.

Current architecture reference:
- [docs/vm-architecture.md](docs/vm-architecture.md)
- [docs/vm-cutover-plan.md](docs/vm-cutover-plan.md)
- [docs/vm-command-coverage.md](docs/vm-command-coverage.md)

```bash
cd ~/picocalc/PicoMiteAllVersions
make -C host
./host/run_tests.sh
./host/run_pixel_tests.sh
./host/run_host_shim_tests.sh
./host/run_frontend_tests.sh
./host/run_optimizer_tests.sh
bash host/run_unsupported_tests.sh
./host/run_missing_syscall_tests.sh
make -C build2350 -j8
arm-none-eabi-size build2350/PicoMite.elf
```

The host binary is `host/mmbasic_test`. Its default mode compares the legacy host interpreter against the bytecode VM. Use `--interp` for the interpreter oracle only, `--vm` for the current VM path, `--vm-source` for the VM-owned raw-source frontend, and `--source-compare` to compare the legacy oracle against that frontend.

For keyboard-driven host tests, `mmbasic_test` supports `--keys TEXT` and `--keys-after-ms MS TEXT`.

`RUN` compiles source to bytecode with the VM-owned frontend and executes on the VM. `FRUN` and the interpreter bridge fallback are removed from the user-facing VM path. The legacy prompt handles shell/OS commands.

(_original readme follows..._)

# PicoMiteRP2350
This contains files to build MMbasic 6.01.00b10 to run on both RP2040 and RP2350<br>
Compile with GCC 13.3.1 arm-none-eabi<br>

<b style="color:red;"> Build with sdk V2.2.0. (Note: in this fork the pico-sdk gpio.c / gpio.h replacement is no longer needed — the RAM-resident GPIO IRQ dispatcher lives in <code>picomite_gpio_irq.c</code>.)<br></b>

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
set(COMPILE WEBRP2350)<br>
<br>
Any of the RP2350 variants or the RP2040 variants can be built by simply changing the set(COMPILE aaaa)<br>
However, to swap between a rp2040 build and a rp2350 build (or visa versa) needs a different build directory.
The process for doing this is as follows:<br>
Close VSCode<br>
Rename the current build directory - e.g. build -> buildrp2040<br>
Rename the inactive build directory - e.g. buildrp2350 -> build<br>
edit CMakeLists.txt to choose a setting for the other chip and save it - e.g.  set(COMPILE PICO) -> set(COMPILE PICORP2350)<br>
Restart VSCode<br>
