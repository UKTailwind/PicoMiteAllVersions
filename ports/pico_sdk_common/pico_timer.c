#include "pico_runtime_internal.h"

repeating_timer_t timer;

void __not_in_flash_func(routinechecks)(void){
    static int when=0;
    if(abs((time_us_64()-mSecTimer*1000))> 5000){
        cancel_repeating_timer(&timer);
        add_repeating_timer_us(-1000, timer_callback, NULL, &timer);
        mSecTimer=time_us_64()/1000;
    }
    if (CurrentlyPlaying == P_WAV || CurrentlyPlaying == P_FLAC || CurrentlyPlaying==P_MP3 || CurrentlyPlaying==P_MIDI ){
        /* SPI-LCD ports take the framebuffer mutex around the WAV
         * input poll because the merge pipeline writes the same
         * memory; no-op on non-merge-pipeline ports. */
        if (SPIatRisk) hal_display_merge_lock_fb();
        checkWAVinput();
        if (SPIatRisk) hal_display_merge_unlock_fb();
    }
    if(CurrentlyPlaying == P_MOD || CurrentlyPlaying==P_ARRAY ) checkWAVinput();
    if(++when & 7 && CurrentLinePtr) return;
    /* USB-host-keyboard ports drive tuh_task / hid_app_task here;
     * non-USB ports drain USB-CDC stdio characters into the console
     * ring buffer. */
    hal_keyboard_routinechecks_pump();
	if(Option.DISPLAY_TYPE>=NEXTGEN && !(low_x==silly_low && high_x==silly_high && low_y==silly_low && high_y==silly_high)){// Buffered LCD displays
        if(Option.Refresh){
            hal_display_nextgen_refresh_rect(low_x, low_y, high_x, high_y);
            low_x=silly_low; high_y=silly_high; low_y=silly_low; high_x=silly_high;
        }
	}
	if(GPSchannel)processgps();
    if(diskchecktimer == 0)CheckSDCard();
    hal_gui_controls_routine_check_touch();

//        if(tud_cdc_connected() && KeyCheck==0){
//            SSPrintString(alive);
//        }
    if(clocktimer==0 && Option.RTC){
        if(classicread==0 && nunchuckread==0){
            RtcGetTime(0);
        }
    }
    /* I²C keyboard polling moved into the PS/2 backend's
     * hal_keyboard_routinechecks_pump() — runs alongside the USB-CDC
     * console drain. USB-host-keyboard backend's pump only does
     * tuh_task / hid_app_task. */
    if(classic1 && ClassicTimer>=10){
        if(classicread==0){
			WiiSend(sizeof(readcontroller),(char *)readcontroller);
            if(!mmI2Cvalue)classicread=1;
        } else if(classicread==1){
			WiiReceive(6, (char *)nunbuff);
            if(!mmI2Cvalue)classicread=2;
            else classicread=0;
        } else {
            classicproc();
            classicread=0;
            classic1=2;
        }
        ClassicTimer=0;
    }
    if(nunchuck1 && NunchuckTimer>=10){
        if(nunchuckread==false){
			WiiSend(sizeof(readcontroller),(char *)readcontroller);
            if(!mmI2Cvalue)nunchuckread=1;
        } else if(nunchuckread==1){
			WiiReceive(6, (char *)nunbuff);
            if(!mmI2Cvalue)nunchuckread=2;
            else nunchuckread=0;
        } else {
            nunproc();
            nunchuckread=0;
            nunchuck1=2;
        }
        NunchuckTimer=0;
    }
/*frame
    if(frame && CurrentLinePtr)ShowCursor(framecursor);
*/
}

/*void myprintf(char *s){
   fputs(s,stdout);
     fflush(stdout);
}*/

void __not_in_flash_func(mT4IntEnable)(int status){
	if(status){
		processtick=true;
	} else{
		processtick=false;
	}
}

volatile int onoff=0;
bool MIPS16 __not_in_flash_func(timer_callback)(repeating_timer_t *rt)
{
    mSecTimer++;                                                      // used by the TIMER function
    if(processtick){
        static int IrTimeout, IrTick, NextIrTick;
        int ElapsedMicroSec, IrDevTmp, IrCmdTmp;
#ifdef rp2350
    if(ExtCurrentConfig[FAST_TIMER_PIN] == EXT_FAST_TIMER && --INT5Timer <= 0) {
        static uint64_t now,last=0;
        uint32_t hi = INT5Count;
        uint32_t lo;
        do {
            // Read the lower 32 bits
            lo = pwm_get_counter(0);
            // Now read the upper 32 bits again and
            // check that it hasn't incremented. If it has loop around
            // and read the lower 32 bits again to get an accurate value
            uint32_t next_hi = INT5Count;
            if (hi == next_hi) break;
            hi = next_hi;
        } while (true);
        now=((uint64_t) hi *50000) + lo;
        INT5Value=now-last;
        last=now;
        INT5Timer = INT5InitTimer;
    }
    hal_i2c_keypad_periodic_scan((uint64_t)mSecTimer);
#endif
        AHRSTimer++;
        InkeyTimer++;                                                     // used to delay on an escape character
        PauseTimer++;													// used by the PAUSE command
        IntPauseTimer++;												// used by the PAUSE command inside an interrupt
        ds18b20Timer++;
		GPSTimer++;
        I2CTimer++;
        hal_keyboard_timer_tick();
        if(clocktimer)clocktimer--;
        if(Timer5)Timer5--;
        if(Timer4)Timer4--;
        if(Timer3)Timer3--;
        if(Timer2)Timer2--;
        if(Timer1)Timer1--;
        if(KeyCheck)KeyCheck--;
        ClassicTimer++;
        NunchuckTimer++;
        if(diskchecktimer && (Option.SD_CS || Option.CombinedCS))diskchecktimer--;
	    if(++CursorTimer > CURSOR_OFF + CURSOR_ON) CursorTimer = 0;		// used to control cursor blink rate
        if(CFuncmSec) CallCFuncmSec();                                  // the 1mS tick for CFunctions (see CFunction.c)
        if(InterruptUsed) {
            int i;
            for(i = 0; i < NBRSETTICKS; i++) if(TickActive[i])TickTimer[i]++;			// used in the interrupt tick
         }
    if(WDTimer) {
        if(--WDTimer == 0) {
            _excep_code = WATCHDOG_TIMEOUT;
            watchdog_enable(1, 1);
            while(1);
        }
    }
        if (ScrewUpTimer) {
            if (--ScrewUpTimer == 0) {
                _excep_code = SCREWUP_TIMEOUT;
                watchdog_enable(1, 1);
                while(1);
            }
        }
        if(PulseActive) {
            int i;
            for(PulseActive = i = 0; i < NBR_PULSE_SLOTS; i++) {
                if(PulseCnt[i] > 0) {                                   // if the pulse timer is running
                    PulseCnt[i]--;                                      // and decrement our count
                    if(PulseCnt[i] == 0)                                // if this is the last count reset the pulse
                        PinSetBit(PulsePin[i], LATINV);
                    else
                        PulseActive = true;                             // there is at least one pulse still active
                }
            }
        }
        ElapsedMicroSec = readIRclock();
        if(IrState > IR_WAIT_START && ElapsedMicroSec > 15000) IrReset();
        IrCmdTmp = -1;

        // check for any Sony IR receive activity
        if(IrState == SONY_WAIT_BIT_START && ElapsedMicroSec > 2800 && (IrCount == 12 || IrCount == 15 || IrCount == 20)) {
            IrDevTmp = ((IrBits >> 7) & 0b11111);
            IrCmdTmp = (IrBits & 0b1111111) | ((IrBits >> 5) & ~0b1111111);
        }

        // check for any NEC IR receive activity
        if(IrState == NEC_WAIT_BIT_END && IrCount == 32) {
            // check if it is a NON extended address and adjust if it is
            if((IrBits >> 24) == ~((IrBits >> 16) & 0xff)) IrBits = (IrBits & 0x0000ffff) | ((IrBits >> 8) & 0x00ff0000);
            IrDevTmp = ((IrBits >> 16) & 0xffff);
            IrCmdTmp = ((IrBits >> 8) & 0xff);
        }
    // GUI controls timer tick — touch pen-state machine + ClickTimer
    // countdown. No-op on stub ports.
    hal_gui_controls_timer_tick();
    // now process the IR message, this includes handling auto repeat while the key is held down
    // IrTick counts how many mS since the key was first pressed
    // NextIrTick is used to time the auto repeat
    // IrTimeout is used to detect when the key is released
    // IrGotMsg is a signal to the interrupt handler that an interrupt is required
    if(IrCmdTmp != -1) {
        if(IrTick > IrTimeout) {
            // this is a new keypress
            IrTick = 0;
            NextIrTick = 650;
        }
        if(IrTick == 0 || IrTick > NextIrTick) {
            if(IrVarType & 0b01)
                *(MMFLOAT *)IrDev = IrDevTmp;
            else
                *(long long int *)IrDev = IrDevTmp;
            if(IrVarType & 0b10)
                *(MMFLOAT *)IrCmd = IrCmdTmp;
            else
                *(long long int *)IrCmd = IrCmdTmp;
            IrGotMsg = true;
            NextIrTick += 250;
        }
        IrTimeout = IrTick + 150;
        IrReset();
    }
    IrTick++;
	if(ExtCurrentConfig[Option.INT1pin] == EXT_PER_IN) INT1Count++;
	if(ExtCurrentConfig[Option.INT2pin] == EXT_PER_IN) INT2Count++;
	if(ExtCurrentConfig[Option.INT3pin] == EXT_PER_IN) INT3Count++;
	if(ExtCurrentConfig[Option.INT4pin] == EXT_PER_IN) INT4Count++;
    if(ExtCurrentConfig[Option.INT1pin] == EXT_FREQ_IN && --INT1Timer <= 0) { INT1Value = INT1Count; INT1Count = 0; INT1Timer = INT1InitTimer; }
    if(ExtCurrentConfig[Option.INT2pin] == EXT_FREQ_IN && --INT2Timer <= 0) { INT2Value = INT2Count; INT2Count = 0; INT2Timer = INT2InitTimer; }
    if(ExtCurrentConfig[Option.INT3pin] == EXT_FREQ_IN && --INT3Timer <= 0) { INT3Value = INT3Count; INT3Count = 0; INT3Timer = INT3InitTimer; }
    if(ExtCurrentConfig[Option.INT4pin] == EXT_FREQ_IN && --INT4Timer <= 0) { INT4Value = INT4Count; INT4Count = 0; INT4Timer = INT4InitTimer; }

    ////////////////////////////////// this code runs once a second /////////////////////////////////
    if(++SecondsTimer >= 1000) {
        SecondsTimer -= 1000;
        hal_heartbeat_tick();      /* no-op on ports without an onboard LED */
            // keep track of the time and date
/*        if(++second >= 60) {
            second = 0 ;
            if(++minute >= 60) {
                minute = 0;
                if(++hour >= 24) {
                    hour = 0;
                    if(++day > DaysInMonth[month] + ((month == 2 && (year % 4) == 0)?1:0)) {
                        day = 1;
                        if(++month > 12) {
                            month = 1;
                            year++;
                        }
                    }
                }
            }
        }*/
        }
    }
  return 1;
}
void __not_in_flash_func(uSec)(int us) {
    /* On WiFi ports a long busy-wait would starve the lwIP poll —
     * pump ProcessWeb() every 500us. On non-WiFi the stub is a
     * no-op so the inner branch reduces to the busy-wait below. */
    if (us < 500) {
        busy_wait_us(us);
    } else {
        uint64_t end = time_us_64() + us;
        while (time_us_64() < end) {
            if (time_us_64() % 500 == 0) ProcessWeb(1);
        }
    }
}
