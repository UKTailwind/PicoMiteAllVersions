/*
 * MMBasic_Print.c — extracted from PicoMite.c.
 *
 * Number- and string-formatting helpers used by PRINT, compiled output,
 * and the VM runtime. All of them are just thin wrappers over
 * MMPrintString / SSPrintString / IntToStr / FloatToStr, so they port
 * verbatim to any platform that supplies those primitives — the host
 * stubs in host_stubs_legacy.c route them to stdout.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

void PRet(void){
    MMPrintString("\r\n");
}
void SRet(void){
    SSPrintString("\r\n");
}

void PInt(int64_t n) {
    char s[20];
    IntToStr(s, (int64_t)n, 10);
    MMPrintString(s);
}
void SInt(int64_t n) {
    char s[20];
    IntToStr(s, (int64_t)n, 10);
    SSPrintString(s);
}

void SIntComma(int64_t n) {
    SSPrintString(", "); SInt(n);
}

void PIntComma(int64_t n) {
    MMPrintString(", "); PInt(n);
}

void PIntH(unsigned long long int n) {
    char s[20];
    IntToStr(s, (int64_t)n, 16);
    MMPrintString(s);
}
void PIntB(unsigned long long int n) {
    char s[65];
    IntToStr(s, (int64_t)n, 2);
    MMPrintString(s);
}
void PIntHC(unsigned long long int n) {
    MMPrintString(", "); PIntH(n);
}
void PIntBC(unsigned long long int n) {
    MMPrintString(", "); PIntB(n);
}

void PFlt(MMFLOAT flt){
	   char s[20];
	   FloatToStr(s, flt, 4,4, ' ');
	    MMPrintString(s);
}
void PFltComma(MMFLOAT n) {
    MMPrintString(", "); PFlt(n);
}
