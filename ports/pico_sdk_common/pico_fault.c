#include "pico_runtime_internal.h"

static void __not_in_flash_func(picomite_runtime_checkabort_service)(void) {
    ProcessWeb(1);          /* stub no-op on non-WiFi */
    routinechecks();
}

static void __not_in_flash_func(picomite_runtime_before_abort)(void) {
    WDTimer = 0;                                                // turn off the watchdog timer
    calibrate=0;
    Display_ShowCursor(false);
    hal_display_merge_abort();
}

static mmbasic_runtime_abort_adapter picomite_abort_adapter = {
    .service = picomite_runtime_checkabort_service,
    .abort_flag = &MMAbort,
    .flags = MMBASIC_RUNTIME_ABORT_FLAG_CHECK_ABORT |
             MMBASIC_RUNTIME_ABORT_FLAG_DO_END_LONGJMP,
    .before_abort = picomite_runtime_before_abort,
};

void __not_in_flash_func(CheckAbort)(void) {
    mmbasic_runtime_checkabort(&picomite_abort_adapter);
}
extern void bc_crash_save_fault(void);
extern void bc_crash_dump_if_any(void);
void sigbus(void){
    bc_crash_save_fault();
    MMPrintString("Error: Invalid address - resetting\r\n");
	uSec(5000000);
	fileio_flash_write_begin();
//	flash_range_erase(PROGSTART, MAX_PROG_SIZE);
    LoadOptions();
    if(Option.NoReset==0){
        Option.Autorun=0;
        SaveOptions();
    }
	fileio_flash_write_end();
    memset(inpbuf,0,STRINGSIZE);
    SoftReset();
}
