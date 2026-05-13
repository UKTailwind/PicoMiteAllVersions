# Stage 1 — Heap region

Parse the bootloader-supplied multiboot1 memory map and reserve a fixed-size buffer for MMBasic's heap. Stage 3 will hand this buffer to the interpreter; Stage 1 just gets the buffer in place and proves we know what RAM the bootloader gave us.

## Goal

After Stage 1:
- `boot.S` requests memory information from the bootloader (multiboot1 header flag bit 1).
- `kmain` walks the supplied memory map and prints each region.
- A 1 MB BSS-resident `mmbasic_heap_storage[]` is reserved (size from `HAL_PORT_HEAP_MEMORY_SIZE` in `port_config.h`).
- Boot output ends with the heap region's address + size and the kernel halts.

We do *not* implement any allocator on top of the buffer in this stage. The reservation is a stable address+size handed to MMBasic in Stage 3 — the interpreter brings its own allocator and manages the region itself, identical to how RP2040/ESP32 ports pass a flash-mmap'd or PSRAM region.

## Deliverables

| File | Role |
|------|------|
| `ports/pc386/boot.S` | header flags now `0x02` (MEMINFO) instead of `0x00` |
| `ports/pc386/multiboot1.h` | info-struct + mmap-entry layouts per spec section 3.3 |
| `ports/pc386/mmap.{c,h}` | walks `mmap_addr..mmap_addr+mmap_length`; print + summarize |
| `ports/pc386/heap_region.{c,h}` | static aligned BSS buffer, size from `port_config.h`; base+size accessors |
| `ports/pc386/kprint.{c,h}` | factored-out `kputc/kputs/kputhex32/kputhex64/kputu32/kputu64`; both consoles |
| `ports/pc386/kmain.c` | parses info, calls `mmap_print` + `mmap_summarize`, prints heap region |

## Verification

```
$ ./build.sh
$ ./run_headless.sh --timeout 2
PicoMite PC386 - Stage 1
multiboot1 magic: 0x2badb002  [ok]
serial COM1: ok
multiboot info: 0x00009500  flags=0x0000024f
Memory map:
  0x0000000000000000  654336 bytes  available
  0x000000000009fc00  1024 bytes  reserved
  0x00000000000f0000  65536 bytes  reserved
  0x0000000000100000  15597568 bytes  available
  0x0000000000fe0000  131072 bytes  reserved
  0x00000000fffc0000  262144 bytes  reserved
Total available RAM: 16251904 bytes (15871 KB)
Largest free region: 15597568 bytes at 0x0000000000100000
MMBasic heap reserved: 1048576 bytes at 0x00108000

Stage 1 complete. Halting.
```

The flags `0x24f` decode as bits 0/1/2/3/6/9 = MEMORY + BOOTDEV + CMDLINE + MODS + MMAP + BOOTLDR — what QEMU populates when it sees a multiboot1 kernel. The heap buffer sits at `0x108000`, which is the kernel's `.bss` immediately above `.data` (the kernel ELF loads at `0x100000`).

## Why a static buffer, not a multiboot-mmap-driven one

Two reasons.

**Determinism.** The MCU ports hand MMBasic a fixed-size heap from a known address. If we instead picked the largest available region from the mmap dynamically, the heap base would shift between QEMU/Limine/real-hardware and between RAM sizes, which is a footgun for any code that compares pointers across reboots (e.g. saved-program flash offsets, error-message backtraces).

**Simplicity.** A 1 MB BSS allocation costs zero file bytes (BSS is zeroed by the loader) and is trivially aligned. The PC's RAM is plentiful enough that we don't need to scrape together fragmented regions; if a future deployment is RAM-tight, the knob is `HAL_PORT_HEAP_MEMORY_SIZE` in `port_config.h`.

The `mmap_summarize()` data is recorded for *informational* purposes only at this stage — Stage 2 will use it to decide where to place disk DMA buffers, and Stage 5/6 will use it to validate the framebuffer + audio buffers fit.

## Deliberately NOT in this stage

- **Allocator on top of the heap region.** MMBasic has its own. Stage 3 wires it in.
- **Reclaiming reserved BIOS ranges.** ACPI reclaim, NVS, etc. — handled never; we have plenty of RAM.
- **Validating heap_region against the mmap.** A future tightness check could assert that `heap_region_base()..+size()` falls entirely inside an `MB1_MEM_AVAILABLE` region. Trivially true for our static BSS placement; add the assert if it ever stops being.
- **PAE / paging.** Flat segments forever (or until somebody really needs > 4 GB on a kernel that targets 386).

## Exit gate

1. `./build.sh` clean (`-Wall -Wextra`, no warnings).
2. `./run_headless.sh --timeout 2` exits 0 and prints the mmap + heap region summary.
3. `host/run_tests.sh` still green (Stage 1 changes nothing in `host/`).
4. `tools/check_hal_purity.sh` green (no core changes).
