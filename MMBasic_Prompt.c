/*
 * MMBasic_Prompt.c — extracted from PicoMite.c.
 *
 * Owns the interactive prompt line editor (EditInputLine), its command
 * history buffer (lastcmd[]), and InsertLastcmd. Shared by the device
 * build and the host REPL so both have the same line-editing behavior
 * (arrow-key history, function-key expansion, inline editing, etc.).
 *
 * MMPromptPos lives here too because EditInputLine is its only reader;
 * the REPL loop sets it after printing "> ".
 *
 * The OLDSTUFF-wrapped alternate EditInputLine that used to sit in
 * PicoMite.c just below the live one was dead code and was dropped
 * during extraction.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

/* Originally externed in PicoMite.c — redeclared here so this translation
 * unit compiles standalone. Definitions live with the display driver on
 * device (Draw.c / SPI-LCD.c) and are stubbed on host. */
extern void MX470Display(int fn);

unsigned char lastcmd[STRINGSIZE*2];    // used to store the command history in case the user uses the up arrow at the command prompt
int MMPromptPos;

void MIPS16 InsertLastcmd(unsigned char *s) {
int i, slen;
    if(strcmp((const char *)lastcmd, (const char *)s) == 0) return;                             // don't duplicate
    slen = strlen((const char *)s);
    if(slen < 1 || slen > sizeof(lastcmd) - 1) return;
    slen++;
    for(i = sizeof(lastcmd) - 1; i >=  slen ; i--)
        lastcmd[i] = lastcmd[i - slen];                             // shift the contents of the buffer up
    strcpy((char *)lastcmd, (char *)s);                                             // and insert the new string in the beginning
    for(i = sizeof(lastcmd) - 1; lastcmd[i]; i--) lastcmd[i] = 0;             // zero the end of the buffer
}

void MIPS16 EditInputLine(void) {
    char *p = NULL;
    char buf[MAXKEYLEN + 3];
    char goend[10];
    int lastcmd_idx, lastcmd_edit;
    int insert, /*startline,*/ maxchars;
    int CharIndex, BufEdited;
    int c, i, j;
    int l4,l3,l2;
    maxchars=255; 
    if(Option.DISPLAY_CONSOLE && Option.Width<=SCREENWIDTH){     //We will always assume the Vt100 is 80 colums if LCD is the console <=80.
      l2=SCREENWIDTH+1-MMPromptPos;
      l3=2*SCREENWIDTH+2-MMPromptPos;
      l4=3*SCREENWIDTH+3-MMPromptPos;
    }else{                         // otherwise assume the VT100 matches Option.Width
      l2=Option.Width +1-MMPromptPos;
      l3=2*Option.Width+2-MMPromptPos;
      l4=3*Option.Width+3-MMPromptPos;
    }
    // Build "\e[80C" equivalent string for the line length
    //strcpy(goend,"\e[");IntToStr(linelen,l2+MMPromptPos, 10);strcat(goend,linelen); strcat(goend, "C");
     strcpy(goend,"\e[");IntToStr(&goend[strlen(goend)],l2+MMPromptPos, 10);strcat(goend, "C");
    
    MMPrintString((char *)inpbuf);                                                     // display the contents of the input buffer (if any)
    CharIndex = strlen((const char *)inpbuf);                                          // get the current cursor position in the line
    insert = false;
//    Cursor = C_STANDARD;
    lastcmd_edit = lastcmd_idx = 0;
    BufEdited = false; //(CharIndex != 0);
    while(1) {
        c = MMgetchar();
        if(c == TAB) {
            strcpy(buf, "        ");
            switch (Option.Tab) {
              case 2:
                buf[2 - (CharIndex % 2)] = 0; break;
              case 3:
                buf[3 - (CharIndex % 3)] = 0; break;
              case 4:
                buf[4 - (CharIndex % 4)] = 0; break;
              case 8:
                buf[8 - (CharIndex % 8)] = 0; break;
            }
        } else {
            buf[0] = c;
            buf[1] = 0;
        }
        do {
            switch(buf[0]) {
                case '\r':
                case '\n':  //if(autoOn && atoi(inpbuf) > 0) autoNext = atoi(inpbuf) + autoIncr;
                            //if(autoOn && !BufEdited) *inpbuf = 0;
                            goto saveline;
                            break;

                case '\b':
                            if(CharIndex > 0) {
                                BufEdited = true;
                                i = CharIndex - 1;
                                /* Fast path: deleting the last char of a
                                 * single-line input. \b \b is enough; no
                                 * need to rewind the cursor and redraw the
                                 * tail. Falls through to the general path
                                 * for mid-line or wrapped-line cases. */
                                if (CharIndex == (int)strlen((const char *)inpbuf)
                                    && CharIndex < l2) {
                                    inpbuf[i] = 0;
                                    MMPrintString("\b \b");
                                    CharIndex--;
                                    if (strlen((const char *)inpbuf) == 0) BufEdited = false;
                                    break;
                                }
                                j= CharIndex;
                                for(p = (char *)inpbuf + i; *p; p++) *p = *(p + 1);                 // remove the char from inpbuf
 
                                // Lets put the cursor at the beginning of where the command is displayed.
                                // backspace to the beginning of line
#define USEBACKSPACE
#ifdef USEBACKSPACE                                
                                while(j)  {
                                  if (j==l4 || j==l3 ||j==l2 ){DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{ MMputchar('\b',0);}
                                  j--;
                                }
                                fflush(stdout);                                
                                 MX470Display(CLEAR_TO_EOS);SSPrintString("\033[0J");        //Clear to End Of Screen
#else
                                 CurrentX=0;CurrentY=CurrentY-((CharIndex+1)/Option.Width * gui_font_height);
                                 if (CharIndex>l4-1)SSPrintString("\e[3A");
                                 else if (CharIndex>l3-1)SSPrintString("\e[2A");
                                 else if(CharIndex>l2-1)SSPrintString("\e[1A");
                                 SSPrintString("\r");
                                 //CurrentX=0;SerUSBPutS("\r");
                                 MX470Display(CLEAR_TO_EOS);SSPrintString("\033[0J");
        				         MMPrintString("> ");
        				         fflush(stdout);


#endif

                                 j=0;
                                 while(j < strlen((const char *)inpbuf)) {
                                      MMputchar(inpbuf[j],0);
                                      if((j==l4-1 || j==l3-1 || j==l2-1 ) && j == strlen((const char *)inpbuf)-1 ){SSPrintString(" ");SSPrintString("\b");}
                                      if((j==l4-1 || j==l3-1 || j==l2-1 ) && j < strlen((const char *)inpbuf)-1 ){SerialConsolePutC(inpbuf[j+1],0);SSPrintString("\b");}
                                      j++;
                                 }
                                 fflush(stdout);

                                 // return the cursor to the right position
                                 for(j = strlen((const char *)inpbuf); j > i; j--){
                                   if (j==l4 || j==l3 || j==l2) {DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{MMputchar('\b',0);}
                                 }
                                 CharIndex--;
                                 fflush(stdout);
                                 if(strlen((const char *)inpbuf)==0)BufEdited = false;
                            }
                            break;

                case CTRLKEY('S'):
                case LEFT:

                	    BufEdited = true;
                	    insert=false; //left at first char will turn OVR on
                	    if(CharIndex > 0) {
                               // if(CharIndex == strlen((const char *)inpbuf)) {
                                    //insert = true;
                               // }
                                if (CharIndex==l4 || CharIndex==l3 || CharIndex==l2 ){DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{MMputchar('\b',1);}
                                insert=true; //Any left turns on INS
                                CharIndex--;
                         }
                     break;

                case CTRLKEY('D'):
                case RIGHT:

                	  if(CharIndex < strlen((const char *)inpbuf)) {
                	   	BufEdited = true;
                	    MMputchar(inpbuf[CharIndex],1);
                	    if((CharIndex==l4-1 || CharIndex==l3-1|| CharIndex==l2-1 ) && CharIndex == strlen((const char *)inpbuf)-1 ){SSPrintString(" ");SSPrintString("\b");}
                	    if((CharIndex==l4-1 || CharIndex==l3-1|| CharIndex==l2-1 ) && CharIndex < strlen((const char *)inpbuf)-1 ){SerialConsolePutC(inpbuf[CharIndex+1],0);SSPrintString("\b");}
                        CharIndex++;
                      }
//                      insert=false; //right always switches to OVER
                     break;
                case CTRLKEY(']'):
                case DEL:

                	      if(CharIndex < strlen((const char *)inpbuf)) {
                	           BufEdited = true;
                	           i = CharIndex;

                	           for(p = (char *)inpbuf + i; *p; p++) *p = *(p + 1);                 // remove the char from inpbuf
                	           j = strlen((const char *)inpbuf);
                	           // Lets put the cursor at the beginning of where the command is displayed.
                               // backspace to the beginning of line
                	           j=CharIndex;
                               while(j)  {
                                  if (j==l4 || j==l3 ||j==l2 ){DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{ MMputchar('\b',0);}
                                  j--;
                               }
                               fflush(stdout);
                               MX470Display(CLEAR_TO_EOS);SSPrintString("\033[0J");        //Clear to End Of Screen
                               j=0;
                               while(j < strlen((const char *)inpbuf)) {
                                    MMputchar(inpbuf[j],0);
                                    if((j==l4-1 || j==l3-1 || j==l2-1 ) && j == strlen((const char *)inpbuf)-1 ){SSPrintString(" ");SSPrintString("\b");}
                                    if((j==l4-1 || j==l3-1 || j==l2-1 ) && j < strlen((const char *)inpbuf)-1 ){SerialConsolePutC(inpbuf[j+1],0);SSPrintString("\b");}
                                    j++;
                               }
                               fflush(stdout);
                               // return the cursor to the right position
                               for(j = strlen((const char *)inpbuf); j > i; j--){
                                 if (j==l4 || j==l3 || j==l2) {DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{ MMputchar('\b',0);}
                               }
                               fflush(stdout);
                           }
                	       break;


                case CTRLKEY('N'):
                case INSERT:insert = !insert;
//                            Cursor = C_STANDARD + insert;
                            break;

                case CTRLKEY('U'):
                case HOME:  
                           BufEdited = true;
                           if(CharIndex > 0) {
                                if(CharIndex == strlen((const char *)inpbuf)) {
                                    insert = true;
//                                    Cursor = C_INSERT;
                                }
                                // backspace to the beginning of line
                                while(CharIndex)  {
                                 	 if (CharIndex==l4 || CharIndex==l3 || CharIndex==l2 ){DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{MMputchar('\b',0);}
                                   	 CharIndex--;
                                }
                                fflush(stdout);
                            }else{ //HOME @ home turns off edit mode
                            	BufEdited = false;
                                insert=false; //home at first char will turn OVR on
                            }
                            break;

                case CTRLKEY('K'):
                case END:   
                            BufEdited = true;
                            while(CharIndex < strlen((const char *)inpbuf)){
                                MMputchar(inpbuf[CharIndex++],0);
                            }   
                            fflush(stdout);
                            break;

/*            if(c == F2)  tp = "RUN";
            if(c == F3)  tp = "LIST";
            if(c == F4)  tp = "EDIT";
            if(c == F10) tp = "AUTOSAVE";
            if(c == F11) tp = "XMODEM RECEIVE";
            if(c == F12) tp = "XMODEM SEND";
            if(c == F5) tp = Option.F5key;
            if(c == F6) tp = Option.F6key;
            if(c == F7) tp = Option.F7key;
            if(c == F8) tp = Option.F8key;
            if(c == F9) tp = Option.F9key;
*/
                case 0x91:
                    if(*Option.F1key)strcpy(&buf[1],(char *)Option.F1key);
                    break;
                case 0x92:
                    strcpy(&buf[1],"RUN\r\n");
                    break;
                case 0x93:
                    strcpy(&buf[1],"LIST\r\n");
                    break;
                case 0x94:
                    strcpy(&buf[1],"EDIT\r\n");
                    break;
                case 0x95:
                    if(*Option.F5key){
                        strcpy(&buf[1],(char *)Option.F5key);
                    }else{
                         /*** F5 will clear the VT100  ***/
            	         SSPrintString("\e[2J\e[H");
            	         fflush(stdout);
                         if(Option.DISPLAY_CONSOLE){ClearScreen(gui_bcolour);CurrentX=0;CurrentY=0;}
                         if(FindSubFun((unsigned char *)"MM.PROMPT", 0) >= 0) {
                            ExecuteProgram((unsigned char *)"MM.PROMPT\0");
                         } else{
                             MMPrintString("> ");                                    // print the prompt
                         }                           
            	         //MMPrintString("> ");
            	         fflush(stdout);
                    }    
                    break;
                case 0x96:
                    if(*Option.F6key)strcpy(&buf[1],(char *)Option.F6key);
                    break;
                case 0x97:
                    if(*Option.F7key)strcpy(&buf[1],(char *)Option.F7key);
                    break;
                case 0x98:
                    if(*Option.F8key)strcpy(&buf[1],(char *)Option.F8key);
                    break;
                case 0x99:
                    if(*Option.F9key)strcpy(&buf[1],(char *)Option.F9key);
                    break;
                case 0x9a:
                    strcpy(&buf[1],"AUTOSAVE\r\n");
                    break;
                case 0x9b:
                    strcpy(&buf[1],"XMODEM RECEIVE\r\n");
                    break;
                 case 0x9c:
                    strcpy(&buf[1],"XMODEM SEND\r\n");
                    break;
                case CTRLKEY('E'):
                case UP:    if(!(BufEdited /*|| autoOn || CurrentLineNbr */)) {
                              
                                if(lastcmd_edit) {
                                    i = lastcmd_idx + strlen((const char *)&lastcmd[lastcmd_idx]) + 1;    // find the next command
                                    if(lastcmd[i] != 0 && i < sizeof(lastcmd) - 1) lastcmd_idx = i;  // and point to it for the next time around
                                } else
                                    lastcmd_edit = true;
                                strcpy((char *)inpbuf, (const char *)&lastcmd[lastcmd_idx]);                      // get the command into the buffer for editing
                                goto insert_lastcmd;
                            }
                            break;

                case CTRLKEY('X'):
                case DOWN:  
                           if(!(BufEdited /*|| autoOn || CurrentLineNbr */)) {
                               if(lastcmd_idx == 0)
                                    *inpbuf = lastcmd_edit = 0;
                                else {
                                    for(i = lastcmd_idx - 2; i > 0 && lastcmd[i - 1] != 0; i--);// find the start of the previous command
                                    lastcmd_idx = i;                                        // and point to it for the next time around
                                    strcpy((char *)inpbuf, (const char *)&lastcmd[i]);                            // get the command into the buffer for editing
                                }
                                goto insert_lastcmd;                                        // gotos are bad, I know, I know
                            }
                            break;

                insert_lastcmd: 

                            // If NoScroll and its near the bottom then clear screen and write command at top
                            //if(Option.NoScroll && Option.DISPLAY_CONSOLE && (CurrentY + 2*gui_font_height >= VRes)){
                            if(Option.NoScroll && Option.DISPLAY_CONSOLE && (CurrentY + (2 + strlen((const char *)inpbuf)/Option.Width)*gui_font_height >= VRes)){    
                                      ClearScreen(gui_bcolour);CurrentX=0;CurrentY=0;
                                      if(FindSubFun((unsigned char *)"MM.PROMPT", 0) >= 0) {
                                         SSPrintString("\r");
                                         ExecuteProgram((unsigned char *)"MM.PROMPT\0");
                                      } else{
                                         SSPrintString("\r");
                                         MMPrintString("> ");                           // print the prompt
                                      }    
                          
                            }else{
			                   // Lets put the cursor at the beginning of where the command is displayed.
                               // backspace to the beginning of line
                                j=CharIndex;  //????????????????????????????????
                                while(j)  {
                                  if (j==l4 || j==l3 ||j==l2 ){DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{ MMputchar('\b',0);}
                                  j--;
                                }
                                fflush(stdout);
                                MX470Display(CLEAR_TO_EOS);SSPrintString("\033[0J");        //Clear to End Of Screen
                            }

				            CharIndex = strlen((const char *)inpbuf);
                            MMPrintString((char *)inpbuf);                                          // display the line
                            if(CharIndex==l4 || CharIndex==l3 || CharIndex==l2){SSPrintString(" ");SSPrintString("\b");}
                            fflush(stdout);
                            CharIndex = strlen((const char *)inpbuf);                                     // get the current cursor position in the line
                            break;
                            

 
 
                default:    if(buf[0] >= ' ' && buf[0] < 0x7f) {
                               // BufEdited = true;  
                               
                                i = CharIndex;
                                j = strlen((const char *)inpbuf);
                                if(insert) {
                                    if(strlen((const char *)inpbuf) >= maxchars - 1) break;               // sorry, line full
                                    for(p = (char *)inpbuf + strlen((const char *)inpbuf); j >= CharIndex; p--, j--) *(p + 1) = *p;
                                    inpbuf[CharIndex] = buf[0];                             // insert the char
                                    MMPrintString((char *)&inpbuf[CharIndex]);                      // display new part of the line
                                    CharIndex++;
                                   // return the cursor to the right position
                                    for(j = strlen((const char *)inpbuf); j > CharIndex; j--){
                                      if (j==l4 || j==l3 || j==l2){DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{ MMputchar('\b',0);}
                                    }
                                    fflush(stdout);  
                                } else {
                                    if(strlen((const char *)inpbuf) >= maxchars-1 ) break;               // sorry, line full  just ignore
                                    inpbuf[strlen((const char *)inpbuf) + 1] = 0;                         // incase we are adding to the end of the string
                                    inpbuf[CharIndex++] = buf[0];                           // overwrite the char
                                    MMputchar(buf[0],0);  
                                    if(j==l4-1 || j==l3-1 || j==l2-1){SSPrintString(" ");SSPrintString("\b");}
                                    fflush(stdout);
                                  
                                }
#if !HAL_PORT_IS_VGA                                                                     
                                i = CharIndex;
                                j = strlen((const char *)inpbuf);
                                // If its going to scroll then clear screen
                                if(Option.NoScroll && Option.DISPLAY_CONSOLE){
                                   if(CurrentY + 2*gui_font_height >= VRes) {
                                      ClearScreen(gui_bcolour);/*CurrentX=0*/;CurrentY=0;
                                      CurrentX = (MMPromptPos-2)*gui_font_width  ;          
                                      //if(FindSubFun((unsigned char *)"MM.PROMPT", 0) >= 0) {
                                      //   ExecuteProgram((unsigned char *)"MM.PROMPT\0");
                                      //} else{
                                         //SSPrintString("\r");
                                         //MMPrintString("> ");                           // print the prompt
                                         DisplayPutC('>');
                                         DisplayPutC(' ');
                                      //}    
                                      DisplayPutS((char *)inpbuf);                      // display the line
                                      
                                    }
                                }
#endif                                
 
                            }
                            break;
            }
            for(i = 0; i < MAXKEYLEN + 1; i++) buf[i] = buf[i + 1];                             // shuffle down the buffer to get the next char
        } while(*buf);
        if(CharIndex == strlen((const char *)inpbuf)) {
          insert = false;
//        Cursor = C_STANDARD;
        }
    }
    
    saveline:
//    Cursor = C_STANDARD;
   
   if(strlen((const char *)inpbuf) < maxchars)InsertLastcmd(inpbuf);
   MMPrintString("\r\n");
}
