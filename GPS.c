/***********************************************************************************************************************
PicoMite MMBasic
GPS.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

************************************************************************************************************************/
/**
 * @file GPS.c
 * @author Geoff Graham, Peter Mather
 * @brief Source for GPS MMBasic function
 */
/**
 * @cond
 * The following section will be excluded from the documentation.
 */

/* ************************************************************************** */
/* ************************************************************************** */
/* Section: Included Files                                                    */
/* ************************************************************************** */
/* ************************************************************************** */

/* This section lists the other files that are included in this file.
 */

/* TODO:  Include other files here if needed. */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

/* ************************************************************************** */
/* ************************************************************************** */
/* Section: File Scope or Global Data                                         */
/* ************************************************************************** */
/* ************************************************************************** */

/*  A brief description of a section can be given directly below the section
    banner.
 */

/* ************************************************************************** */
/** Descriptive Data Item Name

  @Summary
    Brief one-line summary of the data item.

  @Description
    Full description, explaining the purpose and usage of data item.
    <p>
    Additional description in consecutive paragraphs separated by HTML
    paragraph breaks, as necessary.
    <p>
    Type "JavaDoc" in the "How Do I?" IDE toolbar for more information on tags.

  @Remarks
    Any additional remarks
 */
// int global_data;
int GPSchannel = 0;
volatile char gpsbuf1[128];
volatile char gpsbuf2[128];
volatile char *volatile gpsbuf;
volatile char *gpsready;
volatile char gpscount;
volatile int gpscurrent;
volatile int gpsmonitor;
MMFLOAT GPSlatitude = 0;
MMFLOAT GPSlongitude = 0;
MMFLOAT GPSspeed = 0;
int GPSvalid = 0;
char GPStime[9] = "000:00:0";
char GPSdate[11] = "000-00-200";
MMFLOAT GPStrack = 0;
MMFLOAT GPSdop = 0;
int GPSsatellites = 0;
MMFLOAT GPSaltitude = 0;
MMFLOAT GPSgeoid = 0;
int GPSfix = 0;
int GPSadjust = 0;
#ifdef rp2350
int locateday = 0;
int locatemonth = 0;
int locateyear = 0;
int locatehour = 0;
int locateminute = 0;
int locatesecond = 0;
MMFLOAT locatelatitude = 0;
MMFLOAT locatelongitude = 0;
int locatevalid = 0;
MMFLOAT GPSSidereal = 0.0;
MMFLOAT GPSJulian = 0.0;

// Enumeration for celestial bodies
enum CelestialBody
{
  BODY_MOON = 0,
  BODY_MERCURY = 1,
  BODY_VENUS = 2,
  BODY_MARS = 3,
  BODY_JUPITER = 4,
  BODY_SATURN = 5,
  BODY_URANUS = 6,
  BODY_NEPTUNE = 7
};

typedef struct
{
  const char *name;
  double ra_deg;  // J2000 Right Ascension (degrees)
  double dec_deg; // J2000 Declination (degrees)
  double pm_ra;   // Proper motion in RA (arcsec/year)
  double pm_dec;  // Proper motion in Dec (arcsec/year)
} Star;

const Star starCatalog[] = {
    {"Achernar", 24.428000, -57.236750, 0.09774, -0.04008},        // HIP 7588"
    {"Acrux", 186.650000, -63.099000, -0.03500, -0.01400},         // HIP 60718"
    {"Alcyone", 56.871250, +24.106111, 0.01900, -0.04500},         // HIP 17702"
    {"Aldebaran", 68.980162, +16.509301, 0.06278, -0.18894},       // HIP 21421"
    {"Algenib", 0.139167, +15.183333, 0.00200, -0.00300},          // HIP 1067"
    {"Algieba", 154.993333, +23.750000, -0.24800, -0.05000},       // HIP 50583"
    {"Algol", 47.042083, +40.955556, 0.00200, -0.00300},           // HIP 14576"
    {"Alhajoth", 14.177500, +60.716667, 0.00200, -0.00300},        // HIP 12390"
    {"Alhena", 116.313333, +14.195000, -0.07100, -0.02000},        // HIP 31681"
    {"Almaak", 2.065000, +42.329000, 0.00400, -0.00200},           // HIP 9640"
    {"Alnair", 332.058333, -46.960000, 0.01000, -0.01200},         // HIP 109268"
    {"Alnilam", 84.053389, -1.201917, -0.00100, -0.00200},         // HIP 26311"
    {"Alnitak", 85.189000, -1.942000, -0.00300, -0.00200},         // HIP 26727"
    {"Alphard", 141.896389, -24.374167, -0.02400, 0.00600},        // HIP 46390"
    {"Alpheratz", 2.096389, +29.090556, -0.13600, -0.05000},       // HIP 677"
    {"Alpherg", 23.079167, +15.205000, 0.02500, -0.01000},         // HIP 11767"
    {"Alrescha", 30.511667, +2.763333, 0.13500, -0.05000},         // HIP 8833"
    {"Alsephina", 114.828333, -30.360000, -0.01800, -0.01000},     // HIP 31592"
    {"Alshain", 296.565000, +6.425000, 0.53600, 0.38500},          // HIP 94779"
    {"Altair", 297.695827, +8.868322, 0.53682, 0.38554},           // HIP 97649"
    {"Aludra", 97.962083, -29.247500, -0.00400, -0.00200},         // HIP 30324"
    {"Andromeda Galaxy (M31)", 10.684708, 41.269167, 0.0, 0.0},    //"
    {"Antares", 247.351915, -26.432002, -0.01016, -0.02321},       // HIP 80763"
    {"Arcturus", 213.915300, +19.182410, -1.09345, -2.00094},      // HIP 69673"
    {"Aspidiske", 122.383333, -47.336667, -0.01200, -0.00600},     // HIP 39953"
    {"Bellatrix", 81.282000, +6.350000, -0.00800, -0.01300},       // HIP 25336"
    {"Betelgeuse", 88.792939, +7.407064, 0.02495, 0.00956},        // HIP 27989"
    {"Bode's Galaxy (M81)", 148.888750, 69.064444, 0.0, 0.0},      //"
    {"Canopus", 95.987875, -52.695718, 0.01993, 0.02324},          // HIP 30438"
    {"Capella", 79.172327, +45.997991, 0.07552, -0.42713},         // HIP 24608"
    {"Caph", 2.294167, +59.149444, 0.00300, -0.00200},             // HIP 746"
    {"Castor", 113.650000, +31.888333, -0.19100, -0.04500},        // HIP 36850"
    {"Cigar Galaxy (M82)", 149.062500, 69.679722, 0.0, 0.0},       //"
    {"Deneb", 310.357979, +45.280338, 0.00146, 0.00129},           // HIP 102098"
    {"Denebola", 177.264167, +14.572056, -0.49700, -0.11400},      // HIP 57632"
    {"Dubhe", 165.460000, +61.751000, -0.13500, -0.03500},         // HIP 54061"
    {"Elnath", 81.572917, +28.607500, -0.00400, -0.00900},         // HIP 25428"
    {"Eltanin", 269.151667, +51.488889, -0.00900, -0.02200},       // HIP 87833"
    {"Enif", 333.375000, +9.875000, 0.00400, -0.00300},            // HIP 107315"
    {"Fomalhaut", 344.412750, -29.622236, 0.32995, -0.16467},      // HIP 113368"
    {"Gacrux", 187.791667, -56.363056, -0.04000, -0.01400},        // HIP 61084"
    {"Hadar", 210.955000, -60.373000, -0.03300, -0.01400},         // HIP 68702"
    {"Homam", 326.046667, +9.875000, 0.00300, -0.00200},           // HIP 106481"
    {"Kaus Australis", 283.816667, -34.374167, 0.01300, -0.02500}, // HIP 90185"
    {"Kochab", 222.676667, +74.155000, -0.01700, -0.01100},        // HIP 85670"
    {"Kornephoros", 245.997500, +21.489444, -0.00900, -0.00600},   // HIP 80816"
    {"Large Magellanic Cloud", 80.891667, -69.756111, 0.0, 0.0},   //"
    {"Lesath", 263.733333, -37.103333, -0.00200, -0.00100},        // HIP 85927"
    {"Markab", 346.190000, +15.205000, 0.02500, -0.01000},         // HIP 113963"
    {"Menkalinan", 90.983333, +44.947500, -0.00400, -0.00900},     // HIP 28360"
    {"Mimosa", 191.930000, -59.688000, -0.03695, -0.01342},        // HIP 62434"
    {"Mintaka", 83.001667, -0.299167, -0.00200, -0.00300},         // HIP 25930"
    {"Mirfak", 51.080833, +49.861111, 0.02500, -0.02700},          // HIP 15863"
    {"Nunki", 283.816667, -26.296111, 0.01300, -0.02500},          // HIP 92855"
    {"Peacock", 311.918333, -60.282222, 0.01300, -0.01000},        // HIP 100751"
    {"Polaris", 37.954560, +89.264109, 0.19893, -0.01560},         // HIP 11767"
    {"Pollux", 116.329000, +28.026200, -0.62655, -0.04595},        // HIP 37826"
    {"Procyon", 114.825493, +5.224993, -0.71459, -1.03677},        // HIP 37279"
    {"Rasalgethi", 259.056667, +14.390000, -0.00900, -0.00600},    // HIP 84345"
    {"Rasalhague", 263.733333, +12.560000, -0.13700, -0.23400},    // HIP 84893"
    {"Regulus", 152.093333, +11.967222, -0.24993, 0.05027},        // HIP 49669"
    {"Rigel", 78.634467, -8.201639, 0.00187, 0.00056},             // HIP 24436"
    {"Rigil Kent", 219.902058, -60.835153, -3.67925, 0.48184},     // HIP 71683"
    {"Ruchbah", 10.126667, +60.235000, 0.00200, -0.00300},         // HIP 4427"
    {"Sabik", 250.321667, -15.757222, -0.00800, -0.00400},         // HIP 81266"
    {"Sadalmelik", 322.493333, -0.010000, 0.00200, -0.00300},      // HIP 109074"
    {"Sadalsuud", 330.790000, -0.319000, 0.00300, -0.00200},       // HIP 110960"
    {"Sadr", 305.557083, +40.256667, -0.00200, -0.00300},          // HIP 100453"
    {"Saiph", 86.939167, -9.669722, 0.00400, -0.00200},            // HIP 26207"
    {"Scheat", 345.940000, +28.080000, 0.02500, -0.01000},         // HIP 113881"
    {"Shaula", 263.402083, -37.104167, -0.00200, -0.00100},        // HIP 85927"
    {"Shedir", 10.127500, +56.537222, 0.00200, -0.00300},          // HIP 3179"
    {"Sirius", 101.287155, -16.716116, -0.54601, -1.22307},        // HIP 32349"
    {"Small Magellanic Cloud", 13.158333, -72.800278, 0.0, 0.0},   //"
    {"Sombrero Galaxy (M104)", 189.997917, -11.622778, 0.0, 0.0},  //"
    {"Spica", 201.298247, -11.161322, -0.04235, -0.03173},         // HIP 65474"
    {"Suhail", 131.175000, -42.654167, -0.01000, -0.00500},        // HIP 42312"
    {"Tarazed", 297.042000, +10.613000, 0.53600, 0.38500},         // HIP 94779"
    {"Triangulum Galaxy (M33)", 23.462083, 30.659722, 0.0, 0.0},   //"
    {"Vega", 279.234734, +38.783688, 0.20094, 0.28623},            // HIP 91262"
    {"Whirlpool Galaxy (M51)", 202.479167, 47.195278, 0.0, 0.0},   //"
    {"Zubenelgenubi", 229.251667, -16.202500, -0.01000, -0.00500}, // HIP 72622"
    {"Zubeneschamali", 233.671667, -9.382500, -0.01200, -0.00600}, // HIP 73473"
};

// Structure for celestial coordinates
struct s_RADec
{
  MMFLOAT RA;  // Right Ascension in hours (0-24)
  MMFLOAT Dec; // Declination in degrees (-90 to +90)
};
MMFLOAT getJulianCenturies(int day, int month, int year, int hour, int minute, int second);
MMFLOAT getSiderealTime(int day, int month, int year, int hour, int minute, int second, MMFLOAT longitude);
struct s_RADec getCelestialPosition(enum CelestialBody body, MMFLOAT T, MMFLOAT latitude, MMFLOAT sidereal);
struct s_RADec applyProperMotionAndPrecession(MMFLOAT ra0, MMFLOAT dec0, MMFLOAT pm_ra, MMFLOAT pm_dec, MMFLOAT T);
void localRA(struct s_RADec star, MMFLOAT *altitude, MMFLOAT *azimuth, MMFLOAT latitude, MMFLOAT sidereal);
#endif
void GPS_parse(char *nmea);

#define EPOCH_ADJUSTMENT_DAYS 719468L
/* year to which the adjustment was made */
#define ADJUSTED_EPOCH_YEAR 0
/* 1st March of year 0 is Wednesday */
#define ADJUSTED_EPOCH_WDAY 3
/* there are 97 leap years in 400-year periods. ((400 - 97) * 365 + 97 * 366) */
#define DAYS_PER_ERA 146097L
/* there are 24 leap years in 100-year periods. ((100 - 24) * 365 + 24 * 366) */
#define DAYS_PER_CENTURY 36524L
/* there is one leap year every 4 years */
#define DAYS_PER_4_YEARS (3 * 365 + 366)
/* number of days in a non-leap year */
#define DAYS_PER_YEAR 365
/* number of days in January */
#define DAYS_IN_JANUARY 31
/* number of days in non-leap February */
#define DAYS_IN_FEBRUARY 28
/* number of years per era */
#define YEARS_PER_ERA 400
#define SECSPERDAY 86400
#define SECSPERHOUR 3600
#define SECSPERMIN 60
#define DAYSPERWEEK 7
#define YEAR_BASE 1900
/* Number of days per month (except for February in leap years). */
static const int monoff[] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

static int
is_leap_year(int year)
{
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int
leap_days(int y1, int y2)
{
  --y1;
  --y2;
  return (y2 / 4 - y1 / 4) - (y2 / 100 - y1 / 100) + (y2 / 400 - y1 / 400);
}
struct tm *
gmtime_r(const time_t *__restrict tim_p,
         struct tm *__restrict res)
{
  long days, rem;
  const time_t lcltime = *tim_p;
  int era, weekday, year;
  unsigned erayear, yearday, month, day;
  unsigned long eraday;

  days = lcltime / SECSPERDAY + EPOCH_ADJUSTMENT_DAYS;
  rem = lcltime % SECSPERDAY;
  if (rem < 0)
  {
    rem += SECSPERDAY;
    --days;
  }

  /* compute hour, min, and sec */
  res->tm_hour = (int)(rem / SECSPERHOUR);
  rem %= SECSPERHOUR;
  res->tm_min = (int)(rem / SECSPERMIN);
  res->tm_sec = (int)(rem % SECSPERMIN);

  /* compute day of week */
  if ((weekday = ((ADJUSTED_EPOCH_WDAY + days) % DAYSPERWEEK)) < 0)
    weekday += DAYSPERWEEK;
  res->tm_wday = weekday;

  /* compute year, month, day & day of year */
  /* for description of this algorithm see
   * http://howardhinnant.github.io/date_algorithms.html#civil_from_days */
  era = (days >= 0 ? days : days - (DAYS_PER_ERA - 1)) / DAYS_PER_ERA;
  eraday = days - era * DAYS_PER_ERA; /* [0, 146096] */
  erayear = (eraday - eraday / (DAYS_PER_4_YEARS - 1) + eraday / DAYS_PER_CENTURY -
             eraday / (DAYS_PER_ERA - 1)) /
            365;                                                              /* [0, 399] */
  yearday = eraday - (DAYS_PER_YEAR * erayear + erayear / 4 - erayear / 100); /* [0, 365] */
  month = (5 * yearday + 2) / 153;                                            /* [0, 11] */
  day = yearday - (153 * month + 2) / 5 + 1;                                  /* [1, 31] */
  month += month < 10 ? 2 : -10;
  year = ADJUSTED_EPOCH_YEAR + erayear + era * YEARS_PER_ERA + (month <= 1);

  res->tm_yday = yearday >= DAYS_PER_YEAR - DAYS_IN_JANUARY - DAYS_IN_FEBRUARY ? yearday - (DAYS_PER_YEAR - DAYS_IN_JANUARY - DAYS_IN_FEBRUARY) : yearday + DAYS_IN_JANUARY + DAYS_IN_FEBRUARY + is_leap_year(erayear);
  res->tm_year = year - YEAR_BASE;
  res->tm_mon = month;
  res->tm_mday = day;

  res->tm_isdst = 0;

  return (res);
}
struct tm *
gmtime(const time_t *tim_p)
{
  struct _reent *reent = _REENT;

  _REENT_CHECK_TM(reent);
  return gmtime_r(tim_p, (struct tm *)_REENT_TM(reent));
}

time_t
timegm(const struct tm *tm)
{
  int year;
  time_t days;
  time_t hours;
  time_t minutes;
  time_t seconds;

  year = 1900 + tm->tm_year;
  days = 365 * (year - 1970) + leap_days(1970, year);
  days += monoff[tm->tm_mon];

  if (tm->tm_mon > 1 && is_leap_year(year))
    ++days;
  days += tm->tm_mday - 1;

  hours = days * 24 + tm->tm_hour;
  minutes = hours * 60 + tm->tm_min;
  seconds = minutes * 60 + tm->tm_sec;

  return seconds;
}

/* ************************************************************************** */
/* ************************************************************************** */
// Section: Local Functions                                                   */
/* ************************************************************************** */
/* ************************************************************************** */

/*  A brief description of a section can be given directly below the section
    banner.
 */

/* ************************************************************************** */

/**
  @Function
    int ExampleLocalFunctionName ( int param1, int param2 )

  @Summary
    Brief one-line description of the function.

  @Description
    Full description, explaining the purpose and usage of the function.
    <p>
    Additional description in consecutive paragraphs separated by HTML
    paragraph breaks, as necessary.
    <p>
    Type "JavaDoc" in the "How Do I?" IDE toolbar for more information on tags.

  @Precondition
    List and describe any required preconditions. If there are no preconditions,
    enter "None."

  @Parameters
    @param param1 Describe the first parameter to the function.

    @param param2 Describe the second parameter to the function.

  @Returns
    List (if feasible) and describe the return values of the function.
    <ul>
      <li>1   Indicates an error occurred
      <li>0   Indicates an error did not occur
    </ul>

  @Remarks
    Describe any special behavior not described above.
    <p>
    Any additional remarks.

  @Example
    @code
    if(ExampleFunctionName(1, 2) == 0)
    {
        return 3;
    }
 */
// static int ExampleLocalFunction(int param1, int param2) {
//     return 0;
// }

#define INDENT_SPACES "  "

/* ************************************************************************** */
/* ************************************************************************** */
// Section: Interface Functions                                               */
/* ************************************************************************** */
/* ************************************************************************** */

/*  A brief description of a section can be given directly below the section
    banner.
 */

// *****************************************************************************

/**
  @Function
    int ExampleInterfaceFunctionName ( int param1, int param2 )

  @Summary
    Brief one-line description of the function.

  @Remarks
    Refer to the example_file.h interface header for function usage details.
 */
// int ExampleInterfaceFunction(int param1, int param2) {
//     return 0;
// }
/*  @endcond */

void fun_GPS(void)
{
  sret = GetTempStrMemory(); // this will last for the life of the command
  if (!(GPSchannel || PinDef[Option.GPSTX].mode & UART0TX || PinDef[Option.GPSTX].mode & UART1RX))
    error("GPS not activated");
  if (checkstring(ep, (unsigned char *)"LATITUDE") != NULL)
  {
    fret = GPSlatitude;
    targ = T_NBR;
  }
  else if (checkstring(ep, (unsigned char *)"LONGITUDE") != NULL)
  {
    fret = GPSlongitude;
    targ = T_NBR;
  }
  else if (checkstring(ep, (unsigned char *)"SPEED") != NULL)
  {
    fret = GPSspeed;
    targ = T_NBR;
  }
  else if (checkstring(ep, (unsigned char *)"TRACK") != NULL)
  {
    fret = GPStrack;
    targ = T_NBR;
  }
  else if (checkstring(ep, (unsigned char *)"VALID") != NULL)
  {
    iret = GPSvalid;
    targ = T_INT;
  }
  else if (checkstring(ep, (unsigned char *)"TIME") != NULL)
  {
    sret = (unsigned char *)GPStime;
    targ = T_STR;
  }
  else if (checkstring(ep, (unsigned char *)"DATE") != NULL)
  {
    sret = (unsigned char *)GPSdate;
    targ = T_STR;
  }
  else if (checkstring(ep, (unsigned char *)"SATELLITES") != NULL)
  {
    iret = GPSsatellites;
    targ = T_INT;
  }
  else if (checkstring(ep, (unsigned char *)"ALTITUDE") != NULL)
  {
    fret = GPSaltitude;
    targ = T_NBR;
  }
  else if (checkstring(ep, (unsigned char *)"DOP") != NULL)
  {
    fret = GPSdop;
    targ = T_NBR;
  }
  else if (checkstring(ep, (unsigned char *)"FIX") != NULL)
  {
    iret = GPSfix;
    targ = T_INT;
  }
  else if (checkstring(ep, (unsigned char *)"GEOID") != NULL)
  {
    fret = GPSgeoid;
    targ = T_NBR;
  }
#ifdef rp2350
  else if (checkstring(ep, (unsigned char *)"SIDEREAL") != NULL)
  {
    fret = GPSSidereal;
    targ = T_NBR;
  }
  else if (checkstring(ep, (unsigned char *)"JULIAN") != NULL)
  {
    fret = GPSJulian;
    targ = T_NBR;
  }
#endif
  else
    error("Invalid command");
}
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

void processgps(void)
{
  if (GPSTimer > 2000)
  {
    GPSvalid = 0;
  }
  if (gpsready != NULL)
  {
    GPS_parse((char *)gpsready);
    GPSTimer = 0;
    gpsready = NULL;
  }
}
uint8_t parseHex(char c)
{
  if (c < '0')
    return 0;
  if (c <= '9')
    return c - '0';
  if (c < 'A')
    return 0;
  if (c <= 'F')
    return (c - 'A') + 10;
  // if (c > 'F')
  return 0;
}
void GPS_parse(char *nmea)
{
  uint8_t hour, minute, seconds, year = 0, month = 0, day = 0;
  uint16_t __attribute__((unused)) milliseconds;
  // Floating point latitude and longitude value in degrees.
  MMFLOAT __attribute__((unused)) latitude, longitude;
  // Fixed point latitude and longitude value with degrees stored in units of 1/100000 degrees,
  // and minutes stored in units of 1/100000 degrees.  See pull #13 for more details:
  //   https://github.com/adafruit/Adafruit-GPS-Library/pull/13
  int32_t __attribute__((unused)) latitude_fixed, longitude_fixed;
  MMFLOAT latitudeDegrees = 0.0, longitudeDegrees = 0.0;
  struct tm *tm;
  struct tm tma;
  tm = &tma;
  if (gpsmonitor)
  {
    MMPrintString(nmea);
  }
  // do checksum check
  // first look if we even have one
  if (nmea[strlen(nmea) - 4] == '*')
  {
    uint16_t sum = parseHex(nmea[strlen(nmea) - 3]) * 16;
    sum += parseHex(nmea[strlen(nmea) - 2]);
    uint8_t i;
    // check checksum
    for (i = 2; i < (strlen(nmea) - 4); i++)
    {
      sum ^= nmea[i];
    }
    if (sum != 0)
    {
      // bad checksum :(
      return;
    }
  }
  char degreebuff[10];
  // look for a few common sentences
  if (strstr(nmea, "$GPGGA") || strstr(nmea, "$GNGGA"))
  {
    // found GGA
    char *p = nmea;
    // get time
    p = strchr((char *)p, ',') + 1;
    MMFLOAT timef = atof(p);
    uint32_t time = timef;
    hour = time / 10000;
    minute = (time % 10000) / 100;
    seconds = (time % 100);

    milliseconds = fmod(timef, 1.0) * 1000;

    // parse out latitude
    p = strchr((char *)p, ',') + 1;
    if (',' != *p)
    {
      strncpy(degreebuff, p, 2);
      p += 2;
      degreebuff[2] = '\0';
      latitudeDegrees = atol(degreebuff) + (atof(p) / 60.0);
    }

    p = strchr((char *)p, ',') + 1;
    if (',' != *p)
    {
      if (p[0] == 'S')
        latitudeDegrees *= -1.0;
    }
    GPSlatitude = (MMFLOAT)latitudeDegrees;

    // parse out longitude
    p = strchr((char *)p, ',') + 1;
    if (',' != *p)
    {
      strncpy(degreebuff, p, 3);
      p += 3;
      degreebuff[3] = '\0';
      longitudeDegrees = atol(degreebuff) + (atof(p) / 60.0);
    }

    p = strchr((char *)p, ',') + 1;
    if (',' != *p)
    {
      if (p[0] == 'W')
        longitudeDegrees *= -1.0;
    }
    GPSlongitude = (MMFLOAT)longitudeDegrees;

    p = strchr((char *)p, ',') + 1;
    if (',' != *p)
    {
      GPSfix = atoi(p);
    }

    p = strchr((char *)p, ',') + 1;
    if (',' != *p)
    {
      GPSsatellites = atoi(p);
    }

    p = strchr((char *)p, ',') + 1;
    if (',' != *p)
    {
      GPSdop = (MMFLOAT)atof(p);
    }

    p = strchr((char *)p, ',') + 1;
    if (',' != *p)
    {
      GPSaltitude = (MMFLOAT)atof(p);
    }

    p = strchr((char *)p, ',') + 1;
    // Skip Altitude Unts assumed to be meters
    p = strchr((char *)p, ',') + 1;
    // parse Geiod
    if (',' != *p)
    {
      GPSgeoid = (MMFLOAT)atof(p);
    }
    return;
  }
  if (strstr(nmea, "$GPRMC") || strstr(nmea, "$GNRMC"))
  {
    // found RMC
    char *p = nmea;
    int i, localGPSvalid = 0;
    // get time
    p = strchr((char *)p, ',') + 1;
    MMFLOAT timef = atof(p);
    uint32_t time = timef;
    hour = time / 10000;
    minute = (time % 10000) / 100;
    seconds = (time % 100);
    milliseconds = fmod(timef, 1.0) * 1000;
    i = tm->tm_hour;
    GPStime[1] = (hour / 10) + 48;
    GPStime[2] = (hour % 10) + 48;
    i = tm->tm_min;
    GPStime[4] = (minute / 10) + 48;
    GPStime[5] = (minute % 10) + 48;
    i = tm->tm_sec;
    GPStime[7] = (seconds / 10) + 48;
    GPStime[8] = (seconds % 10) + 48;

    p = strchr((char *)p, ',') + 1;
    if (p[0] == 'A')
      localGPSvalid = 1;
    else if (p[0] == 'V')
      localGPSvalid = 0;
    else
    {
      GPSvalid = 0;
      return;
    }

    // parse out latitude
    p = strchr((char *)p, ',') + 1;
    if (',' != *p)
    {
      strncpy(degreebuff, p, 2);
      p += 2;
      degreebuff[2] = '\0';
      latitudeDegrees = atol(degreebuff) + (atof(p) / 60.0);
    }

    p = strchr((char *)p, ',') + 1;
    if (',' != *p)
    {
      if (p[0] == 'S')
        latitudeDegrees *= -1.0;
    }
    GPSlatitude = (MMFLOAT)latitudeDegrees;

    // parse out longitude
    p = strchr((char *)p, ',') + 1;
    if (',' != *p)
    {
      strncpy(degreebuff, p, 3);
      p += 3;
      degreebuff[3] = '\0';
      longitudeDegrees = atol(degreebuff) + (atof(p) / 60.0);
    }

    p = strchr((char *)p, ',') + 1;
    if (',' != *p)
    {
      if (p[0] == 'W')
        longitudeDegrees *= -1.0;
    }
    GPSlongitude = (MMFLOAT)longitudeDegrees;
    // speed
    p = strchr((char *)p, ',') + 1;
    if (',' != *p)
    {
      GPSspeed = (MMFLOAT)atof(p);
    }

    // angle
    p = strchr((char *)p, ',') + 1;
    if (',' != *p)
    {
      GPStrack = (MMFLOAT)atof(p);
    }

    p = strchr((char *)p, ',') + 1;
    if (',' != *p && p[6] == ',')
    {
      uint32_t fulldate = atoi(p);
      day = fulldate / 10000;
      month = (fulldate % 10000) / 100;
      year = (fulldate % 100);
      GPStime[0] = 8;
      tm->tm_year = year + 100;
      tm->tm_mon = month - 1;
      tm->tm_mday = day;
      tm->tm_hour = hour;
      tm->tm_min = minute;
      tm->tm_sec = seconds;
      time_t timestamp = timegm(tm); /* See README.md if your system lacks timegm(). */
      timestamp += GPSadjust;
      tm = gmtime(&timestamp);
      i = tm->tm_hour;
      GPStime[1] = (i / 10) + 48;
      GPStime[2] = (i % 10) + 48;
      i = tm->tm_min;
      GPStime[4] = (i / 10) + 48;
      GPStime[5] = (i % 10) + 48;
      i = tm->tm_sec;
      GPStime[7] = (i / 10) + 48;
      GPStime[8] = (i % 10) + 48;
      i = tm->tm_mday;
      GPSdate[0] = 10;
      GPSdate[1] = (i / 10) + 48;
      GPSdate[2] = (i % 10) + 48;
      i = tm->tm_mon + 1;
      GPSdate[4] = (i / 10) + 48;
      GPSdate[5] = (i % 10) + 48;
      i = tm->tm_year % 100;
      GPSdate[9] = (i / 10) + 48;
      GPSdate[10] = (i % 10) + 48;
#ifdef rp2350
      // Calculate Local Sidereal Time
      GPSSidereal = getSiderealTime(day, month, year + 2000, hour, minute, seconds, GPSlongitude);
      GPSJulian = getJulianCenturies(day, month, year + 2000, hour, minute, seconds);
#endif
      // we don't parse the remaining, yet!
      GPSvalid = localGPSvalid;
      return;
    }
  }

  return;
}
#ifdef rp2350
/**
 * @brief Calculate Local Sidereal Time
 *
 * Calculates the Local Sidereal Time for a given date, time, and longitude.
 * Uses optimized GMST polynomial calculation.
 *
 * @param day Day of month (1-31)
 * @param month Month (1-12)
 * @param year Full year (e.g., 2025)
 * @param hour Hour (0-23) UTC
 * @param minute Minute (0-59)
 * @param second Second (0-59)
 * @param longitude Observer's longitude in degrees (positive East)
 * @return Local Sidereal Time in hours (0-24)
 */
MMFLOAT getSiderealTime(int day, int month, int year, int hour, int minute, int second, MMFLOAT longitude)
{
  // Simplified Julian Day calculation from date
  int a = (14 - month) / 12;
  int y = year + 4800 - a;
  int m = month + 12 * a - 3;
  int jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;

  // Days since J2000.0 (2000 Jan 1 12h UT = JD 2451545.0)
  // Time fraction: combine all time components in one calculation
  MMFLOAT d = (MMFLOAT)(jdn - 2451545) + (hour - 12.0 + minute * 0.0166666667 + second * 0.000277777778) * 0.0416666667;

  // GMST in degrees, simplified polynomial (combine T calculation inline)
  MMFLOAT T = d * 0.0000273791; // d / 36525
  MMFLOAT gmst = 280.46061837 + 360.98564736629 * d + T * T * (0.000387933 - T * 0.0000000258);

  // Add longitude and normalize to 0-360 in one step
  gmst += longitude;
  gmst -= (int)(gmst * 0.00277777778) * 360.0; // Fast modulo for positive values
  if (gmst < 0.0)
    gmst += 360.0;

  // Convert to hours (0-24)
  return gmst * 0.06666666667;
}

/**
 * @brief Calculate altitude and azimuth of a celestial object from equatorial coordinates
 *
 * Converts Right Ascension and Declination to Altitude and Azimuth based on
 * observer's location (from GPS) and local sidereal time.
 *
 * @param star Structure containing RA (hours) and Dec (degrees) of the celestial object
 * @param altitude Pointer to store calculated altitude in degrees (-90 to +90)
 * @param azimuth Pointer to store calculated azimuth in degrees (0-360, North=0, East=90)
 */
void localRA(struct s_RADec star, MMFLOAT *altitude, MMFLOAT *azimuth, MMFLOAT latitude, MMFLOAT sidereal)
{
  // Constants
  const MMFLOAT DEG2RAD = M_PI / 180.0;
  const MMFLOAT RAD2DEG = 180.0 / M_PI;

  // Observer's latitude in radians
  MMFLOAT lat = latitude * DEG2RAD;

  // Object's declination in radians
  MMFLOAT dec = star.Dec * DEG2RAD;

  // Hour Angle = Local Sidereal Time - Right Ascension (in hours)
  // Then convert to degrees (* 15) and radians
  MMFLOAT HA_hours = sidereal - star.RA;
  MMFLOAT HA = HA_hours * 15.0 * DEG2RAD;

  // Precompute trig values
  MMFLOAT sin_lat = sin(lat);
  MMFLOAT cos_lat = cos(lat);
  MMFLOAT sin_dec = sin(dec);
  MMFLOAT cos_dec = cos(dec);
  MMFLOAT sin_HA = sin(HA);
  MMFLOAT cos_HA = cos(HA);

  // Calculate altitude using standard formula
  // sin(alt) = sin(lat)*sin(dec) + cos(lat)*cos(dec)*cos(HA)
  MMFLOAT sin_alt = sin_lat * sin_dec + cos_lat * cos_dec * cos_HA;
  MMFLOAT alt = asin(sin_alt);

  // Calculate azimuth using standard formula (North=0, East=90 convention)
  // tan(Az) = sin(HA) / (cos(HA)*sin(lat) - tan(dec)*cos(lat))
  // Use atan2 for proper quadrant: Az = atan2(sin(HA), cos(HA)*sin(lat) - tan(dec)*cos(lat))
  // Then add 180° to convert from South=0 to North=0 convention
  MMFLOAT az_x = cos_HA * sin_lat - (sin_dec / cos_dec) * cos_lat;
  MMFLOAT az_y = sin_HA;
  MMFLOAT az = atan2(az_y, az_x) + M_PI; // Add 180° to get North=0

  // Convert to degrees
  *altitude = alt * RAD2DEG;
  *azimuth = az * RAD2DEG;

  // Apply atmospheric refraction correction (Meeus Chapter 16)
  // Standard atmosphere: P=1013.25 hPa, T=15°C
  if (*altitude > -1.0)
  {
    // R = 1.02 / tan(h + 10.3/(h + 5.11)) in arcminutes
    // With pressure/temperature correction factor: (P/1010) * (283/(273+T))
    // For P=1013.25, T=15: factor = (1013.25/1010) * (283/288) = 1.0032 * 0.9826 = 0.986
    MMFLOAT h = *altitude;
    MMFLOAT R = 1.02 / tan((h + 10.3 / (h + 5.11)) * DEG2RAD);
    R *= 0.986;            // Pressure/temperature correction for standard atmosphere
    *altitude += R / 60.0; // Convert arcminutes to degrees
  }

  // Normalize azimuth to 0-360 range
  while (*azimuth >= 360.0)
    *azimuth -= 360.0;
  while (*azimuth < 0.0)
    *azimuth += 360.0;
}

/**
 * @brief Calculate celestial body position using Meeus analytical series
 *
 * Calculates apparent Right Ascension and Declination for Moon and planets.
 * For the Moon: includes topocentric parallax correction.
 * For planets: uses simplified VSOP87-based elements with perturbations.
 * Accuracy: Moon 1-2', planets 2-5' arcminutes.
 *
 * @param body Celestial body to calculate (BODY_MOON, BODY_MERCURY, etc.)
 * @return Structure containing apparent/topocentric RA (hours) and Dec (degrees)
 */
/**
 * @brief Calculate celestial body position using Meeus analytical series
 *
 * Calculates apparent Right Ascension and Declination for Moon and planets.
 * For the Moon: includes topocentric parallax correction.
 * For planets: uses simplified VSOP87-based elements with perturbations.
 * Accuracy: Moon 1-2', planets 2-5' arcminutes.
 *
 * @param body Celestial body to calculate (BODY_MOON, BODY_MERCURY, etc.)
 * @return Structure containing apparent/topocentric RA (hours) and Dec (degrees)
 */
struct s_RADec getCelestialPosition(enum CelestialBody body, MMFLOAT T, MMFLOAT latitude, MMFLOAT sidereal)
{
  struct s_RADec celestial;

  // ---------------- PLANETS ----------------
  if (body != BODY_MOON)
  {
    MMFLOAT L, a, e, i, Omega, w;
    switch (body)
    {
    case BODY_MERCURY:
      L = 252.250906 + 149472.6746358 * T;
      a = 0.38709893;
      e = 0.20563069 + 0.00002527 * T;
      i = 7.00487 - 0.00000178 * T;
      Omega = 48.33167 - 0.12214 * T;
      w = 77.45645 + 0.15940 * T;
      break;
    case BODY_VENUS:
      L = 181.979801 + 58517.8156760 * T;
      a = 0.72333199;
      e = 0.00677323 - 0.00004938 * T;
      i = 3.39471 - 0.00000030 * T;
      Omega = 76.68069 - 0.27274 * T;
      w = 131.53298 + 0.00493 * T;
      break;
    case BODY_MARS:
      L = 355.433000 + 19139.8585209 * T;
      a = 1.52366231;
      e = 0.09341233 + 0.00011902 * T;
      i = 1.85061 - 0.00000608 * T;
      Omega = 49.57854 - 0.29257 * T;
      w = 336.04084 + 0.44441 * T;
      break;
    case BODY_JUPITER:
      L = 34.351519 + 3033.6272590 * T;
      a = 5.20336301;
      e = 0.04839266 - 0.00012880 * T;
      i = 1.30530 - 0.00000155 * T;
      Omega = 100.55615 + 0.19873 * T;
      w = 14.75385 + 0.21252 * T;
      break;
    case BODY_SATURN:
      L = 50.077444 + 1213.8664925 * T;
      a = 9.53707032;
      e = 0.05415060 - 0.00034647 * T;
      i = 2.48446 + 0.00000437 * T;
      Omega = 113.71504 - 0.25638 * T;
      w = 92.43194 + 0.54180 * T;
      break;
    case BODY_URANUS:
      L = 314.055005 + 428.8199667 * T;
      a = 19.19126393;
      e = 0.04716771 - 0.00001253 * T;
      i = 0.76986 - 0.00000219 * T;
      Omega = 74.22988 + 0.04204 * T;
      w = 170.96424 + 0.09266 * T;
      break;
    case BODY_NEPTUNE:
      L = 304.348665 + 218.8756617 * T;
      a = 30.06896348;
      e = 0.00858587 + 0.00000251 * T;
      i = 1.76917 - 0.00000093 * T;
      Omega = 131.72169 - 0.00606 * T;
      w = 44.97135 - 0.00711 * T;
      break;
    default:
      celestial.RA = 0.0;
      celestial.Dec = 0.0;
      return celestial;
    }

    // Mean anomaly
    MMFLOAT M = fmod(L - w, 360.0);
    if (M < 0.0)
      M += 360.0;
    MMFLOAT M_rad = M * M_PI / 180.0;

    // Solve Kepler’s equation
    MMFLOAT E = M_rad;
    for (int iter = 0; iter < 5; iter++)
      E -= (E - e * sin(E) - M_rad) / (1.0 - e * cos(E));

    // True anomaly
    MMFLOAT nu = 2.0 * atan2(sqrt(1.0 + e) * sin(E / 2.0), sqrt(1.0 - e) * cos(E / 2.0));
    MMFLOAT r = a * (1.0 - e * cos(E));

    // Argument of latitude
    MMFLOAT i_rad = i * M_PI / 180.0;
    MMFLOAT Omega_rad = Omega * M_PI / 180.0;
    MMFLOAT w_rad = w * M_PI / 180.0;
    MMFLOAT u = nu + (w_rad - Omega_rad);

    // Heliocentric rectangular coords
    MMFLOAT xh = r * (cos(Omega_rad) * cos(u) - sin(Omega_rad) * sin(u) * cos(i_rad));
    MMFLOAT yh = r * (sin(Omega_rad) * cos(u) + cos(Omega_rad) * sin(u) * cos(i_rad));
    MMFLOAT zh = r * (sin(u) * sin(i_rad));

    // Earth heliocentric coords (simplified Meeus)
    MMFLOAT L_e = 100.466457 + 36000.76982779 * T;
    MMFLOAT a_e = 1.000001018;
    MMFLOAT e_e = 0.01670862 - 0.000042037 * T;
    MMFLOAT i_e = 0.00005;
    MMFLOAT Omega_e = -11.26064 - 0.000013 * T;
    MMFLOAT w_e = 102.93735 + 0.000046 * T;

    MMFLOAT M_e = fmod(L_e - w_e, 360.0);
    if (M_e < 0.0)
      M_e += 360.0;
    MMFLOAT M_e_rad = M_e * M_PI / 180.0;
    MMFLOAT E_e = M_e_rad;
    for (int iter = 0; iter < 5; iter++)
      E_e -= (E_e - e_e * sin(E_e) - M_e_rad) / (1.0 - e_e * cos(E_e));
    MMFLOAT nu_e = 2.0 * atan2(sqrt(1.0 + e_e) * sin(E_e / 2.0), sqrt(1.0 - e_e) * cos(E_e / 2.0));
    MMFLOAT r_e = a_e * (1.0 - e_e * cos(E_e));
    MMFLOAT i_e_rad = i_e * M_PI / 180.0;
    MMFLOAT Omega_e_rad = Omega_e * M_PI / 180.0;
    MMFLOAT w_e_rad = w_e * M_PI / 180.0;
    MMFLOAT u_e = nu_e + (w_e_rad - Omega_e_rad);
    MMFLOAT x_e = r_e * (cos(Omega_e_rad) * cos(u_e) - sin(Omega_e_rad) * sin(u_e) * cos(i_e_rad));
    MMFLOAT y_e = r_e * (sin(Omega_e_rad) * cos(u_e) + cos(Omega_e_rad) * sin(u_e) * cos(i_e_rad));
    MMFLOAT z_e = r_e * (sin(u_e) * sin(i_e_rad));

    // Geocentric coords
    MMFLOAT xg = xh - x_e;
    MMFLOAT yg = yh - y_e;
    MMFLOAT zg = zh - z_e;

    // Ecliptic longitude/latitude
    MMFLOAT lambda = atan2(yg, xg);
    MMFLOAT beta = atan2(zg, sqrt(xg * xg + yg * yg));

    // Obliquity of the ecliptic
    MMFLOAT eps = (23.439291 - 0.0130042 * T) * M_PI / 180.0;

    // Equatorial coords
    MMFLOAT ra_rad = atan2(sin(lambda) * cos(eps) - tan(beta) * sin(eps), cos(lambda));
    MMFLOAT dec_rad = asin(sin(beta) * cos(eps) + cos(beta) * sin(eps) * sin(lambda));

    celestial.RA = fmod(ra_rad * 12.0 / M_PI + 24.0, 24.0);
    celestial.Dec = dec_rad * 180.0 / M_PI;

    // --- Topocentric correction for planets ---
    MMFLOAT dist_au = sqrt(xg * xg + yg * yg + zg * zg);

    // Convert to radians
    const MMFLOAT DEG2RAD = M_PI / 180.0;
    const MMFLOAT RAD2DEG = 180.0 / M_PI;

    MMFLOAT ra = celestial.RA * 15.0 * DEG2RAD;
    MMFLOAT dec = celestial.Dec * DEG2RAD;
    MMFLOAT phi = latitude * DEG2RAD;

    // Horizontal parallax (radians)
    MMFLOAT pi = asin(6378.137 / (dist_au * 149597870.7));

    // Hour angle
    MMFLOAT H = (sidereal * 15.0 * DEG2RAD) - ra;

    MMFLOAT dRA = -(pi * cos(phi) * sin(H)) / cos(dec);
    MMFLOAT dDec = -pi * (sin(phi) * cos(dec) - cos(phi) * cos(H) * sin(dec));

    MMFLOAT ra_corr = ra + dRA;
    MMFLOAT dec_corr = dec + dDec;

    MMFLOAT ra_hours = (ra_corr * RAD2DEG) / 15.0;
    ra_hours = fmod(ra_hours, 24.0);
    if (ra_hours < 0.0)
      ra_hours += 24.0;

    celestial.RA = ra_hours;
    celestial.Dec = dec_corr * RAD2DEG;

    return celestial;
  }
  // ---------------- MOON ----------------
  // Fundamental arguments (degrees)
  MMFLOAT L0 = 218.3164477 + 481267.88123421 * T - 0.0015786 * T * T; // Mean longitude
  MMFLOAT D = 297.8501921 + 445267.1114034 * T - 0.0018819 * T * T;   // Mean elongation
  MMFLOAT M = 357.5291092 + 35999.0502909 * T - 0.0001536 * T * T;    // Sun's mean anomaly
  MMFLOAT Mm = 134.9633964 + 477198.8675055 * T + 0.0087414 * T * T;  // Moon's mean anomaly
  MMFLOAT F = 93.2720950 + 483202.0175233 * T - 0.0036539 * T * T;    // Argument of latitude

  // Longitude of ascending node
  MMFLOAT Omega = 125.04452 - 1934.136261 * T;

  // Convert to radians
  MMFLOAT L0_rad = fmod(L0, 360.0) * M_PI / 180.0;
  MMFLOAT D_rad = fmod(D, 360.0) * M_PI / 180.0;
  MMFLOAT M_rad = fmod(M, 360.0) * M_PI / 180.0;
  MMFLOAT Mm_rad = fmod(Mm, 360.0) * M_PI / 180.0;
  MMFLOAT F_rad = fmod(F, 360.0) * M_PI / 180.0;
  MMFLOAT Om_rad = fmod(Omega, 360.0) * M_PI / 180.0;

  // Periodic terms for longitude (degrees)
  MMFLOAT Sigma_l = 6.288774 * sin(Mm_rad) + 1.274027 * sin(2 * D_rad - Mm_rad) + 0.658314 * sin(2 * D_rad) + 0.213618 * sin(2 * Mm_rad) - 0.185116 * sin(M_rad) - 0.114332 * sin(2 * F_rad);

  // Latitude terms (degrees)
  MMFLOAT Sigma_b = 5.128122 * sin(F_rad) + 0.280602 * sin(Mm_rad + F_rad) + 0.277693 * sin(Mm_rad - F_rad) + 0.173237 * sin(2 * D_rad - F_rad);

  // Distance terms (km)
  MMFLOAT Sigma_r = -20905.355 * cos(Mm_rad) - 3699.111 * cos(2 * D_rad - Mm_rad) - 2955.968 * cos(2 * D_rad) - 569.925 * cos(2 * Mm_rad);

  // Geocentric ecliptic coords
  MMFLOAT lambda = L0 + Sigma_l;
  MMFLOAT beta = Sigma_b;
  MMFLOAT Delta = 385000.56 + Sigma_r;

  // Nutation
  MMFLOAT delta_psi = (-17.20 * sin(Om_rad) - 1.32 * sin(2 * L0_rad)) / 3600.0;
  MMFLOAT delta_eps = (9.20 * cos(Om_rad) + 0.57 * cos(2 * L0_rad)) / 3600.0;
  lambda += delta_psi;

  // Obliquity
  MMFLOAT eps0 = 23.439291 - 0.0130042 * T;
  MMFLOAT epsilon = eps0 + delta_eps;

  // Convert to radians
  MMFLOAT lambda_rad = fmod(lambda, 360.0) * M_PI / 180.0;
  MMFLOAT beta_rad = beta * M_PI / 180.0;
  MMFLOAT epsilon_rad = epsilon * M_PI / 180.0;

  // Equatorial coords
  MMFLOAT ra_rad = atan2(sin(lambda_rad) * cos(epsilon_rad) - tan(beta_rad) * sin(epsilon_rad),
                         cos(lambda_rad));
  MMFLOAT dec_rad = asin(sin(beta_rad) * cos(epsilon_rad) + cos(beta_rad) * sin(epsilon_rad) * sin(lambda_rad));

  // Horizontal parallax
  MMFLOAT sin_pi = 6378.14 / Delta;

  // Observer latitude
  MMFLOAT lat_rad = latitude * M_PI / 180.0;
  MMFLOAT rho_sin_phi = sin(lat_rad);
  MMFLOAT rho_cos_phi = cos(lat_rad);

  // Hour angle
  MMFLOAT H = sidereal * 15.0 * M_PI / 180.0 - ra_rad;

  // Topocentric corrections
  MMFLOAT delta_ra = atan2(-rho_cos_phi * sin_pi * sin(H),
                           cos(dec_rad) - rho_cos_phi * sin_pi * cos(H));
  MMFLOAT ra_topo = ra_rad + delta_ra;
  MMFLOAT dec_topo = atan2((sin(dec_rad) - rho_sin_phi * sin_pi) * cos(delta_ra),
                           cos(dec_rad) - rho_cos_phi * sin_pi * cos(H));

  celestial.RA = fmod(ra_topo * 12.0 / M_PI + 24.0, 24.0);
  celestial.Dec = dec_topo * 180.0 / M_PI;

  return celestial;
}

/**
 * @brief Apply proper motion and precession corrections to J2000 coordinates
 *
 * Takes J2000 epoch coordinates and proper motion values, then applies:
 * 1. Proper motion correction for elapsed time since J2000
 * 2. Precession correction from J2000 to current date using Meeus Chapter 21
 *
 * @param ra0 Right Ascension at J2000 epoch in hours (0-24)
 * @param dec0 Declination at J2000 epoch in degrees (-90 to +90)
 * @param pm_ra Proper motion in RA in arcseconds per year
 * @param pm_dec Proper motion in Dec in arcseconds per year
 * @return struct s_RADec with corrected RA (hours) and Dec (degrees)
 */
struct s_RADec applyProperMotionAndPrecession(MMFLOAT ra0, MMFLOAT dec0, MMFLOAT pm_ra, MMFLOAT pm_dec, MMFLOAT T)
{
  struct s_RADec result;
  const MMFLOAT DEG2RAD = M_PI / 180.0;
  const MMFLOAT RAD2DEG = 180.0 / M_PI;

  // Years since J2000.0 for proper motion
  MMFLOAT years = T * 100.0;

  // Apply proper motion (convert arcsec/yr to degrees and hours)
  // pm_ra already includes cos(dec) factor from catalog
  MMFLOAT ra_corrected = ra0 + (pm_ra / 3600.0) * years / 15.0; // arcsec to hours
  MMFLOAT dec_corrected = dec0 + (pm_dec / 3600.0) * years;     // arcsec to degrees

  // Precession from J2000.0 to current date (Meeus Chapter 21)
  // Precession angles in arcseconds
  MMFLOAT zeta_A = (2306.2181 + 1.39656 * T - 0.000139 * T * T) * T +
                   (0.30188 - 0.000344 * T) * T * T + 0.017998 * T * T * T;
  MMFLOAT z_A = (2306.2181 + 1.39656 * T - 0.000139 * T * T) * T +
                (1.09468 + 0.000066 * T) * T * T + 0.018203 * T * T * T;
  MMFLOAT theta_A = (2004.3109 - 0.85330 * T - 0.000217 * T * T) * T -
                    (0.42665 + 0.000217 * T) * T * T - 0.041833 * T * T * T;

  // Convert to radians
  MMFLOAT zeta = zeta_A / 3600.0 * DEG2RAD;
  MMFLOAT z = z_A / 3600.0 * DEG2RAD;
  MMFLOAT theta = theta_A / 3600.0 * DEG2RAD;

  // Convert input coordinates to radians
  MMFLOAT ra0_rad = ra_corrected * 15.0 * DEG2RAD; // hours to radians
  MMFLOAT dec0_rad = dec_corrected * DEG2RAD;

  // Apply precession rotation (Meeus equations 21.4)
  MMFLOAT A_prec = cos(dec0_rad) * sin(ra0_rad + zeta);
  MMFLOAT B_prec = cos(theta) * cos(dec0_rad) * cos(ra0_rad + zeta) - sin(theta) * sin(dec0_rad);
  MMFLOAT C_prec = sin(theta) * cos(dec0_rad) * cos(ra0_rad + zeta) + cos(theta) * sin(dec0_rad);

  // Calculate precessed coordinates
  MMFLOAT ra_rad = atan2(A_prec, B_prec) + z;
  MMFLOAT dec_rad = asin(C_prec);

  // Convert back to hours and degrees
  result.RA = ra_rad * RAD2DEG / 15.0;
  result.Dec = dec_rad * RAD2DEG;

  // Normalize RA to 0-24 range
  while (result.RA < 0.0)
    result.RA += 24.0;
  while (result.RA >= 24.0)
    result.RA -= 24.0;

  return result;
}

/**
 * @brief Calculate Julian centuries from J2000.0 epoch
 *
 * Converts date/time to Julian centuries (T) measured from the
 * J2000.0 epoch (2000 Jan 1.5 TT = JD 2451545.0).
 *
 * @param day Day of month (1-31)
 * @param month Month (1-12)
 * @param year Full year (e.g., 2025)
 * @param hour Hour (0-23) UTC
 * @param minute Minute (0-59)
 * @param second Second (0-59)
 * @return Julian centuries from J2000.0
 */
MMFLOAT getJulianCenturies(int day, int month, int year, int hour, int minute, int second)
{
  int a = (14 - month) / 12;
  int y = year + 4800 - a;
  int m = month + 12 * a - 3;
  int jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
  MMFLOAT jd = (MMFLOAT)jdn + (hour - 12.0) / 24.0 + minute / 1440.0 + second / 86400.0;

  return (jd - 2451545.0) / 36525.0;
}

void cmd_exec_star(int day, int month, int year, int hour, int minute, int second, MMFLOAT longitude, MMFLOAT latitude, unsigned char *cmd)
{
  unsigned char *tp;
  MMFLOAT *altitude, *azimuth;
  struct s_RADec star;

  // Get Julian centuries from J2000.0
  MMFLOAT T = getJulianCenturies(day, month, year, hour, minute, second);

  // Calculate Local Sidereal Time
  MMFLOAT sidereal = getSiderealTime(day, month, year, hour, minute, second, longitude);

  if ((tp = checkstring(cmd, (unsigned char *)"MOON")) ||
      (tp = checkstring(cmd, (unsigned char *)"MERCURY")) ||
      (tp = checkstring(cmd, (unsigned char *)"VENUS")) ||
      (tp = checkstring(cmd, (unsigned char *)"MARS")) ||
      (tp = checkstring(cmd, (unsigned char *)"JUPITER")) ||
      (tp = checkstring(cmd, (unsigned char *)"SATURN")) ||
      (tp = checkstring(cmd, (unsigned char *)"URANUS")) ||
      (tp = checkstring(cmd, (unsigned char *)"NEPTUNE")))
  {
    getcsargs(&tp, 7);
    if (!(argc == 3 || argc == 7))
      StandardError(2);
    altitude = findvar(argv[0], V_FIND);
    if (!(g_vartbl[g_VarIndex].type & T_NBR))
      StandardError(6);
    azimuth = findvar(argv[2], V_FIND);
    if (!(g_vartbl[g_VarIndex].type & T_NBR))
      StandardError(6);
    if (checkstring(cmd, (unsigned char *)"MOON"))
      star = getCelestialPosition(BODY_MOON, T, latitude, sidereal);
    else if (checkstring(cmd, (unsigned char *)"MERCURY"))
      star = getCelestialPosition(BODY_MERCURY, T, latitude, sidereal);
    else if (checkstring(cmd, (unsigned char *)"VENUS"))
      star = getCelestialPosition(BODY_VENUS, T, latitude, sidereal);
    else if (checkstring(cmd, (unsigned char *)"MARS"))
      star = getCelestialPosition(BODY_MARS, T, latitude, sidereal);
    else if (checkstring(cmd, (unsigned char *)"JUPITER"))
      star = getCelestialPosition(BODY_JUPITER, T, latitude, sidereal);
    else if (checkstring(cmd, (unsigned char *)"SATURN"))
      star = getCelestialPosition(BODY_SATURN, T, latitude, sidereal);
    else if (checkstring(cmd, (unsigned char *)"URANUS"))
      star = getCelestialPosition(BODY_URANUS, T, latitude, sidereal);
    else if (checkstring(cmd, (unsigned char *)"NEPTUNE"))
      star = getCelestialPosition(BODY_NEPTUNE, T, latitude, sidereal);
    localRA(star, altitude, azimuth, latitude, sidereal);
    if (argc == 7)
    {
      MMFLOAT *bodyRA = findvar(argv[4], V_FIND);
      if (!(g_vartbl[g_VarIndex].type & T_NBR))
        StandardError(6);
      MMFLOAT *bodyDec = findvar(argv[6], V_FIND);
      if (!(g_vartbl[g_VarIndex].type & T_NBR))
        StandardError(6);
      *bodyRA = star.RA;
      *bodyDec = star.Dec;
    }
    return;
  }

  // Check if cmdline matches a star name in the catalog using binary search
  {
    int numStars = sizeof(starCatalog) / sizeof(starCatalog[0]);
    int starIndex = -1;

    // Extract the star name from cmdline (up to first comma). Allow spaces in names.
    char searchName[64];
    int i = 0;
    unsigned char *p = cmd;
    // skip leading spaces
    while (*p == ' ') p++;
    // copy until comma or EOS, preserving spaces inside the name
    while (*p && *p != ',' && i < (int)(sizeof(searchName) - 1))
    {
      searchName[i++] = *p++;
    }
    // trim trailing spaces
    while (i > 0 && searchName[i - 1] == ' ') i--;
    searchName[i] = '\0';

    // Binary search through sorted catalog
    int low = 0;
    int high = numStars - 1;
    while (low <= high)
    {
      int mid = (low + high) / 2;
      int cmp = strcasecmp(searchName, starCatalog[mid].name);
      if (cmp == 0)
      {
        starIndex = mid;
        break;
      }
      else if (cmp < 0)
        high = mid - 1;
      else
        low = mid + 1;
    }

    if (starIndex >= 0)
    {
      // Found a star - skip past the name to get arguments
      tp = p; // p already points past the star name
      {
        getcsargs(&tp, 7);
        if (!(argc == 3 || argc == 7))
          StandardError(2);
        altitude = findvar(argv[0], V_FIND);
        if (!(g_vartbl[g_VarIndex].type & T_NBR))
          StandardError(6);
        azimuth = findvar(argv[2], V_FIND);
        if (!(g_vartbl[g_VarIndex].type & T_NBR))
          StandardError(6);

        // Get star data and apply corrections
        MMFLOAT ra0 = starCatalog[starIndex].ra_deg / 15.0; // Convert degrees to hours
        MMFLOAT dec0 = starCatalog[starIndex].dec_deg;
        MMFLOAT pm_ra = starCatalog[starIndex].pm_ra;
        MMFLOAT pm_dec = starCatalog[starIndex].pm_dec;
        star = applyProperMotionAndPrecession(ra0, dec0, pm_ra, pm_dec, T);
        localRA(star, altitude, azimuth, latitude, sidereal);
        if (argc == 7)
        {
          MMFLOAT *bodyRA = findvar(argv[4], V_FIND);
          if (!(g_vartbl[g_VarIndex].type & T_NBR))
            StandardError(6);
          MMFLOAT *bodyDec = findvar(argv[6], V_FIND);
          if (!(g_vartbl[g_VarIndex].type & T_NBR))
            StandardError(6);
          *bodyRA = star.RA;
          *bodyDec = star.Dec;
        }
      }
      return;
    }
  }

  // Fall through to manual RA/Dec entry
  {
    getcsargs(&cmd, 15);
    if (!(argc == 7 || argc == 11 || argc == 15))
      StandardError(2);
    // get the two variables
    altitude = findvar(argv[0], V_FIND);
    if (!(g_vartbl[g_VarIndex].type & T_NBR))
      StandardError(6);
    azimuth = findvar(argv[2], V_FIND);
    if (!(g_vartbl[g_VarIndex].type & T_NBR))
      StandardError(6);

    // Get J2000.0 catalog coordinates
    MMFLOAT ra0 = getnumber(argv[4]);  // RA in hours (J2000)
    MMFLOAT dec0 = getnumber(argv[6]); // Dec in degrees (J2000)
    MMFLOAT pm_ra = 0.0;
    MMFLOAT pm_dec = 0.0;
    if (argc == 11)
    {
      pm_ra = getnumber(argv[8]);   // Proper motion in RA (arcsec/yr)
      pm_dec = getnumber(argv[10]); // Proper motion in Dec (arcsec/yr)
    }
    star = applyProperMotionAndPrecession(ra0, dec0, pm_ra, pm_dec, T);
    localRA(star, altitude, azimuth, latitude, sidereal);
    if (argc == 15)
    {
      MMFLOAT *bodyRA = findvar(argv[12], V_FIND);
      if (!(g_vartbl[g_VarIndex].type & T_NBR))
        StandardError(6);
      MMFLOAT *bodyDec = findvar(argv[14], V_FIND);
      if (!(g_vartbl[g_VarIndex].type & T_NBR))
        StandardError(6);
      *bodyRA = star.RA;
      *bodyDec = star.Dec;
    }
    return;
  }
}

void cmd_star(void)
{
  if (cmdtoken == GetCommandValue((unsigned char *)"Star"))
  {
    if (!GPSvalid)
      error("GPS data not valid");
    int day = (GPSdate[1] - 48) * 10 + (GPSdate[2] - 48);
    int month = (GPSdate[4] - 48) * 10 + (GPSdate[5] - 48);
    int year = 2000 + (GPSdate[9] - 48) * 10 + (GPSdate[10] - 48);
    int hour = (GPStime[1] - 48) * 10 + (GPStime[2] - 48);
    int minute = (GPStime[4] - 48) * 10 + (GPStime[5] - 48);
    int second = (GPStime[7] - 48) * 10 + (GPStime[8] - 48);
    cmd_exec_star(day, month, year, hour, minute, second, GPSlongitude, GPSlatitude, cmdline);
  }
  else
  { // cmd_astro was used in which case use the location and time specified
    if (!locatevalid)
      error("Location data not valid");
    int day = locateday;
    int month = locatemonth;
    int year = locateyear;
    int hour = locatehour;
    int minute = locateminute;
    int second = locatesecond;
    MMFLOAT latitude = locatelatitude;
    MMFLOAT longitude = locatelongitude;
    cmd_exec_star(day, month, year, hour, minute, second, longitude, latitude, cmdline);
  }
}
void cmd_locate(void)
{
  getcsargs(&cmdline, 5);
  if (!(argc == 5))
    SyntaxError();
  unsigned char *arg = getCstring(argv[0]);
  MMFLOAT lat = getnumber(argv[2]);
  if (lat < -90.0 || lat > 90.0)
    error("Invalid latitude");
  MMFLOAT lon = getnumber(argv[4]);
  if (lon < -180.0 || lon > 180.0)
    error("Invalid longitude");
  {
    getargs(&arg, 11, (unsigned char *)"-/ :"); // this is a macro and must be the first executable stmt in a block
    if (!(argc == 11))
      SyntaxError();
    ;
    int d = atoi((char *)argv[0]);
    int m = atoi((char *)argv[2]);
    int y = atoi((char *)argv[4]);
    if (d > 1000)
    {
      int tmp = d;
      d = y;
      y = tmp;
    }
    if (y >= 0 && y < 100)
      y += 2000;
    if (d < 1 || d > 31 || m < 1 || m > 12 || y < 1902 || y > 2999)
      error("Invalid date");
    int h = atoi((char *)argv[6]);
    int min = atoi((char *)argv[8]);
    int s = atoi((char *)argv[10]);
    if (h < 0 || h > 23 || min < 0 || min > 59 || s < 0 || s > 59)
      error("Invalid time");
    locateday = d;
    locatemonth = m;
    locateyear = y;
    locatehour = h;
    locateminute = min;
    locatesecond = s;
    locatelatitude = lat;
    locatelongitude = lon;
    locatevalid = 1;
  }
}
#endif