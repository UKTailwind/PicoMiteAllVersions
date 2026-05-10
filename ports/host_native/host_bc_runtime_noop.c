/*
 * ports/host_native/host_bc_runtime_noop.c — host-only override of
 * port_bc_runtime_free_source.
 *
 * bc_runtime.c calls a per-port hook between compile and runtime-table
 * allocation. Heap-tight device ports (pico, esp32, future ARM) BC_FREE
 * the source buffer. Host doesn't: source may come from the test harness
 * via malloc (mmbasic_test mallocs .bas contents and hands it in), and
 * BC_FREE walks MMHeap's page bitmap — running that on a malloc'd pointer
 * reads/writes garbage. The harness keeps ownership of the buffer and
 * releases it itself.
 *
 * This file is linked into:
 *   - host/build.sh test target
 *   - ports/mmbasic_stdio/Makefile
 *   - ports/mmbasic_ansi/Makefile
 *
 * It is *not* linked into device ports; they provide their own strong
 * runtime hook.
 */
void port_bc_runtime_free_source(const char **source) { (void)source; }
