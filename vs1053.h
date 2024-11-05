/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
/* 
 * This is a driver library for VS1053 MP3 Codec Breakout
 * (Ogg Vorbis / MP3 / AAC / WMA / FLAC / MIDI Audio Codec Chip).
 * Adapted for Espressif ESP8266 and ESP32 boards.
 *
 * version 1.0.1
 *
 * Licensed under GNU GPLv3 <http://gplv3.fsf.org/>
 * Copyright © 2017
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

#ifndef VS1053_H
#define VS1053_H
#include "stdint.h"

enum VS1053_I2S_RATE {
    VS1053_I2S_RATE_192_KHZ,
    VS1053_I2S_RATE_96_KHZ,
    VS1053_I2S_RATE_48_KHZ
};

                                            // (-100 = right channel silent, 100 = left channel silent)


    uint16_t read_register(uint8_t _reg) ;

    void sdi_send_buffer(uint8_t *data, size_t len);

    void sdi_send_fillers(size_t length);

    void wram_write(uint16_t address, uint16_t data);

    uint16_t wram_read(uint16_t address);


    // Constructor.  Only sets pin values.  Doesn't touch the chip.  Be sure to call begin()!
    void VS1053(uint8_t _cs_pin, uint8_t _dcs_pin, uint8_t _dreq_pin, uint8_t _reset_pin);

    // Begin operation.  Sets pins correctly, and prepares SPI bus.
    void begin();

    // Prepare to start playing. Call this each time a new song starts
    void startSong();

    // Play a chunk of data.  Copies the data to the chip.  Blocks until complete
    void playChunk(uint8_t *data, size_t len);

    // Finish playing a song. Call this after the last playChunk call
    void stopSong();

    // Set the player volume.Level from 0-100, higher is louder
    void setVolume(uint8_t vol);

    // Adjusting the left and right volume balance, higher to enhance the right side, lower to enhance the left side.
    void setBalance(int8_t balance);

    // Set the player baas/treble, 4 nibbles for treble gain/freq and bass gain/freq
    void setTone(uint8_t *rtone);

    // Get the currenet volume setting, higher is louder
    uint8_t getVolume();

    // Get the currenet balance setting (-100..100)
    int8_t getBalance();

    // Print configuration details to serial output.
    void printDetails(const char *header);

    // Do a soft reset
    void softReset();

    // Test communication with module
    bool testComm(const char *header);


    // Fine tune the data rate
    void adjustRate(long ppm2);

    // Streaming Mode On
    void streamModeOn();
    
    // Default: Streaming Mode Off
    void streamModeOff();      

    // An optional switch preventing the module starting up in MIDI mode
    void switchToMp3Mode();

    // disable I2S output; this is the default state
    void disableI2sOut();

//    // enable I2S output (GPIO4=LRCLK/WSEL; GPIO5=MCLK; GPIO6=SCLK/BCLK; GPIO7=SDATA/DOUT)
//    void enableI2sOut(VS1053_I2S_RATE i2sRate = VS1053_I2S_RATE_48_KHZ);

    // Checks whether the VS1053 chip is connected and is able to exchange data to the ESP
    bool isChipConnected();

    // gets Version of the VLSI chip being used
    uint16_t getChipVersion();    

    // Provides SCI_DECODE_TIME register value
    uint16_t getDecodedTime();

    // Clears SCI_DECODE_TIME register (sets 0x00)
    void clearDecodedTime();

    // Writes to VS10xx's SCI (serial command interface) SPI bus.
    // A low level method which lets users access the internals of the VS1053.
    void writeRegister(uint8_t _reg, uint16_t _value);

    // Load a patch or plugin to fix bugs and/or extend functionality.
    // For more info about patches see http://www.vlsi.fi/en/support/software/vs10xxpatches.html
    void loadUserCode(void);

    // Loads the latest generic firmware patch.
    void loadDefaultVs1053Patches();
    void noteOn(uint8_t channel, uint8_t note, uint8_t attack_velocity);
    void noteOff(uint8_t channel, uint8_t note, uint8_t release_velocity);
    void miditest(int test);
    void talkMIDI(uint8_t cmd, uint8_t data1, uint8_t data2);
    uint16_t VS1053free(void);
    extern volatile uint16_t VSbuffer;
    void setVolumes(int valueL, int valueR);
    void VS1053reset(uint8_t _reset_pin);

#endif
/*  @endcond */