/*
 * drivers/gui_touch/gui_touch_stub.c — stubs for the GUI touch-panel
 * sub-commands on targets without a touch panel (VGA / HDMI / host).
 *
 * Both hooks return 0 so cmd_guiMX170 falls through to its next
 * branch or the "Unknown command" error.
 */

int hal_port_gui_touch_cmd(unsigned char * cmdline) {
    (void)cmdline;
    return 0;
}
int hal_port_gui_touch_test(unsigned char * p) {
    (void)p;
    return 0;
}
