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
//int global_data;
int GPSchannel=0;
volatile char gpsbuf1[128];
volatile char gpsbuf2[128];
volatile char * volatile gpsbuf;
volatile char *gpsready;
volatile char gpscount;
volatile int gpscurrent;
volatile int gpsmonitor;
MMFLOAT GPSlatitude=0;
MMFLOAT GPSlongitude=0;
MMFLOAT GPSspeed=0;
int GPSvalid=0;
char GPStime[9]="000:00:0";
char GPSdate[11]="000-00-200";
MMFLOAT GPStrack=0;
MMFLOAT GPSdop=0;
int GPSsatellites=0;
MMFLOAT GPSaltitude=0;
MMFLOAT GPSgeoid=0;
int GPSfix=0;  
int GPSadjust=0;
void GPS_parse(char *nmea);
#define EPOCH_ADJUSTMENT_DAYS	719468L
/* year to which the adjustment was made */
#define ADJUSTED_EPOCH_YEAR	0
/* 1st March of year 0 is Wednesday */
#define ADJUSTED_EPOCH_WDAY	3
/* there are 97 leap years in 400-year periods. ((400 - 97) * 365 + 97 * 366) */
#define DAYS_PER_ERA		146097L
/* there are 24 leap years in 100-year periods. ((100 - 24) * 365 + 24 * 366) */
#define DAYS_PER_CENTURY	36524L
/* there is one leap year every 4 years */
#define DAYS_PER_4_YEARS	(3 * 365 + 366)
/* number of days in a non-leap year */
#define DAYS_PER_YEAR		365
/* number of days in January */
#define DAYS_IN_JANUARY		31
/* number of days in non-leap February */
#define DAYS_IN_FEBRUARY	28
/* number of years per era */
#define YEARS_PER_ERA		400
#define SECSPERDAY 86400
#define SECSPERHOUR 3600
#define SECSPERMIN 60
#define DAYSPERWEEK 7
#define YEAR_BASE 1900
/* Number of days per month (except for February in leap years). */
static const int monoff[] = {
	0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

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
	return (y2/4 - y1/4) - (y2/100 - y1/100) + (y2/400 - y1/400);
}
struct tm *
gmtime_r (const time_t *__restrict tim_p,
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
  res->tm_hour = (int) (rem / SECSPERHOUR);
  rem %= SECSPERHOUR;
  res->tm_min = (int) (rem / SECSPERMIN);
  res->tm_sec = (int) (rem % SECSPERMIN);

  /* compute day of week */
  if ((weekday = ((ADJUSTED_EPOCH_WDAY + days) % DAYSPERWEEK)) < 0)
    weekday += DAYSPERWEEK;
  res->tm_wday = weekday;

  /* compute year, month, day & day of year */
  /* for description of this algorithm see
   * http://howardhinnant.github.io/date_algorithms.html#civil_from_days */
  era = (days >= 0 ? days : days - (DAYS_PER_ERA - 1)) / DAYS_PER_ERA;
  eraday = days - era * DAYS_PER_ERA;	/* [0, 146096] */
  erayear = (eraday - eraday / (DAYS_PER_4_YEARS - 1) + eraday / DAYS_PER_CENTURY -
      eraday / (DAYS_PER_ERA - 1)) / 365;	/* [0, 399] */
  yearday = eraday - (DAYS_PER_YEAR * erayear + erayear / 4 - erayear / 100);	/* [0, 365] */
  month = (5 * yearday + 2) / 153;	/* [0, 11] */
  day = yearday - (153 * month + 2) / 5 + 1;	/* [1, 31] */
  month += month < 10 ? 2 : -10;
  year = ADJUSTED_EPOCH_YEAR + erayear + era * YEARS_PER_ERA + (month <= 1);

  res->tm_yday = yearday >= DAYS_PER_YEAR - DAYS_IN_JANUARY - DAYS_IN_FEBRUARY ?
      yearday - (DAYS_PER_YEAR - DAYS_IN_JANUARY - DAYS_IN_FEBRUARY) :
      yearday + DAYS_IN_JANUARY + DAYS_IN_FEBRUARY + is_leap_year(erayear);
  res->tm_year = year - YEAR_BASE;
  res->tm_mon = month;
  res->tm_mday = day;

  res->tm_isdst = 0;

  return (res);
}
struct tm *
gmtime (const time_t * tim_p)
{
  struct _reent *reent = _REENT;

  _REENT_CHECK_TM(reent);
  return gmtime_r (tim_p, (struct tm *)_REENT_TM(reent));
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
//static int ExampleLocalFunction(int param1, int param2) {
//    return 0;
//}

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
//int ExampleInterfaceFunction(int param1, int param2) {
//    return 0;
//}
/*  @endcond */

void fun_GPS(void){
    sret = GetTempMemory(STRINGSIZE);                                    // this will last for the life of the command
    if(!GPSchannel) error("GPS not activated");
    if(checkstring(ep, (unsigned char *)"LATITUDE") != NULL) {
        fret = GPSlatitude;
        targ = T_NBR;   
    }
    else if(checkstring(ep, (unsigned char *)"LONGITUDE") != NULL) {
        fret = GPSlongitude;
        targ = T_NBR;   
    }
    else if(checkstring(ep, (unsigned char *)"SPEED") != NULL) {
        fret = GPSspeed;
        targ = T_NBR;   
    }
    else if(checkstring(ep, (unsigned char *)"TRACK") != NULL) {
        fret = GPStrack;
        targ = T_NBR;   
    }
    else if(checkstring(ep, (unsigned char *)"VALID") != NULL) {
        iret = GPSvalid;
        targ = T_INT;   
    }
    else if(checkstring(ep, (unsigned char *)"TIME") != NULL) {
        sret = (unsigned char *)GPStime;
        targ = T_STR;   
    } 
    else if(checkstring(ep, (unsigned char *)"DATE") != NULL) {
        sret = (unsigned char *)GPSdate;
        targ = T_STR;   
    } 
    else if(checkstring(ep, (unsigned char *)"SATELLITES") != NULL) {
        iret = GPSsatellites;
        targ = T_INT;   
    } 
    else if(checkstring(ep, (unsigned char *)"ALTITUDE") != NULL) {
        fret = GPSaltitude;
        targ = T_NBR;   
    }
    else if(checkstring(ep, (unsigned char *)"DOP") != NULL) {
        fret = GPSdop;
        targ = T_NBR;   
    }
    else if(checkstring(ep, (unsigned char *)"FIX") != NULL) {
        iret = GPSfix;
        targ = T_INT;   
    } 
    else if(checkstring(ep, (unsigned char *)"GEOID") != NULL) {
        fret = GPSgeoid;
        targ = T_NBR;   
    }
    else error("Invalid command");
}
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
  
    
void processgps(void){
    if(GPSTimer>2000){
        GPSvalid=0;
    }
    if(gpsready !=NULL){
        GPS_parse((char *)gpsready);
        GPSTimer=0;
        gpsready=NULL;
    }
}
uint8_t parseHex(char c) {
    if (c < '0')
      return 0;
    if (c <= '9')
      return c - '0';
    if (c < 'A')
       return 0;
    if (c <= 'F')
       return (c - 'A')+10;
    // if (c > 'F')
    return 0;
}
void GPS_parse(char *nmea) {
  uint8_t hour, minute, seconds, year=0, month=0, day=0;
  uint16_t __attribute__((unused)) milliseconds;
  // Floating point latitude and longitude value in degrees.
  MMFLOAT __attribute__((unused)) latitude, longitude;
  // Fixed point latitude and longitude value with degrees stored in units of 1/100000 degrees,
  // and minutes stored in units of 1/100000 degrees.  See pull #13 for more details:
  //   https://github.com/adafruit/Adafruit-GPS-Library/pull/13
  int32_t __attribute__((unused)) latitude_fixed, longitude_fixed;
  MMFLOAT latitudeDegrees=0.0, longitudeDegrees=0.0;
  MMFLOAT geoidheight, altitude;
  MMFLOAT speed, angle, HDOP;
  MMFLOAT __attribute__((unused)) magvariation;
  char __attribute__((unused)) lat, lon;
  uint8_t fixquality, satellites;
  struct tm  *tm;
  struct tm tma;
  tm=&tma;
  if(gpsmonitor){
	  MMPrintString(nmea);
  }
  // do checksum check
  // first look if we even have one
  if (nmea[strlen(nmea)-4] == '*') {
    uint16_t sum = parseHex(nmea[strlen(nmea)-3]) * 16;
    sum += parseHex(nmea[strlen(nmea)-2]);
    uint8_t i;
    // check checksum 
    for (i=2; i < (strlen(nmea)-4); i++) {
      sum ^= nmea[i];
    }
    if (sum != 0) {
      // bad checksum :(
      return;
    }
  }
  int32_t degree;
  long minutes;
  char degreebuff[10];
  // look for a few common sentences
  if (strstr(nmea, "$GPGGA") || strstr(nmea, "$GNGGA")) {
    // found GGA
    char *p = nmea;
    // get time
    p = strchr((char *)p, ',')+1;
    MMFLOAT timef = atof(p);
    uint32_t time = timef;
    hour = time / 10000;
    minute = (time % 10000) / 100;
    seconds = (time % 100);

    milliseconds = fmod(timef, 1.0) * 1000;

    // parse out latitude
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      strncpy(degreebuff, p, 2);
      p += 2;
      degreebuff[2] = '\0';
      degree = atol(degreebuff) * 10000000;
      strncpy(degreebuff, p, 2); // minutes
      p += 3; // skip decimal point
      strncpy(degreebuff + 2, p, 4);
      degreebuff[6] = '\0';
      minutes = 50 * atol(degreebuff) / 3;
      latitude_fixed = degree + minutes;
      latitude = degree / 100000 + minutes * 0.000006F;
      latitudeDegrees = (latitude-100*(MMFLOAT)((int)(latitude/100)))/60.0;
      latitudeDegrees += (MMFLOAT)((int)(latitude/100));
    }
    
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      if (p[0] == 'S') latitudeDegrees *= -1.0;
      if (p[0] == 'N') lat = 'N';
      else if (p[0] == 'S') lat = 'S';
      else if (p[0] == ',') lat = 0;
      else return;
    }
    GPSlatitude=(MMFLOAT)latitudeDegrees;
    
    // parse out longitude
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      strncpy(degreebuff, p, 3);
      p += 3;
      degreebuff[3] = '\0';
      degree = atol(degreebuff) * 10000000;
      strncpy(degreebuff, p, 2); // minutes
      p += 3; // skip decimal point
      strncpy(degreebuff + 2, p, 4);
      degreebuff[6] = '\0';
      minutes = 50 * atol(degreebuff) / 3;
      longitude_fixed = degree + minutes;
      longitude = degree / 100000 + minutes * 0.000006F;
      longitudeDegrees = (longitude-100*(MMFLOAT)((int)(longitude/100)))/60.0;
      longitudeDegrees += (MMFLOAT)((int)(longitude/100));
    }
    
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      if (p[0] == 'W') longitudeDegrees *= -1.0;
      if (p[0] == 'W') lon = 'W';
      else if (p[0] == 'E') lon = 'E';
      else if (p[0] == ',') lon = 0;
      else return;
    }
    GPSlongitude=(MMFLOAT)longitudeDegrees;
    
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      fixquality = atoi(p);
      GPSfix=(int)fixquality;
    }
    
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      satellites = atoi(p);
      GPSsatellites=(int)satellites;
    }
    
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      HDOP = atof(p);
      GPSdop=(MMFLOAT)HDOP;
    }
    
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      altitude = atof(p);
      GPSaltitude=(MMFLOAT)altitude;
    }
    
    p = strchr((char *)p, ',')+1;
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      geoidheight = atof(p);
      GPSgeoid=(MMFLOAT)geoidheight;
    }
    return;
  }
  if (strstr(nmea, "$GPRMC") || strstr(nmea, "$GNRMC")) {
   // found RMC
    char *p = nmea;
    int i, localGPSvalid=0;
    // get time
    p = strchr((char *)p, ',')+1;
    MMFLOAT timef = atof(p);
    uint32_t time = timef;
    hour = time / 10000;
    minute = (time % 10000) / 100;
    seconds = (time % 100);
    milliseconds = fmod(timef, 1.0) * 1000;
    i=tm->tm_hour;
    GPStime[1]=(hour/10) + 48;
    GPStime[2]=(hour % 10) + 48;
    i=tm->tm_min;
    GPStime[4]=(minute/10) + 48;
    GPStime[5]=(minute % 10) + 48;
    i=tm->tm_sec;
    GPStime[7]=(seconds/10) + 48;
    GPStime[8]=(seconds % 10) + 48;

    p = strchr((char *)p, ',')+1;
    if (p[0] == 'A') 
      localGPSvalid = 1;
    else if (p[0] == 'V')
      localGPSvalid = 0;
    else
    {
        GPSvalid=0;
        return;
    }

    // parse out latitude
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      strncpy(degreebuff, p, 2);
      p += 2;
      degreebuff[2] = '\0';
      long degree = atol(degreebuff) * 10000000;
      strncpy(degreebuff, p, 2); // minutes
      p += 3; // skip decimal point
      strncpy(degreebuff + 2, p, 4);
      degreebuff[6] = '\0';
      long minutes = 50 * atol(degreebuff) / 3;
      latitude_fixed = degree + minutes;
      latitude = degree / 100000 + minutes * 0.000006F;
      latitudeDegrees = (latitude-100*(MMFLOAT)((int)(latitude/100)))/60.0;
      latitudeDegrees += (MMFLOAT)((int)(latitude/100));
    }
    
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      if (p[0] == 'S') latitudeDegrees *= -1.0;
      if (p[0] == 'N') lat = 'N';
      else if (p[0] == 'S') lat = 'S';
      else if (p[0] == ',') lat = 0;
      else return;
    }
    GPSlatitude=(MMFLOAT)latitudeDegrees;
    
    // parse out longitude
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      strncpy(degreebuff, p, 3);
      p += 3;
      degreebuff[3] = '\0';
      degree = atol(degreebuff) * 10000000;
      strncpy(degreebuff, p, 2); // minutes
      p += 3; // skip decimal point
      strncpy(degreebuff + 2, p, 4);
      degreebuff[6] = '\0';
      minutes = 50 * atol(degreebuff) / 3;
      longitude_fixed = degree + minutes;
      longitude = degree / 100000 + minutes * 0.000006F;
      longitudeDegrees = (longitude-100*(MMFLOAT)((int)(longitude/100)))/60.0;
      longitudeDegrees += (MMFLOAT)((int)(longitude/100));
    }
    
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      if (p[0] == 'W') longitudeDegrees *= -1.0;
      if (p[0] == 'W') lon = 'W';
      else if (p[0] == 'E') lon = 'E';
      else if (p[0] == ',') lon = 0;
      else return;
    }
    GPSlongitude=(MMFLOAT)longitudeDegrees;
    // speed
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      speed = atof(p);
      GPSspeed=(MMFLOAT)speed;
    }
    
    // angle
    p = strchr((char *)p, ',')+1;
    if (',' != *p)
    {
      angle = atof(p);
      GPStrack=(MMFLOAT)angle;
    }
    
    p = strchr((char *)p, ',')+1;
    if (',' != *p && p[6]==',')
    {
      uint32_t fulldate = atoi(p);
      day = fulldate / 10000;
      month = (fulldate % 10000) / 100;
      year = (fulldate % 100);
    GPStime[0]=8;
    tm->tm_year = year + 100;
    tm->tm_mon = month - 1;
    tm->tm_mday = day;
    tm->tm_hour = hour;
    tm->tm_min = minute;
    tm->tm_sec = seconds;
    time_t timestamp = timegm(tm); /* See README.md if your system lacks timegm(). */
    timestamp+=GPSadjust;
    tm=gmtime(&timestamp);
    i=tm->tm_hour;
    GPStime[1]=(i/10) + 48;
    GPStime[2]=(i % 10) + 48;
    i=tm->tm_min;
    GPStime[4]=(i/10) + 48;
    GPStime[5]=(i % 10) + 48;
    i=tm->tm_sec;
    GPStime[7]=(i/10) + 48;
    GPStime[8]=(i % 10) + 48;
    i=tm->tm_mday;
    GPSdate[0]=10;
    GPSdate[1]=(i/10) + 48;
    GPSdate[2]=(i % 10) + 48;
    i=tm->tm_mon+1;
    GPSdate[4]=(i/10) + 48;
    GPSdate[5]=(i % 10) + 48;
    i=tm->tm_year % 100;
    GPSdate[9]=(i/10) + 48;
    GPSdate[10]=(i % 10) + 48;
    // we don't parse the remaining, yet!
    GPSvalid=localGPSvalid;
    return;
    }
  }

  return;
}
/*  @endcond */


