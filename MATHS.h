/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

MATHS.h

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


#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)


// General definitions used by other modules
extern void Q_Mult(MMFLOAT *q1, MMFLOAT *q2, MMFLOAT *n);
extern void Q_Invert(MMFLOAT *q, MMFLOAT *n);
extern void cmd_SensorFusion(char *passcmdline);
#ifdef rp2350
extern int parsenumberarray(unsigned char *tp, MMFLOAT **a1float, int64_t **a1int, int argno, int dimensions, int *dims, bool ConstantNotAllowed);
extern int parsefloatrarray(unsigned char *tp, MMFLOAT **a1float, int argno, int dimensions, int *dims, bool ConstantNotAllowed);
extern int parseintegerarray(unsigned char *tp, int64_t **a1int, int argno, int dimensions, int *dims, bool ConstantNotAllowed);
#else
extern int parsenumberarray(unsigned char *tp, MMFLOAT **a1float, int64_t **a1int, int argno, short dimensions, short *dims, bool ConstantNotAllowed);
extern int parsefloatrarray(unsigned char *tp, MMFLOAT **a1float, int argno, int dimensions, short *dims, bool ConstantNotAllowed);
extern int parseintegerarray(unsigned char *tp, int64_t **a1int, int argno, int dimensions, short *dims, bool ConstantNotAllowed);
#endif
extern int parseany(unsigned char *tp, MMFLOAT **a1float, int64_t **a1int, unsigned char ** a1str, int *length, bool stringarray);
void MahonyQuaternionUpdate(MMFLOAT ax, MMFLOAT ay, MMFLOAT az, MMFLOAT gx, MMFLOAT gy, MMFLOAT gz, MMFLOAT mx, MMFLOAT my, MMFLOAT mz, MMFLOAT Ki, MMFLOAT Kp, MMFLOAT deltat, MMFLOAT *yaw, MMFLOAT *pitch, MMFLOAT *roll);
void MadgwickQuaternionUpdate(MMFLOAT ax, MMFLOAT ay, MMFLOAT az, MMFLOAT gx, MMFLOAT gy, MMFLOAT gz, MMFLOAT mx, MMFLOAT my, MMFLOAT mz, MMFLOAT beta, MMFLOAT deltat, MMFLOAT *pitch, MMFLOAT *yaw, MMFLOAT *roll);
extern volatile unsigned int AHRSTimer;
#define CRC4_DEFAULT_POLYNOME       0x03
#define CRC4_ITU                    0x03


// CRC 8
#define CRC8_DEFAULT_POLYNOME       0x07
#define CRC8_DVB_S2                 0xD5
#define CRC8_AUTOSAR                0x2F
#define CRC8_BLUETOOTH              0xA7
#define CRC8_CCITT                  0x07
#define CRC8_DALLAS_MAXIM           0x31                // oneWire
#define CRC8_DARC                   0x39
#define CRC8_GSM_B                  0x49
#define CRC8_SAEJ1850               0x1D
#define CRC8_WCDMA                  0x9B


// CRC 12
#define CRC12_DEFAULT_POLYNOME      0x080D
#define CRC12_CCITT                 0x080F
#define CRC12_CDMA2000              0x0F13
#define CRC12_GSM                   0x0D31


// CRC 16
#define CRC16_DEFAULT_POLYNOME      0x1021
#define CRC16_CHAKRAVARTY           0x2F15
#define CRC16_ARINC                 0xA02B
#define CRC16_CCITT                 0x1021
#define CRC16_CDMA2000              0xC867
#define CRC16_DECT                  0x0589
#define CRC16_T10_DIF               0x8BB7
#define CRC16_DNP                   0x3D65
#define CRC16_IBM                   0x8005
#define CRC16_OPENSAFETY_A          0x5935
#define CRC16_OPENSAFETY_B          0x755B
#define CRC16_PROFIBUS              0x1DCF


// CRC 32
#define CRC32_DEFAULT_POLYNOME      0x04C11DB7
#define CRC32_ISO3309               0x04C11DB7
#define CRC32_CASTAGNOLI            0x1EDC6F41
#define CRC32_KOOPMAN               0x741B8CD7
#define CRC32_KOOPMAN_2             0x32583499
#define CRC32_Q                     0x814141AB


// CRC 64
#define CRC64_DEFAULT_POLYNOME      0x42F0E1EBA9EA3693
#define CRC64_ECMA64                0x42F0E1EBA9EA3693
#define CRC64_ISO64                 0x000000000000001B
typedef struct {

	/* Controller gains */
	MMFLOAT Kp;
	MMFLOAT Ki;
	MMFLOAT Kd;

	/* Derivative low-pass filter time constant */
	MMFLOAT tau;

	/* Output limits */
	MMFLOAT limMin;
	MMFLOAT limMax;
	
	/* Integrator limits */
	MMFLOAT limMinInt;
	MMFLOAT limMaxInt;

	/* Sample time (in seconds) */
	MMFLOAT T;

	/* Controller "memory" */
	MMFLOAT integrator;
	MMFLOAT prevError;			/* Required for integrator */
	MMFLOAT differentiator;
	MMFLOAT prevMeasurement;		/* Required for differentiator */

	/* Controller output */
	MMFLOAT out;

} PIDController;

typedef struct PIDchan {
	unsigned char *interrupt;
    int process;
    PIDController *PIDparams;
    uint64_t timenext;
    bool active;
}s_PIDchan;
extern s_PIDchan PIDchannels[MAXPID+1];

MMFLOAT PIDController_Update(PIDController *pid, MMFLOAT setpoint, MMFLOAT measurement);


#endif
/*  @endcond */
