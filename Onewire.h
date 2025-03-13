/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
MMBasic

Onewire.h

Include file that contains the globals and defines for Onewire.c (One Wire support) in MMBasic.

Copyright 2012 Gerard Sexton
This file is free software: you can redistribute it and/or modify it under the terms of the GNU General
Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

************************************************************************************************************************/

// These two together take up about 4K of flash and no one seems to use them !!
//#define INCLUDE_CRC
#define INCLUDE_1WIRE_SEARCH

/* ********************************************************************************
 All other required definitions and global variables should be define here
**********************************************************************************/
#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
#ifndef ONEWIRE_HEADER
#define ONEWIRE_HEADER
extern long long int *ds18b20Timers;
extern int mmOWvalue;  
#endif
#endif
/*  @endcond */
