/*
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

GPS.h

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the distribution.
3. The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
   on the console at startup (additional copyright messages may be added).
4. All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
   by the <copyright holder>.
5. Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
   without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

************************************************************************************************************************/

#ifndef MINMEA_H
#define MINMEA_H

#include "configuration.h"

#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)

/* ============================================================================
 * Standard includes
 * ============================================================================ */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>

/* ============================================================================
 * Time and date constants
 * ============================================================================ */
#define YEAR0 1900                 // The first year
#define EPOCH_YR 1970              // EPOCH = Jan 1 1970 00:00:00
#define SECS_DAY (24L * 60L * 60L) // Seconds in a day
#define TIME_MAX ULONG_MAX
#define ABB_LEN 3 // Abbreviation length

/* Time macros */
#define LEAPYEAR(year) (!((year) % 4) && (((year) % 100) || !((year) % 400)))
#define YEARSIZE(year) (LEAPYEAR(year) ? 366 : 365)
#define FIRSTSUNDAY(timp) (((timp)->tm_yday - (timp)->tm_wday + 420) % 7)
#define FIRSTDAYOF(timp) (((timp)->tm_wday - (timp)->tm_yday + 420) % 7)

/* ============================================================================
 * MINMEA configuration
 * ============================================================================ */
#define MINMEA_MAX_LENGTH 80

/* ============================================================================
 * NMEA sentence type enumeration
 * ============================================================================ */
enum minmea_sentence_id
{
    MINMEA_INVALID = -1,
    MINMEA_UNKNOWN = 0,
    MINMEA_SENTENCE_RMC, // Recommended Minimum Navigation Information
    MINMEA_SENTENCE_GGA, // Global Positioning System Fix Data
    MINMEA_SENTENCE_GSA, // GPS DOP and Active Satellites
    MINMEA_SENTENCE_GLL, // Geographic Position - Latitude/Longitude
    MINMEA_SENTENCE_GST, // GPS Pseudorange Noise Statistics
    MINMEA_SENTENCE_GSV, // GPS Satellites in View
    MINMEA_SENTENCE_VTG, // Track Made Good and Ground Speed
};

/* ============================================================================
 * GLL status enumeration
 * ============================================================================ */
enum minmea_gll_status
{
    MINMEA_GLL_STATUS_DATA_VALID = 'A',
    MINMEA_GLL_STATUS_DATA_NOT_VALID = 'V',
};

/* ============================================================================
 * FAA mode enumeration (added in NMEA 2.3)
 * ============================================================================ */
enum minmea_faa_mode
{
    MINMEA_FAA_MODE_AUTONOMOUS = 'A',
    MINMEA_FAA_MODE_DIFFERENTIAL = 'D',
    MINMEA_FAA_MODE_ESTIMATED = 'E',
    MINMEA_FAA_MODE_MANUAL = 'M',
    MINMEA_FAA_MODE_SIMULATED = 'S',
    MINMEA_FAA_MODE_NOT_VALID = 'N',
    MINMEA_FAA_MODE_PRECISE = 'P',
};

/* ============================================================================
 * GSA mode enumeration
 * ============================================================================ */
enum minmea_gsa_mode
{
    MINMEA_GPGSA_MODE_AUTO = 'A',
    MINMEA_GPGSA_MODE_FORCED = 'M',
};

/* ============================================================================
 * GSA fix type enumeration
 * ============================================================================ */
enum minmea_gsa_fix_type
{
    MINMEA_GPGSA_FIX_NONE = 1,
    MINMEA_GPGSA_FIX_2D = 2,
    MINMEA_GPGSA_FIX_3D = 3,
};

/* ============================================================================
 * Type definitions - Basic NMEA structures
 * ============================================================================ */

/* Fixed-point value with scale */
struct minmea_float
{
    int value;
    int scale;
};

/* Date structure */
struct minmea_date
{
    int day;
    int month;
    int year;
};

/* Time structure */
struct minmea_time
{
    int hours;
    int minutes;
    int seconds;
    int microseconds;
};

/* Satellite information */
struct minmea_sat_info
{
    int nr;        // Satellite number
    int elevation; // Elevation in degrees
    int azimuth;   // Azimuth in degrees
    int snr;       // Signal-to-noise ratio
};

/* ============================================================================
 * Type definitions - NMEA sentence structures
 * ============================================================================ */

/* RMC - Recommended Minimum Navigation Information */
struct minmea_sentence_rmc
{
    struct minmea_time time;
    bool valid;
    struct minmea_float latitude;
    struct minmea_float longitude;
    struct minmea_float speed;
    struct minmea_float course;
    struct minmea_date date;
    struct minmea_float variation;
};

/* GGA - Global Positioning System Fix Data */
struct minmea_sentence_gga
{
    struct minmea_time time;
    struct minmea_float latitude;
    struct minmea_float longitude;
    int fix_quality;
    int satellites_tracked;
    struct minmea_float hdop;
    struct minmea_float altitude;
    char altitude_units;
    struct minmea_float height;
    char height_units;
    int dgps_age;
};

/* GLL - Geographic Position - Latitude/Longitude */
struct minmea_sentence_gll
{
    struct minmea_float latitude;
    struct minmea_float longitude;
    struct minmea_time time;
    char status;
    char mode;
};

/* GST - GPS Pseudorange Noise Statistics */
struct minmea_sentence_gst
{
    struct minmea_time time;
    struct minmea_float rms_deviation;
    struct minmea_float semi_major_deviation;
    struct minmea_float semi_minor_deviation;
    struct minmea_float semi_major_orientation;
    struct minmea_float latitude_error_deviation;
    struct minmea_float longitude_error_deviation;
    struct minmea_float altitude_error_deviation;
};

/* GSA - GPS DOP and Active Satellites */
struct minmea_sentence_gsa
{
    char mode;
    int fix_type;
    int sats[12];
    struct minmea_float pdop;
    struct minmea_float hdop;
    struct minmea_float vdop;
};

/* GSV - GPS Satellites in View */
struct minmea_sentence_gsv
{
    int total_msgs;
    int msg_nr;
    int total_sats;
    struct minmea_sat_info sats[4];
};

/* VTG - Track Made Good and Ground Speed */
struct minmea_sentence_vtg
{
    struct minmea_float true_track_degrees;
    struct minmea_float magnetic_track_degrees;
    struct minmea_float speed_knots;
    struct minmea_float speed_kph;
    enum minmea_faa_mode faa_mode;
};

/* ============================================================================
 * External variables - GPS buffers and state
 * ============================================================================ */
extern volatile char gpsbuf1[128];
extern volatile char gpsbuf2[128];
extern volatile char *volatile gpsbuf;
extern volatile char gpscount;
extern volatile int gpscurrent;
extern volatile char *gpsready;
extern volatile int gpsmonitor;
extern int GPSchannel;

/* ============================================================================
 * External variables - GPS data
 * ============================================================================ */
extern MMFLOAT GPSlatitude;
extern MMFLOAT GPSlongitude;
extern MMFLOAT GPSspeed;
extern MMFLOAT GPStrack;
extern MMFLOAT GPSdop;
extern MMFLOAT GPSaltitude;
extern int GPSvalid;
extern int GPSfix;
extern int GPSadjust;
extern int GPSsatellites;
extern char GPStime[9];
extern char GPSdate[11];

/* ============================================================================
 * External variables - Time conversion support
 * ============================================================================ */
extern const int _ytab[2][12];

/* ============================================================================
 * Function declarations - Checksum and validation
 * ============================================================================ */

/* Calculate raw sentence checksum (does not check sentence integrity) */
uint8_t minmea_checksum(const char *sentence);

/* Check sentence validity and checksum (returns true for valid sentences) */
bool minmea_check(const char *sentence, bool strict);

/* ============================================================================
 * Function declarations - Sentence identification
 * ============================================================================ */

/* Determine talker identifier */
bool minmea_talker_id(char talker[3], const char *sentence);

/* Determine sentence identifier */
enum minmea_sentence_id minmea_sentence_id(const char *sentence, bool strict);

/* ============================================================================
 * Function declarations - Sentence parsing
 * ============================================================================ */

/*
 * Scanf-like processor for NMEA sentences. Supports the following formats:
 * c - single character (char *)
 * d - direction, returned as 1/-1, default 0 (int *)
 * f - fractional, returned as value + scale (int *, int *)
 * i - decimal, default zero (int *)
 * s - string (char *)
 * t - talker identifier and type (char *)
 * T - date/time stamp (int *, int *, int *)
 * Returns true on success.
 */
bool minmea_scan(const char *sentence, const char *format, ...);

/* Parse specific sentence types (return true on success) */
bool minmea_parse_rmc(struct minmea_sentence_rmc *frame, const char *sentence);
bool minmea_parse_gga(struct minmea_sentence_gga *frame, const char *sentence);
bool minmea_parse_gsa(struct minmea_sentence_gsa *frame, const char *sentence);
bool minmea_parse_gll(struct minmea_sentence_gll *frame, const char *sentence);
bool minmea_parse_gst(struct minmea_sentence_gst *frame, const char *sentence);
bool minmea_parse_gsv(struct minmea_sentence_gsv *frame, const char *sentence);
bool minmea_parse_vtg(struct minmea_sentence_vtg *frame, const char *sentence);

/* ============================================================================
 * Function declarations - Time conversion
 * ============================================================================ */

/* Convert GPS UTC date/time representation to a UNIX timestamp */
int minmea_gettime(struct timespec *ts, const struct minmea_date *date, const struct minmea_time *time_);

/* Time conversion utilities */
time_t timegm(const struct tm *tm);
struct tm *gmtime(const time_t *timer);

/* ============================================================================
 * Function declarations - GPS processing
 * ============================================================================ */
void processgps(void);

#endif /* !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE) */

#endif /* MINMEA_H */

/* vim: set ts=4 sw=4 et: */
/*  @endcond */