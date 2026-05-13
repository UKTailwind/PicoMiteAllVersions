# PC386 — Emulation & Toolchain Reference

How to install the cross compiler and emulators, and the canonical command lines for each. This is reference material, not a plan; the plan lives in `../pc386-plan.md`.

## Cross compiler

The kernel builds with a freestanding ELF cross compiler — the system `gcc` on macOS / Linux targets the host ABI and won't produce a bare-metal multiboot kernel.

Install via the project bootstrap:

```sh
./toolchain/pc386/install_cross.sh
```

This script:
1. Detects whether `i686-elf-gcc` is on `PATH`. If yes, exits 0.
2. On macOS, installs from the `nativeos/i686-elf-toolchain` homebrew tap (precompiled, ~30 s).
3. On Linux, prefers the distro package (`gcc-i686-elf` on Arch, similar on others).
4. Fallback path: builds binutils + gcc from source under `~/.local/i686-elf-toolchain` (~20 min).

Verify:

```sh
i686-elf-gcc --version
i686-elf-ld --version
```

## QEMU (primary)

Install:

```sh
brew install qemu              # macOS
sudo apt install qemu-system-x86  # Debian/Ubuntu
```

### Direct kernel boot (development inner loop)

Skips the bootloader entirely — QEMU implements multiboot loading natively. This is the fast path:

```sh
qemu-system-i386 \
  -kernel build/mmbasic.elf \
  -serial stdio \
  -display none \
  -no-reboot -no-shutdown \
  -d guest_errors
```

- `-kernel` — load the multiboot1 ELF directly at boot.
- `-serial stdio` — pipe COM1 to the terminal. Kernel `printf` over serial appears here.
- `-display none` — headless. For visual runs, drop this flag.
- `-no-reboot -no-shutdown` — triple-faults exit QEMU instead of looping.
- `-d guest_errors` — log CPU exceptions to stderr.

### Hard-disk Limine boot

Tests the Limine-installed `C:` hard-disk image:

```sh
./ports/pc386/build.sh
./ports/pc386/build_disks.sh
./ports/pc386/run_limine.sh
```

The normal interactive path is the boot floppy (`run.sh` / `run_floppy.sh`),
which exercises BIOS floppy boot plus runtime FDC access. The Limine path is
kept as a hard-disk installation and bootloader compatibility check.

### GDB debugging

```sh
# Terminal 1
qemu-system-i386 -kernel build/mmbasic.elf -s -S -display none

# Terminal 2
i686-elf-gdb build/mmbasic.elf \
  -ex 'target remote localhost:1234' \
  -ex 'break kmain' \
  -ex 'continue'
```

`-s` exposes the GDB stub on TCP 1234. `-S` halts at boot until GDB attaches.

### Memory size

Default QEMU RAM is 128 MB. Constrain to match real-hardware target shape:

```sh
qemu-system-i386 -m 4M ...    # 4 MB — minimal 386
qemu-system-i386 -m 16M ...   # 16 MB — realistic 486 era
```

## DOSBox-X (sanity check)

Install:

```sh
brew install dosbox-x         # macOS
sudo apt install dosbox-x     # Debian/Ubuntu
```

DOSBox-X cannot do `-kernel`-style direct boot — it needs a bootable disk image. Build the floppy first, then:

```sh
./ports/pc386/build.sh
./ports/pc386/build_disks.sh
./ports/pc386/run_dosbox.sh
```

The launcher writes a temporary config using dynamic CPU emulation and `cycles=max`.
Without that, DOSBox-X can run at conservative 1990s PC speed and the VGA updates are
visibly slow.

The equivalent manual config is:

```ini
[sdl]
output = opengl

[dosbox]
machine = svga_s3
memsize = 16

[cpu]
core = dynamic
cycles = max

[autoexec]
imgmount A "ports/pc386/test_disks/pc386-floppy.img" -t floppy -size 512,18,2,80
boot A:
```

DOSBox-X can expose boot images through BIOS services without fully modelling the
guest-visible floppy/IDE controller path. The pc386 runtime therefore keeps the
native 82077 FDC driver as the normal path, but falls back to BIOS `int 13h`
sector reads for floppy media when direct FDC reads fail. Use QEMU or Bochs for
runtime controller validation; use DOSBox-X mainly as an independent BIOS-boot
sanity check.

Important BIOS-thunk rule: after the pc386 kernel remaps the 8259 PIC to
protected-mode vectors `0x20..0x2F`, real-mode BIOS calls must not see live IRQs.
In real mode, IRQ1 is expected at BIOS vector `0x09`; after the remap it arrives
as `int 21h`, which DOSBox-X reports as `Illegal Unhandled Interrupt Called 21`
because its DOS kernel has already shut down for guest boot. The protected-mode
BIOS thunk therefore masks both PICs before entering real mode for `int 10h` or
`int 13h`, then restores the previous masks after returning to protected mode.
Do not add new BIOS thunk users without preserving that mask/restore behavior.

DOSBox-X serial-output capture is via `serial1=file:out.log` in the config — read after exit. Less ergonomic than QEMU's `-serial stdio`; suitable for manual sanity checks, not for CI.

## 86Box (period-authentic)

Install:

```sh
brew install --cask 86box     # macOS
```

86Box is a cycle-accurate emulator with real BIOS ROMs. Use this before flashing real hardware. Configuration is GUI-driven; set up a 386DX/486-class machine with VGA/SVGA, 8-16 MB RAM, a 1.44 MB floppy drive using `pc386-floppy.img`, and an IDE hard disk using `c.img`.

Not part of the automated test loop. Exists for the "would this actually work on a beige-box machine" gut check.

## Build → boot cookbook

| Goal | Command |
|------|---------|
| Build kernel ELF | `./ports/pc386/build.sh` |
| Build/refresh disk images | `./ports/pc386/build_disks.sh` |
| Run in QEMU with display | `./ports/pc386/run.sh` |
| Run in QEMU headless | `./ports/pc386/run_headless.sh` |
| Run focused REPL tests | `python3 ports/pc386/tests/repl_expect.py files graphics_vbe` |
| Run screenshot mode stress | `python3 ports/pc386/tests/screen_probe.py --mode-stress --out /tmp/pc386-mode-stress.ppm` |
| Boot floppy in DOSBox-X | `./ports/pc386/run_dosbox.sh` |
| Boot Limine-installed C: | `./ports/pc386/run_limine.sh` |
| Debug with GDB | `./ports/pc386/run.sh debug` then attach GDB |

## When to use which emulator

- **Inner dev loop (every edit):** QEMU floppy boot for realistic device shape, or `./ports/pc386/run.sh kernel` for direct `-kernel` isolation.
- **Bootloader / Limine config changes:** `./ports/pc386/run_limine.sh`. Tests the C: hard-disk boot path.
- **Pre-real-HW gate:** DOSBox-X then 86Box. Catches BIOS-quirk dependencies QEMU forgives.
- **CI / golden-output tests:** QEMU headless only. DOSBox-X/86Box are not deterministic enough for golden compare.
