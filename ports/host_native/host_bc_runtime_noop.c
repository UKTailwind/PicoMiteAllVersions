/*
 * ports/host_native/host_bc_runtime_noop.c — host-only override of
 * port_bc_runtime_free_source.
 *
 * bc_runtime.c provides a weak default that BC_FREE's the source buffer
 * between compile and runtime-table allocation. Heap-tight device ports
 * (pico, esp32, future ARM) want that. Host doesn't: source may come
 * from the test harness via malloc (mmbasic_test mallocs .bas contents
 * and hands it in), and BC_FREE walks MMHeap's page bitmap — running
 * that on a malloc'd pointer reads/writes garbage. Host overrides the
 * weak default with this strong no-op; the harness keeps ownership of
 * the buffer and releases it itself.
 *
 * This file is linked into:
 *   - host/build.sh test target
 *   - ports/mmbasic_stdio/Makefile
 *   - ports/mmbasic_ansi/Makefile
 *
 * It is *not* linked into device ports — they fall through to the
 * weak default in bc_runtime.c.
 */
void port_bc_runtime_free_source(const char **source) { (void)source; }
