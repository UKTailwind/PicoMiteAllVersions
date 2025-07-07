#include "configuration.h"
#include "MMBasic.h"
#include "Functions.h"
#include "Commands.h"
#include "Operators.h"
#include "Custom.h"
#include "Hardware_Includes.h"
// this is the command table that defines the various tokens for commands in the source code
// most of them are listed in the .h files so you should not add your own here but instead add
// them to the appropiate .h file
#define INCLUDE_COMMAND_TABLE
__attribute__((used)) const struct s_tokentbl commandtbl[] = {
    #include "Functions.h"
    #include "Commands.h"
    #include "Operators.h"
    #include "Custom.h"
    #include "Hardware_Includes.h"
//    #include "AllCommands.h"
};
#undef INCLUDE_COMMAND_TABLE

// this is the token table that defines the other tokens in the source code
// most of them are listed in the .h files so you should not add your own here
// but instead add them to the appropiate .h file
#define INCLUDE_TOKEN_TABLE
__attribute__((used)) const struct s_tokentbl tokentbl[] = {
    #include "Functions.h"
    #include "Commands.h"
    #include "Operators.h"
    #include "Custom.h"
    #include "Hardware_Includes.h"
//    #include "AllCommands.h"
};
#undef INCLUDE_TOKEN_TABLE

// Initialise MMBasic
void   MIPS16 InitBasic(void) {
    DefaultType = T_NBR;
    CommandTableSize =  (sizeof(commandtbl)/sizeof(struct s_tokentbl));
    TokenTableSize =  (sizeof(tokentbl)/sizeof(struct s_tokentbl));

    ClearProgram(true);

    // load the commonly used tokens
    // by looking them up once here performance is improved considerably
    tokenTHEN  = GetTokenValue( (unsigned char *)"Then");
    tokenELSE  = GetTokenValue( (unsigned char *)"Else");
    tokenGOTO  = GetTokenValue( (unsigned char *)"GoTo");
    tokenEQUAL = GetTokenValue( (unsigned char *)"=");
    tokenTO    = GetTokenValue( (unsigned char *)"To");
    tokenSTEP  = GetTokenValue( (unsigned char *)"Step");
    tokenWHILE = GetTokenValue( (unsigned char *)"While");
    tokenUNTIL = GetTokenValue( (unsigned char *)"Until");
    tokenGOSUB = GetTokenValue( (unsigned char *)"GoSub");
    tokenAS    = GetTokenValue( (unsigned char *)"As");
    tokenFOR   = GetTokenValue( (unsigned char *)"For");
    cmdLOOP  = GetCommandValue( (unsigned char *)"Loop");
    cmdIF      = GetCommandValue( (unsigned char *)"If");
    cmdENDIF   = GetCommandValue( (unsigned char *)"EndIf");
    cmdEND_IF  = GetCommandValue( (unsigned char *)"End If");
    cmdELSEIF  = GetCommandValue( (unsigned char *)"ElseIf");
    cmdELSE_IF = GetCommandValue( (unsigned char *)"Else If");
    cmdELSE    = GetCommandValue( (unsigned char *)"Else");
    cmdSELECT_CASE = GetCommandValue( (unsigned char *)"Select Case");
    cmdCASE        = GetCommandValue( (unsigned char *)"Case");
    cmdCASE_ELSE   = GetCommandValue( (unsigned char *)"Case Else");
    cmdEND_SELECT  = GetCommandValue( (unsigned char *)"End Select");
	cmdSUB = GetCommandValue( (unsigned char *)"Sub");
	cmdFUN = GetCommandValue( (unsigned char *)"Function");
    cmdLOCAL = GetCommandValue( (unsigned char *)"Local");
    cmdSTATIC = GetCommandValue( (unsigned char *)"Static");
    cmdENDSUB= GetCommandValue( (unsigned char *)"End Sub");
    cmdENDFUNCTION = GetCommandValue( (unsigned char *)"End Function");
    cmdDO=  GetCommandValue( (unsigned char *)"Do");
    cmdFOR=  GetCommandValue( (unsigned char *)"For");
    cmdNEXT= GetCommandValue( (unsigned char *)"Next");
	cmdIRET = GetCommandValue( (unsigned char *)"IReturn");
    cmdCSUB = GetCommandValue( (unsigned char *)"CSub");
    cmdComment = GetCommandValue( (unsigned char *)"/*");
    cmdEndComment = GetCommandValue( (unsigned char *)"*/");
//  SInt(CommandTableSize);
//  SIntComma(TokenTableSize);
//  SSPrintString("\r\n");
}
