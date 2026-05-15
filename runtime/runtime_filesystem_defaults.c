/*
 * runtime/runtime_filesystem_defaults.c — shared default bodies for
 * the BASIC-core filesystem-routing hooks `port_drivecheck_remap` and
 * `port_filesystem_prefix`.
 *
 * The defaults model the canonical A:=LittleFS / B:=FatFs split that
 * Pico, host, and ESP32 all share:
 *   - port_drivecheck_remap is the identity (the drive type fetched
 *     from drivecheck is used unchanged).
 *   - port_filesystem_prefix returns "A:" for filesystem 0 and "B:"
 *     for any non-zero filesystem id.
 *
 * Previously duplicated byte-identically in:
 *   - ports/pico_sdk_common/cmd_files_hooks.c
 *   - ports/host_native/host_runtime.c
 *   - ports/esp32_s3_metro/main/esp32_cmd_files_hooks.c
 *
 * pc386 deliberately does NOT link this TU — it has FatFs on every
 * volume (no LFS) and uses DOS-style A:/B:/C: drive-letter routing,
 * so both hooks need real per-port bodies in pc386_runtime.c.
 *
 * docs/port-duplication-audit.md Finding 7.
 */

int port_drivecheck_remap(int t) { return t; }

const char *port_filesystem_prefix(int filesystem)
{
    return filesystem ? "B:" : "A:";
}
