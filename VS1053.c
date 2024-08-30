/**
 * This is a driver library for VS1053 MP3 Codec Breakout
 * (Ogg Vorbis / MP3 / AAC / WMA / FLAC / MIDI Audio Codec Chip).
 * Adapted for Espressif ESP8266 and ESP32 boards.
 *
 * version 1.0.1
 *
 * Licensed under GNU GPLv3 <http://gplv3.fsf.org/>
 * Copyright © 2018
 *
 * @authors baldram, edzelf, MagicCube, maniacbug
 *
 * Development log:
 *  - 2011: initial VS1053 Arduino library
 *          originally written by J. Coliz (github: @maniacbug),
 *  - 2016: refactored and integrated into Esp-radio sketch
 *          by Ed Smallenburg (github: @edzelf)
 *  - 2017: refactored to use as PlatformIO library
 *          by Marcin Szalomski (github: @baldram | twitter: @baldram)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License or later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "VS1053.h"
#include "vs1053b-patches.h"
#define LOG(...)
extern void __not_in_flash_func(spi_write_fast)(spi_inst_t *spi, const uint8_t *src, size_t len);
extern void __not_in_flash_func(spi_finish)(spi_inst_t *spi);
//#define LOG printf
#define _BV( bit ) ( 1<<(bit) )
uint8_t cs_pin;                         // Pin where CS line is connected
uint8_t dcs_pin;                        // Pin where DCS line is connected
uint8_t dreq_pin;                       // Pin where DREQ line is connected
uint8_t reset_pin;                       // Pin where DREQ line is connected
uint8_t curvol;                         // Current volume setting 0..100%
int8_t  curbalance = 0;                 // Current balance setting -100..100
uint8_t endFillByte;                    // Byte to send when stopping song
const uint8_t vs1053_chunk_size = 32;
// SCI Register
const uint8_t SCI_MODE = 0x0;
const uint8_t SCI_STATUS = 0x1;
const uint8_t SCI_BASS = 0x2;
const uint8_t SCI_CLOCKF = 0x3;
const uint8_t SCI_DECODE_TIME = 0x4;        // current decoded time in full seconds
const uint8_t SCI_AUDATA = 0x5;
const uint8_t SCI_WRAM = 0x6;
const uint8_t SCI_WRAMADDR = 0x7;
const uint8_t SCI_AIADDR = 0xA;
const uint8_t SCI_VOL = 0xB;
const uint8_t SCI_AICTRL0 = 0xC;
const uint8_t SCI_AICTRL1 = 0xD;
const uint8_t SCI_num_registers = 0xF;
// SCI_MODE bits
const uint8_t SM_SDINEW = 11;           // Bitnumber in SCI_MODE always on
const uint8_t SM_RESET = 2;             // Bitnumber in SCI_MODE soft reset
const uint8_t SM_CANCEL = 3;            // Bitnumber in SCI_MODE cancel song
const uint8_t SM_TESTS = 5;             // Bitnumber in SCI_MODE for tests
const uint8_t SM_LINE1 = 14;            // Bitnumber in SCI_MODE for Line input
const uint8_t SM_STREAM = 6;            // Bitnumber in SCI_MODE for Streaming Mode
const uint8_t SM_LAYER12 = 1;
const uint16_t ADDR_REG_GPIO_DDR_RW = 0xc017;
const uint16_t ADDR_REG_GPIO_VAL_R = 0xc018;
const uint16_t ADDR_REG_GPIO_ODATA_RW = 0xc019;
const uint16_t ADDR_REG_I2S_CONFIG_RW = 0xc040;
#define xmit_multi(a,b) spi_write_blocking((AUDIO_SPI==1 ? spi0 : spi1),a,b);
static BYTE __not_in_flash_func(xchg)(BYTE data_out){
	BYTE data_in=0;
	spi_write_read_blocking((AUDIO_SPI==1 ? spi0 : spi1),&data_out,&data_in,1);
	return data_in;
}

uint8_t stdmax(int a, int b){
    if(a>b)return a;
    return b;
}
int stdmap(int v){
    v=(v*80)/100;
    if(v==0)return 0xFE;
    else return 100-v;
}
void await_data_request() {
    while (!(gpio_get(dreq_pin))) {
    }
}

void control_mode_on() {
        gpio_put(dcs_pin,GPIO_PIN_SET);
        gpio_put(cs_pin,GPIO_PIN_RESET);
}

void control_mode_off()  {
        gpio_put(cs_pin,GPIO_PIN_SET);
}

void __not_in_flash_func(data_mode_on()) {
        gpio_put(cs_pin,GPIO_PIN_SET);
        gpio_put(dcs_pin,GPIO_PIN_RESET);
}

void __not_in_flash_func(data_mode_off()) {
        gpio_put(dcs_pin,GPIO_PIN_SET);
}

bool data_request() {
    return (gpio_get(dreq_pin) == 1);
}

uint16_t read_register(uint8_t _reg){
    uint16_t result;

    control_mode_on();
    xchg(3);
    xchg(_reg);
    // Note: transfer16 does not seem to work
    result = (xchg(0xFF) << 8) | // Read 16 bits data
             (xchg(0xFF));
    await_data_request(); // Wait for DREQ to be HIGH again
    control_mode_off();
    return result;
}

void writeRegister(uint8_t _reg, uint16_t _value){
    control_mode_on();
    xchg(2);        // Write operation
    xchg(_reg);     // Register to write (0..0xF)
    xchg(_value>>8);
    xchg(_value & 0xFF);
//    SPI.write16(_value); // Send 16 bits data
    await_data_request();
    control_mode_off();
}

void __not_in_flash_func(sdi_send_buffer)(uint8_t *data, size_t len) {
    size_t chunk_length; // Length of chunk 32 byte or shorter

    gpio_put(cs_pin,GPIO_PIN_SET);
    gpio_put(dcs_pin,GPIO_PIN_RESET);
    while (len) // More to do?
    {
        while (!(gpio_get(dreq_pin))) {
        }
        chunk_length = len;
        if (len > vs1053_chunk_size) {
            chunk_length = vs1053_chunk_size;
        }
        len -= chunk_length;
        if(PinDef[Option.AUDIO_CLK_PIN].mode & SPI0SCK)spi_write_fast(spi0, data, chunk_length);
        else spi_write_fast(spi1, data, chunk_length);
//        xmit_multi(data, chunk_length);
        data += chunk_length;
    }
	if(PinDef[Option.AUDIO_CLK_PIN].mode & SPI0SCK)spi_finish(spi0);
	else spi_finish(spi1);
    gpio_put(dcs_pin,GPIO_PIN_SET);
}

void sdi_send_fillers(size_t len) {
    size_t chunk_length; // Length of chunk 32 byte or shorter

    data_mode_on();
    while (len) // More to do?
    {
        await_data_request(); // Wait for space available
        chunk_length = len;
        if (len > vs1053_chunk_size) {
            chunk_length = vs1053_chunk_size;
        }
        len -= chunk_length;
        while (chunk_length--) {
            xchg(endFillByte);
        }
    }
    data_mode_off();
}

void wram_write(uint16_t address, uint16_t data) {
    writeRegister(SCI_WRAMADDR, address);
    writeRegister(SCI_WRAM, data);
}

uint16_t wram_read(uint16_t address) {
    writeRegister(SCI_WRAMADDR, address); // Start reading from WRAM
    return read_register(SCI_WRAM);        // Read back result
}
uint16_t VS1053free(void){
    uint16_t wrp, rdp; // VS1053b read and write pointers
    writeRegister(SCI_WRAMADDR, 0x5A7D); // Start reading from WRAM
    wrp = read_register(SCI_WRAM);
    rdp = read_register(SCI_WRAM);  
    return (wrp-rdp) & 1023;    
}
bool testComm(const char *header) {
    // Test the communication with the VS1053 module.  The result wille be returned.
    // If DREQ is low, there is problably no VS1053 connected.  Pull the line HIGH
    // in order to prevent an endless loop waiting for this signal.  The rest of the
    // software will still work, but readbacks from VS1053 will fail.
    int i; // Loop control
    uint16_t r1, r2, cnt = 0;
    uint16_t delta = 300; // 3 for fast SPI
    uSec(20000);
    if (!gpio_get(dreq_pin)) {
        error("VS1053 not properly installed!");
        // Allow testing without the VS1053 module
//        pinMode(dreq_pin, INPUT_PULLUP); // DREQ is now input with pull-up
        return false;                    // Return bad result
    }
    // Further TESTING.  Check if SCI bus can write and read without errors.
    // We will use the volume setting for this.
    // Will give warnings on serial output if DEBUG is active.
    // A maximum of 20 errors will be reported.
    if (strstr(header, "Fast")) {
        delta = 30; // Fast SPI, more loops
    }

    LOG("%s", header);  // Show a header

    for (i = 0; (i < 0xFFFF) && (cnt < 20); i += delta) {
        writeRegister(SCI_VOL, i);         // Write data to SCI_VOL
        r1 = read_register(SCI_VOL);        // Read back for the first time
        r2 = read_register(SCI_VOL);        // Read back a second time
        if (r1 != r2 || i != r1 || i != r2) // Check for 2 equal reads
        {
            error("VS1053 read failure");
            cnt++;
            uSec(10000);
        }
    }
    return (cnt == 0); // Return the result
}
void VS1053reset(uint8_t _reset_pin){
    reset_pin=_reset_pin;
    PinSetBit(PINMAP[reset_pin],LATCLR);
    uSec(20000);
    PinSetBit(PINMAP[reset_pin],LATSET);
    PinSetBit(PINMAP[dreq_pin],CNPUSET);
    uSec(100000);
}
void VS1053(uint8_t _cs_pin, uint8_t _dcs_pin, uint8_t _dreq_pin, uint8_t _reset_pin){
    dreq_pin=_dreq_pin;
    cs_pin=_cs_pin;
    dcs_pin=_dcs_pin;
    reset_pin=_reset_pin;
    PinSetBit(PINMAP[reset_pin],LATCLR);
    uSec(20000);
    PinSetBit(PINMAP[reset_pin],LATSET);
    PinSetBit(PINMAP[dreq_pin],CNPUSET);
    uSec(100000);
        LOG("\r\n");
    LOG("Reset VS1053...\r\n");
    LOG("End reset VS1053...\r\n");
//   gpio_put(cs_pin,GPIO_PIN_SET); // Back to normal again
//    gpio_put(dcs_pin,GPIO_PIN_SET);
//    uSec(500000);
    // Init SPI in slow mode ( 0.2 MHz )
//	SET_SPI_CLK(display_details[device].speed, display_details[device].CPOL, display_details[device].CPHASE);
    spi_init((AUDIO_SPI==1 ? spi0 : spi1), 8000);
	spi_set_format((AUDIO_SPI==1 ? spi0 : spi1), 8, 0,0, SPI_MSB_FIRST);
    // printDetails("Right after reset/startup");
    uSec(20000);
    // printDetails("20 msec after reset");
    if (testComm("Slow SPI,Testing VS1053 read/write registers...\r\n")) {
        //softReset();
        // Switch on the analog parts
        writeRegister(SCI_AUDATA, 44101); // 44.1kHz stereo
        // The next clocksetting allows SPI clocking at 5 MHz, 4 MHz is safe then.
        writeRegister(SCI_CLOCKF, 0xE000); // Normal clock settings multiplyer 3.0 = 12.2 MHz
        // SPI Clock to 4 MHz. Now you can set high speed SPI clock.
        spi_init((AUDIO_SPI==1 ? spi0 : spi1), 5400000);
//        PInt(spi_get_baudrate(AUDIO_SPI==1 ? spi0 : spi1));PRet();
        spi_set_format((AUDIO_SPI==1 ? spi0 : spi1), 8, 0,0, SPI_MSB_FIRST);
        uSec(5000);
        writeRegister(SCI_MODE, _BV(SM_SDINEW) | _BV(SM_LINE1) | _BV(SM_LAYER12));
        testComm("Fast SPI, Testing VS1053 read/write registers again...\r\n");
        uSec(5000);
        await_data_request();
        endFillByte = wram_read(0x1E06) & 0xFF;
        LOG("endFillByte is %X\r\n", endFillByte);
        //printDetails("After last clocksetting") ;
        uSec(5000);
    }
}

void setVolume(uint8_t vol) {
    // Set volume.  Both left and right.
    // Input value is 0..100.  100 is the loudest.
    uint8_t valueL, valueR; // Values to send to SCI_VOL

    curvol = vol;                         // Save for later use
    valueL = vol;
    valueR = vol;

    if (curbalance < 0) {
        valueR = stdmax(0, vol + curbalance);
    } else if (curbalance > 0) {
        valueL = stdmax(0, vol - curbalance);
    }

    valueL = stdmap(valueL); // 0..100% to left channel
    valueR = stdmap(valueR); // 0..100% to right channel
    writeRegister(SCI_VOL, (valueL << 8) | valueR); // Volume left and right
}
void setVolumes(int valueL, int valueR) {
    valueL = stdmap(valueL); // 0..100% to left channel
    valueR = stdmap(valueR); // 0..100% to right channel
    int value=((valueL << 8) | valueR);
    writeRegister(SCI_VOL, value); // Volume left and right
}

void setBalance(int8_t balance) {
    if (balance > 100) {
        curbalance = 100;
    } else if (balance < -100) {
        curbalance = -100;
    } else {
        curbalance = balance;
    }
}

void setTone(uint8_t *rtone) { // Set bass/treble (4 nibbles)
    // Set tone characteristics.  See documentation for the 4 nibbles.
    uint16_t value = 0; // Value to send to SCI_BASS
    int i;              // Loop control

    for (i = 0; i < 4; i++) {
        value = (value << 4) | rtone[i]; // Shift next nibble in
    }
    writeRegister(SCI_BASS, value); // Volume left and right
}

uint8_t getVolume() { // Get the currenet volume setting.
    return curvol;
}

int8_t getBalance() { // Get the currenet balance setting.
    return curbalance;
}

void startSong() {
    sdi_send_fillers(10);
}

void __not_in_flash_func(playChunk)(uint8_t *data, size_t len) {
    sdi_send_buffer(data, len);
}

void stopSong() {
    uint16_t modereg; // Read from mode register
    int i;            // Loop control

    sdi_send_fillers(2052);
    uSec(10000);
    writeRegister(SCI_MODE, _BV(SM_SDINEW) | _BV(SM_CANCEL));
    for (i = 0; i < 200; i++) {
        sdi_send_fillers(32);
        modereg = read_register(SCI_MODE); // Read status
        if ((modereg & _BV(SM_CANCEL)) == 0) {
            sdi_send_fillers(2052);
            LOG("Song stopped correctly after %d msec\r\n", i * 10);
            return;
        }
        uSec(10000);
    }
    printDetails("Song stopped incorrectly!");
}
void LoadUserCode(void) {
  int i;
  for (i=0;i<CODE_SIZE;i++) {
    writeRegister(atab[i], dtab[i]);
  }
}

void softReset() {
    LOG("Performing soft-reset\r\n");
    writeRegister(SCI_MODE, _BV(SM_SDINEW) | _BV(SM_RESET | _BV(SM_LAYER12)));
    uSec(10000);
    await_data_request();
}

/**
 * VLSI datasheet: "SM_STREAM activates VS1053b’s stream mode. In this mode, data should be sent with as
 * even intervals as possible and preferable in blocks of less than 512 bytes, and VS1053b makes
 * every attempt to keep its input buffer half full by changing its playback speed up to 5%. For best
 * quality sound, the average speed error should be within 0.5%, the bitrate should not exceed
 * 160 kbit/s and VBR should not be used. For details, see Application Notes for VS10XX. This
 * mode only works with MP3 and WAV files."
*/

void streamModeOn() {
    LOG("Performing streamModeOn\r\n");
    writeRegister(SCI_MODE, _BV(SM_SDINEW) | _BV(SM_STREAM));
    uSec(10000);
    await_data_request();
}

void streamModeOff() {
    LOG("Performing streamModeOff\r\n");
    writeRegister(SCI_MODE, _BV(SM_SDINEW));
    uSec(10000);
    await_data_request();
}

void printDetails(const char *header) {
    uint16_t regbuf[16];
    uint8_t i;
    (void)regbuf;

    LOG("%s", header);
    LOG("REG   Contents\r\n");
    LOG("---   -----\r\n");
    for (i = 0; i <= SCI_num_registers; i++) {
        regbuf[i] = read_register(i);
    }
    for (i = 0; i <= SCI_num_registers; i++) {
        uSec(5000);
        LOG("%3X - %5X\r\n", i, regbuf[i]);
    }
}

/**
 * An optional switch.
 * Most VS1053 modules will start up in MIDI mode. The result is that there is no audio when playing MP3.
 * You can modify the board, but there is a more elegant way without soldering.
 * No side effects for boards which do not need this switch. It means you can call it just in case.
 *
 * Read more here: http://www.bajdi.com/lcsoft-vs1053-mp3-module/#comment-33773
 */
void switchToMp3Mode() {
    wram_write(ADDR_REG_GPIO_DDR_RW, 3); // GPIO DDR = 3
    wram_write(ADDR_REG_GPIO_ODATA_RW, 0); // GPIO ODATA = 0
    uSec(10000);
    LOG("Switched to mp3 mode\r\n");
    softReset();
}



/**
 * A lightweight method to check if VS1053 is correctly wired up (power supply and connection to SPI interface).
 *
 * @return true if the chip is wired up correctly
 */
bool isChipConnected() {
    uint16_t status = read_register(SCI_STATUS);

    return !(status == 0 || status == 0xFFFF);
}

/**
 * get the Version Number for the VLSI chip
 * VLSI datasheet: 0 for VS1001, 1 for VS1011, 2 for VS1002, 3 for VS1003, 4 for VS1053 and VS8053,
 * 5 for VS1033, 7 for VS1103, and 6 for VS1063. 
 */
uint16_t getChipVersion() {
    uint16_t status = read_register(SCI_STATUS);
       
    return ( (status & 0x00F0) >> 4);
}

/**
 * Provides current decoded time in full seconds (from SCI_DECODE_TIME register value)
 *
 * When decoding correct data, current decoded time is shown in SCI_DECODE_TIME
 * register in full seconds. The user may change the value of this register.
 * In that case the new value should be written twice to make absolutely certain
 * that the change is not overwritten by the firmware. A write to SCI_DECODE_TIME
 * also resets the byteRate calculation.
 *
 * SCI_DECODE_TIME is reset at every hardware and software reset. It is no longer
 * cleared when decoding of a file ends to allow the decode time to proceed
 * automatically with looped files and with seamless playback of multiple files.
 * With fast playback (see the playSpeed extra parameter) the decode time also
 * counts faster. Some codecs (WMA and Ogg Vorbis) can also indicate the absolute
 * play position, see the positionMsec extra parameter in section 10.11.
 *
 * @see VS1053b Datasheet (1.31) / 9.6.5 SCI_DECODE_TIME (RW)
 *
 * @return current decoded time in full seconds
 */
uint16_t getDecodedTime() {
    return read_register(SCI_DECODE_TIME);
}

/**
 * Clears decoded time (sets SCI_DECODE_TIME register to 0x00)
 *
 * The user may change the value of this register. In that case the new value
 * should be written twice to make absolutely certain that the change is not
 * overwritten by the firmware. A write to SCI_DECODE_TIME also resets the
 * byteRate calculation.
 */
void clearDecodedTime() {
    writeRegister(SCI_DECODE_TIME, 0x00);
    writeRegister(SCI_DECODE_TIME, 0x00);
}

/**
 * Fine tune the data rate
 */
void adjustRate(long ppm2) {
    writeRegister(SCI_WRAMADDR, 0x1e07);
    writeRegister(SCI_WRAM, ppm2);
    writeRegister(SCI_WRAM, ppm2 >> 16);
    // oldClock4KHz = 0 forces  adjustment calculation when rate checked.
    writeRegister(SCI_WRAMADDR, 0x5b1c);
    writeRegister(SCI_WRAM, 0);
    // Write to AUDATA or CLOCKF checks rate and recalculates adjustment.
    writeRegister(SCI_AUDATA, read_register(SCI_AUDATA));
}

/**
 * Load the latest generic firmware patch
 */
void loadDefaultVs1053Patches() {
   LoadUserCode();
   LOG("Loaded latest patch\r\n");
};

#define PLUGIN_SIZE 28
const unsigned short plugin[28] = { /* Compressed plugin */
  0x0007, 0x0001, 0x8050, 0x0006, 0x0014, 0x0030, 0x0715, 0xb080, /*    0 */
  0x3400, 0x0007, 0x9255, 0x3d00, 0x0024, 0x0030, 0x0295, 0x6890, /*    8 */
  0x3400, 0x0030, 0x0495, 0x3d00, 0x0024, 0x2908, 0x4d40, 0x0030, /*   10 */
  0x0200, 0x000a, 0x0001, 0x0050,
};

void RTLoadUserCode(void) {
  int i = 0;

  while (i<sizeof(plugin)/sizeof(plugin[0])) {
    unsigned short addr, n, val;
    addr = plugin[i++];
    n = plugin[i++];
    if (n & 0x8000U) { /* RLE run, replicate n samples */
      n &= 0x7FFF;
      val = plugin[i++];
      while (n--) {
        writeRegister(addr, val);
      }
    } else {           /* Copy run, copy n samples */
      while (n--) {
        val = plugin[i++];
        writeRegister(addr, val);
      }
    }
  }
}


void sendMIDI(uint8_t data)
{
  xchg(0);
  xchg(data);
}

//Plays a MIDI note. Doesn't check to see that cmd is greater than 127, or that data values are less than 127
void talkMIDI(uint8_t cmd, uint8_t data1, uint8_t data2) {
  //
  // Wait for chip to be ready (Unlikely to be an issue with real time MIDI)
  //
    await_data_request();
    data_mode_on();
    sendMIDI(cmd);
    //Some commands only have one data byte. All cmds less than 0xBn have 2 data bytes 
    //(sort of: http://253.ccarh.org/handout/midiprotocol/)
    if( (cmd & 0xF0) <= 0xB0 || (cmd & 0xF0) >= 0xE0) {
        sendMIDI(data1);
        sendMIDI(data2);
    } else {
        sendMIDI(data1);
    }
    data_mode_off() ;
}

//Send a MIDI note-on message.  Like pressing a piano key
//channel ranges from 0-15
void noteOn(uint8_t channel, uint8_t note, uint8_t attack_velocity) {
  talkMIDI( (0x90 | channel), note, attack_velocity);
}

//Send a MIDI note-off message.  Like releasing a piano key
void noteOff(uint8_t channel, uint8_t note, uint8_t release_velocity) {
  talkMIDI( (0x80 | channel), note, release_velocity);
}

void miditest(int test) {
    RTLoadUserCode();
    uSec(100000);
    if(test==0)return;
  
    talkMIDI(0xB0, 0x07, 120); //0xB0 is channel message, set channel volume to near max (127)

    if(test==1){
        //Demo Basic MIDI instruments, GM1
        //=================================================================
        MMPrintString("Basic Instruments\r\n");
        talkMIDI(0xB0, 0, 0x00); //Default bank GM1

        //Change to different instrument
        for(int instrument = 0 ; instrument < 127 ; instrument++) {
            CheckAbort();

            MMPrintString(" Instrument: ");
            PInt(instrument);PRet();

            talkMIDI(0xC0, instrument, 0); //Set instrument number. 0xC0 is a 1 data byte command

            //Play notes from F#-0 (30) to F#-5 (90):
            for (int note = 30 ; note < 40 ; note++) {
            MMPrintString("N:");
            PInt(note);PRet();
            
            //Note on channel 1 (0x90), some note value (note), middle velocity (0x45):
            noteOn(0, note, 127);
            uSec(200000);

            //Turn off the note with a given off/release velocity
            noteOff(0, note, 127);
            uSec(50000);
            }

            uSec(100000); //uSec between instruments
        }
    } else if(test==2){
        //Demo GM2 / Fancy sounds
        //=================================================================
        MMPrintString("Demo Fancy Sounds\r\n");
        talkMIDI(0xB0, 0, 0x78); //Bank select drums

        //For this bank 0x78, the instrument does not matter, only the note
        for(int instrument = 30 ; instrument < 31 ; instrument++) {
            CheckAbort();

            MMPrintString(" Instrument: ");
            PInt(instrument);PRet();

            talkMIDI(0xC0, instrument, 0); //Set instrument number. 0xC0 is a 1 data byte command

            //Play fancy sounds from 'High Q' to 'Open Surdo [EXC 6]'
            for (int note = 27 ; note < 87 ; note++) {
            MMPrintString("N:");
            PInt(note);PRet();
            
            //Note on channel 1 (0x90), some note value (note), middle velocity (0x45):
            noteOn(0, note, 127);
            uSec(50000);

            //Turn off the note with a given off/release velocity
            noteOff(0, note, 127);
            uSec(50000);
            }

            uSec(100000); //uSec between instruments
        }
    } else if(test==3){
        //Demo Melodic
        //=================================================================
        MMPrintString("Demo Melodic? Sounds\r\n");
        talkMIDI(0xB0, 0, 0x79); //Bank select Melodic
        //These don't sound different from the main bank to me

        //Change to different instrument
        for(int instrument = 27 ; instrument < 87 ; instrument++) {
            CheckAbort();

            MMPrintString(" Instrument: ");
            PInt(instrument);PRet();

            talkMIDI(0xC0, instrument, 0); //Set instrument number. 0xC0 is a 1 data byte command

            //Play notes from F#-0 (30) to F#-5 (90):
            for (int note = 30 ; note < 40 ; note++) {
            MMPrintString("N:");PRet();
            PInt(note);
            
            //Note on channel 1 (0x90), some note value (note), middle velocity (0x45):
            noteOn(0, note, 127);
            uSec(500000);

            //Turn off the note with a given off/release velocity
            noteOff(0, note, 127);
            uSec(50000);
            }

            uSec(100000); //uSec between instruments
        }
    }
}
  //=================================================================

