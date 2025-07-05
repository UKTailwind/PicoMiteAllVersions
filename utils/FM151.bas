'File Manager V1.51 for PicoMiteVGA/HDMI by JAVAVI (c)2025
DATA_USER_COMMANDS:
Data ".."
Data "MEMORY      'List the amount of memory currently in use."
Data "FLASH LIST  'Display a list of flash including the first line."
Data "OPTION LIST 'Display list the settings of option that have been changed."
Data "LIST PINS   'List Pins with assigned functions."
Data "PRINT Time$, Date$, Day$(Date$)"
Data "PRINT \qRAM HEAP =\q;MM.Info(HEAP)/1024"
Data "SetPin GP25, DOUT"
Data "OPTION HEARTBEAT OFF"
Data "> FILES \q*\q,name  'List files in current directories, sort by name."
Data "> FILES \q*\q,size  'List files in current directories, sort by size."
Data "> FILES \q*\q,time  'List files in current directories, sort by time."
Data "> FILES \q*\q,type  'List files in current directories, sort by type."
Data "> LIST COMMANDS"
Data "> LIST FUNCTIONS"
Data "> LIST VARIABLES"
Data "> '------------------------------------------------------------------"
Data "> 'For start FM from flash memory enter this instructions"
Data "> FLASH ERASE ALL   'Erase all flash program location."
Data "> FLASH SAVE 2      'Save this program to the flash location 2"
Data "> OPTION F9 \qFLASH RUN 2\q+Chr$(13) 'Runs flash 2 when a F9 pressed."
Data ""
'================================================
Clear
Option ESCAPE
Option default integer
MODE 1:Font 1
'------------------------------------------------
Const FW=MM.Info(FONTWIDTH):FH=MM.Info(FONTHEIGHT)
Const CHR=MM.HRES\FW:CVR=MM.VRES\FH
'------------------------------------------------
'Global Configuration
Const DFColor=0  'Dirs & Files Color (0=Colorer ON)
Const SelColor=14'Color of selected files
Const SStimeout=60000
Const SOMAX=8   'Max number of Sort Order options
Const RMAX=199  'Max number of Records in File List (255 max)
'------------------------------------------------
Dim integer mpIndx
Dim integer CKey,Tick1s,i
Dim integer PSide=1'Current Panel Side (Left=1,Right=0)
Dim integer LIndx,RIndx'Current File List Index
Dim integer c(15)=(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15)
Dim integer FLS(7,1)=(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)
'File List Struct - FLS(Parameter,Side) Side=[1-Left/0-Right]
'RecQt,DirQt,FileQt,SelQt,SortOrd,FColor,cPos,cTop
'------------------------------------------------
Dim string  TMP$
Dim string  LDisk$="A:"    LENGTH 2
Dim string  RDisk$="A:"    LENGTH 2
Dim string  LDir$="/"      LENGTH 127
Dim string  RDir$="/"      LENGTH 127
Dim string  LFLAtr$        LENGTH 255
Dim string  RFLAtr$        LENGTH 255
Dim string  LFList$(RMAX)  LENGTH 63
Dim string  RFList$(RMAX)  LENGTH 63
'================================================
BEGIN:
Colour Map  c(),c()
FLS(4,1)=1:FLS(4,0)=1
FLS(5,1)=DFColor:FLS(5,0)=DFColor
'------------------------------------------------
Timer =0
SetTick 1000,ISR_Tick1s,1
If MM.Info(SDCARD)="Ready" Then RDisk$="B:"
PanelsReStart()
'================================================
' MAIN CONTROL PANEL FUNCTIONS
'================================================
Do
GetCKey
Select Case CKey
Case 0'No Key Pressed
Case 9'TAP
  SetPControl("TAB")
Case 10,13'ENTER'
  EnterControl
Case 27'ESCAPE
  W_QUIT
  PanelsReStore
Case 32,132'SPACE'Ins
  SetPControl("SEL")
Case 42'PrtScr
  Save Image "PrtScr"
Case 65 To 90,97 To 122
  Color c(7),c(0):Print @(0,38*FH);
  EnterCommandLine(Chr$(CKey))
  PanelsReStore
Case 96'~
  SortSwitch
Case 128'Up
  SetPControl("UP")
Case 129'Dn
  SetPControl("DWN")
Case 130'Left
  SetPControl("PREV")
Case 131'Right
  SetPControl("NEXT")
Case 134'Home
  SetPControl("TOP")
Case 135'End
  SetPControl("BOT")
Case 136'PgUp
  SetPControl("PREV")
Case 137'PgDn
  SetPControl("NEXT")
Case 139,8'Alt
  SetAltPControl
Case 145'F1 Help
  W_F1_Help
  PanelsReStore
Case 146'F2 Menu
  W_F2_Menu
  PanelsReStart
Case 147'F3 View
  W_F3_View
Case 148'F4 Edit
  SetPControl("DIS")
  W_F4_Edit
  PanelsReStart
Case 149'F5 Copy
  SetPControl("DIS")
  W_F5_Copy
  PanelsReStart
Case 150'F6 RenMov
  SetPControl("DIS")
  W_F6_RenMOVE
  PanelsReStart
Case 151'F7 MkDir
  SetPControl("DIS")
  W_F7_MkDIR
  PanelsReStart
Case 152'F8 Delete
  SetPControl("DIS")
  W_F8_DELETE
  PanelsReStart
Case 153'F9 Menu
  W_F9_USER_COMMANDS
  PanelsReStart
Case 154'F10 Exit
  CLS : End
Case 158,19'Pause
  Play Stop
Case Else
  'Print @(0,38*FH)CKey;"  ";'For Test
End Select
If CKey Then Timer =0
'----------------------
If Tick1s Then PrintTime
If Timer>SStimeout Then
  SSMatrix
  CLS :Timer =0
  PanelsReStore
EndIf
Loop
'ALT CONTROL PANEL FUNCTIONS --------------------
Sub  SetAltPControl
PrintFKeyMenu("DATA_FKeyAlt1")
Do :GetCKey: Loop Until CKey
Select Case CKey
Case 145'F1 L.Disk
  SetPControl("DIS")
  WDiskSelect(1,2,15):LDir$="/"
  LPanelShow():SetPControl("ENA")
Case 146'F2 R.Disk
  SetPControl("DIS")
  WDiskSelect(0,42,15):RDir$="/"
  RPanelShow():SetPControl("ENA")
Case 149'F5 XSEND
  CLS
  End "?\qXMODEM -->\q: XMODEM SEND "+"\q"+GetCurrFileName$()+"\q"
Case 150'F6 XREVIVE
  CLS
  End "?\qXMODEM <--\q: XMODEM RECIVE "+"\q"+GetCurrFileName$()+"\q"
End Select
PrintFKeyMenu("DATA_FKeyMain")
End Sub
'================================================
' SET PANELS CONTROL
'================================================
Sub SetPControl(CTRL$)
Local SIZE
If PSide Then
'---- Left Panel Control --------
Select Case CTRL$
Case "TAB"
  PSide=0
  SetPControl("DIS")
  SetPControl("ENA")
Case "SEL"
  SelectingFile(LFLAtr$,LIndx)
  SetPControl("DWN")
  If FLS(3,1) Then
    Print @(24*FW,34*FH,2)"Selected:";FLS(3,1);" ";
  Else
    Print @(24*FW,34*FH)String$(14,196);
  EndIf
Case "UP"
  PFNAPrint(1)
  If LIndx>0 Then
    Inc LIndx,-1
    If FLS(6,1)>0 Then
      Inc FLS(6,1),-1
    Else
      If FLS(7,1)>0 Then Inc FLS(7,1),-1
      PFLPrint(1)
    EndIf
  EndIf
  PFNAPrint(1,2,7)
Case "DWN"
  PFNAPrint(1)
  If LIndx<FLS(0,1) Then
    Inc LIndx
    If FLS(6,1)<32 Then
      Inc FLS(6,1)
    Else
      Inc FLS(7,1)
      PFLPrint(1)
    EndIf
  EndIf
  PFNAPrint(1,2,7)
Case "TOP"
  LIndx=0:FLS(7,1)=0:FLS(6,1)=0
  PFLPrint(1)
  PFNAPrint(1,2,7)
Case "BOT"
  LIndx=FLS(0,1)
  If FLS(0,1)>32 Then
    FLS(6,1)=32:FLS(7,1)=FLS(0,1)-32
  Else
    FLS(6,1)=FLS(0,1)-FLS(7,1)
  EndIf
  PFLPrint(1)
  PFNAPrint(1,2,7)
Case "PREV"
  If LIndx>=32  And FLS(7,1)>=32 Then
    Inc FLS(7,1),-32:Inc LIndx,-32
  Else
    LIndx=0:FLS(7,1)=0:FLS(6,1)=0
  EndIf
  PFLPrint(1)
  PFNAPrint(1,2,7)
Case "NEXT"
  If (LIndx-FLS(6,1))+32<FLS(0,1) Then
    Inc FLS(7,1),32:Inc LIndx,32
    If FLS(7,1)+FLS(6,1)>FLS(0,1) Then LIndx=FLS(0,1):FLS(6,1)=LIndx-FLS(7,1)
  Else
    LIndx=FLS(0,1):FLS(6,1)=FLS(0,1)-FLS(7,1)
  EndIf
  PFLPrint(1)
  PFNAPrint(1,2,7)
End Select
'---- Right Panel Control -------
Else
Select Case CTRL$
Case "TAB"
  PSide=1
  SetPControl("DIS")
  SetPControl("ENA")
Case "SEL"
  SelectingFile(RFLAtr$,RIndx)
  SetPControl("DWN")
  If FLS(3,0) Then
    Print @(64*FW,34*FH,2)"Selected:";FLS(3,0);" ";
  Else
    Print @(64*FW,34*FH)String$(14,196);
  EndIf
Case "UP"
  PFNAPrint(0)
  If RIndx>0 Then
    Inc RIndx,-1
    If FLS(6,0)>0 Then
      Inc FLS(6,0),-1
    Else
      If FLS(7,0)>0 Then Inc FLS(7,0),-1
      PFLPrint(0)
    EndIf
  EndIf
  PFNAPrint(0,2,7)
Case "DWN"
  PFNAPrint(0)
  If RIndx<FLS(0,0) Then
    Inc RIndx
    If FLS(6,0)<32 Then
      Inc FLS(6,0)
    Else
      Inc FLS(7,0)
      PFLPrint(0)
    EndIf
  EndIf
  PFNAPrint(0,2,7)
Case "TOP"
  RIndx=0:FLS(7,0)=0:FLS(6,0)=0
  PFLPrint(0)
  PFNAPrint(0,2,7)
Case "BOT"
  RIndx=FLS(0,0)
  If FLS(0,0)>32 Then
    FLS(6,0)=32:FLS(7,0)=FLS(0,0)-32
  Else
    FLS(6,0)=FLS(0,0)-FLS(7,0)
  EndIf
  PFLPrint(0)
  PFNAPrint(0,2,7)
Case "PREV"
  If RIndx>=32 And FLS(7,0)>=32 Then
    Inc FLS(7,0),-32:Inc RIndx,-32
  Else
    RIndx=0:FLS(7,0)=0:FLS(6,0)=0
  EndIf
  PFLPrint(0)
  PFNAPrint(0,2,7)
Case "NEXT"
  If (RIndx-FLS(6,0))+32<FLS(0,0) Then
    Inc FLS(7,0),32:Inc RIndx,32
    If FLS(7,0)+FLS(6,0)>FLS(0,0) Then RIndx=FLS(0,0):FLS(6,0)=RIndx-FLS(7,0)
  Else
    RIndx=FLS(0,0):FLS(6,0)=FLS(0,0)-FLS(7,0)
  EndIf
  PFLPrint(0)
  PFNAPrint(0,2,7)
End Select
EndIf
'---- Common Actions --------
Select Case CTRL$
Case "DIS"
  PFNAPrint(1)
  PFNAPrint(0)
Case "ENA"
  If PSide Then
    Drive LDisk$
    Chdir LDir$
    PFNAPrint(1,2,7)
  Else
    Drive RDisk$
    Chdir RDir$
    PFNAPrint(0,2,7)
  EndIf
End Select
'Print Command Line ---------
PrintComLine(">")
'Print Panels Info ----------
If PSide Then
  WClear(1,35,38,2)
  Print @(FW,36*FH);
  If LIndx Then
    TMP$=LFList$(LIndx)
    If Mid$(TMP$,1,1)="/" Then
      Print " <DIR>"
    Else
      Print "Size:";MM.Info(FILESIZE TMP$)
    EndIf
    Print @(20*FW,36*FH);MM.Info(MODIFIED TMP$)
    If Len(TMP$)<39 Then
      TMP$=Mid$(TMP$,1,38)
      TMP$=TMP$+Space$(38-Len(TMP$))
    Else
      TMP$=Mid$(TMP$,1,37)+"~"
    EndIf
    Print @(FW,35*FH);TMP$;
  Else
    Print " <..>"
  EndIf
Else
  WClear(41,35,38,2)
  Print @(41*FW,36*FH);
  If RIndx Then
    TMP$=RFList$(RIndx)
    If Mid$(TMP$,1,1)="/" Then
      Print " <DIR>"
    Else
      Print "Size:";MM.Info(FILESIZE TMP$)
    EndIf
    Print @(60*FW,36*FH);MM.Info(MODIFIED TMP$)
    If Len(TMP$)<39 Then
      TMP$=Mid$(TMP$,1,38)
      TMP$=TMP$+Space$(38-Len(TMP$))
    Else
      TMP$=Mid$(TMP$,1,37)+"~"
    EndIf
    Print @(41*FW,35*FH);TMP$;
  Else
    Print " <..>"
  EndIf
EndIf
End Sub
'================================================
' ENTER KEY CONTROLS
'================================================
Sub EnterControl
Local prevDir$ Length 63'***

If PSide Then TMP$=LFList$(LIndx) Else TMP$=RFList$(RIndx)
'Change Directory
If TMP$=".." Or Left$(TMP$,1)="/" Then
  If TMP$=".." Then
    Chdir ".."
    If PSide Then TMP$=LDir$ Else TMP$=RDir$'***
    TMP$=Left$(TMP$,Len(TMP$)-1)
    i=Len(Cwd$)-2: If i>1 Then Inc i
    prevDir$=Mid$(TMP$,i,255)
  Else
    Chdir Mid$(TMP$,2,63)
  EndIf
  TMP$=Cwd$:TMP$=Mid$(TMP$,3,63)
  If Len(TMP$)>1 Then TMP$=TMP$+"/"
  If PSide Then
    LDir$=TMP$
    LIndx=0:FLS(6,1)=0:FLS(7,1)=0
    GetFList(LDisk$,LDir$,LFList$(),LFLAtr$,1)
    FColorer(LFList$(),LFLAtr$,FLS(5,1))
    If prevDir$<>"" Then LIndx=IsInFList(1,prevDir$):FLS(6,1)=LIndx
    If LIndx>32 Then FLS(6,1)=LIndx Mod 32: FLS(7,1)=(LIndx\32)*32
    LPrintPanel
  Else
    RDir$=TMP$
    RIndx=0:FLS(6,0)=0:FLS(7,0)=0
    GetFList(RDisk$,RDir$,RFList$(),RFLAtr$,0)
    FColorer(RFList$(),RFLAtr$,FLS(5,0))
    If prevDir$<>"" Then RIndx=IsInFList(0,prevDir$):FLS(6,0)=RIndx
    If RIndx>32 Then FLS(6,0)=RIndx Mod 32: FLS(7,0)=(RIndx\32)*32
    RPrintPanel
  EndIf
  SetPControl("ENA")
  Exit Sub
EndIf
'Action with files
Select Case LCase$(Right$(TMP$,4))
Case ".bas"
  Run TMP$
End Select
W_F3_View
End Sub
'-----------------------
Sub EnterCommandLine(CMD$)
  Print Cwd$;">";
  CMD$=InputE$(CMD$,79-Len(Cwd$))
  If CMD$="" Then Exit Sub
  CLS : End CMD$
End Sub
'================================================
' FUNCTION KEY WINDOWS
'================================================
Sub W_F1_Help
Local integer i,x=2*FW,y=2*FH
Color c(15),c(1)
OpenWindow("Help",0,0,80,40)
Restore DATA_HELP
Do
  Read TMP$
  If TMP$="" Then Exit Do
  Print @(x,y+FH*i)TMP$
  Inc y,FH
Loop
Color 0,c(7): Print @(74*FW,2*FH);"    "
Color 0,c(14):Print @(74*FW,3*FH);"    "
Do :Loop Until Inkey$<>""
Color c(15),c(0)
End Sub
'-----------------------
Sub W_F2_Menu
'Local string TMP$
Local integer x,n,i=1
If PSide Then x=1 Else x=41
WClear(x,1,38,33)
PrintFKeyMenu("DATA_FKeyZero")
Color c(7),c(0)
Restore DATA_MENU_COMMANDS
For n=1 To 33
  Read TMP$
  If TMP$="" Then Inc n,-1: Exit For
  Print @(x*FW,n*FH);"> "+Mid$(TMP$,1,38)
Next
Do
If CKey Then
  TMP$="> "+GetRecordFromData$("DATA_MENU_COMMANDS",i)
  Print @(x*FW,i*FH,2)TMP$;Space$(38-Len(TMP$));
EndIf
CKey=Asc(Inkey$)
Select Case CKey
Case 0 'NOP
Case 10,13'ENTER
  TMP$=GetRecordFromData$("DATA_MENU_COMMANDS",i)
  If TMP$=".." Then Exit Sub
  Call TMP$
  Exit Do
Case 27'ESC
  Exit Sub
Case 128'UP
  Print @(x*FW,i*FH)TMP$;Space$(38-Len(TMP$));
  If i>1  Then Inc i,-1
Case 129'DN
  Print @(x*FW,i*FH)TMP$;Space$(38-Len(TMP$));
  If i<n Then Inc i,1
Case Else
  Exit Sub
End Select
Loop
End Sub
'-----------------------
Sub W_F3_View
TMP$=Choice(PSide,LFList$(LIndx),RFList$(RIndx))
On ERROR ignore
Select Case LCase$(Right$(TMP$,4))
Case ".bmp",".jpg",".png"
  ImageViewer(TMP$)
Case ".mod",".mp3",".wav","flac"
  MusicPlayer("START")
Case Else
  CLS : List TMP$
  Do : Loop While Inkey$=""
  PanelsReStart
  SetPControl("ENA")
End Select
W_ERROR_MSG : If MM.Errno Then On ERROR CLEAR:PanelsReStart
End Sub
'----------------------------
Sub MusicPlayer(command$)
Local  B%
Select Case command$
Case "STOP"
  Play Stop: Exit Sub
Case "START"
  mpIndx=0:Play Stop
  mpIndx=Choice(PSide,LIndx,RIndx)
  If FLS(3,mpSide) Then
    Inc mpIndx,-1
    MusicPlayer("NEXT")
    Exit Sub
  EndIf
Case "NEXT"
  If FLS(3,PSide) Then
    Do
      Inc mpIndx :If mpIndx>255 Then mpIndx=1
      B=Choice(PSide,Peek(VAR LFLAtr$,mpIndx),Peek(VAR RFLAtr$,mpIndx))
    Loop Until B And 128
  Else
    Exit Sub
  EndIf
End Select
command$=Choice(PSide,LFList$(mpIndx),RFList$(mpIndx))
'------Play Music File
On ERROR ignore
Select Case LCase$(Right$(command$,4))
Case "flac"
  Play FLAC command$,ISR_MPlayer
Case ".mod"
  Play MODFILE command$,ISR_MPlayer
Case ".wav"
  Play WAV command$,ISR_MPlayer
Case ".mp3"
  Play MP3 command$,ISR_MPlayer
End Select
W_ERROR_MSG : If MM.Errno Then On ERROR CLEAR:PanelsReStart
End Sub

Sub ISR_MPlayer
  If mpIndx<>0 Then MusicPlayer("NEXT")
End Sub
'----------------------------
Sub ImageViewer(fname$)
If Instr(MM.DEVICE$,"RP2350") Then MODE 3 Else MODE 1
On ERROR ignore
Select Case LCase$(Mid$(fname$,Len(fname$)-3,4))
Case ".bmp"
  Load IMAGE fname$
Case ".jpg"
  Load JPG fname$
Case ".png"
  Load PNG fname$
End Select
If MM.Errno Then
  MODE 1: W_ERROR_MSG : If MM.Errno Then On ERROR CLEAR
Else
  Do : Loop While Inkey$=""
  MODE 1
EndIf
PanelsReStart
SetPControl("ENA")
End Sub
'----------------------
Sub W_F4_Edit
TMP$=GetCurrFileName$()
If TMP$=".." Then
  Color c(15),c(1)
  OpenWindow("EDIT New File",4,14,72,7)
  Print @(6*FW,16*FH);GetCurrFullPath$();
  Print @(8*FW,18*FH);">"
  Color c(14),c(0):Print @(9*FW,18*FH);Space$(64);
  Print @(9*FW,18*FH);:TMP$=InputE$("",64)
  Color c(15),c(0)
  If TMP$<>"" Then Edit File TEMP$
Else
  If Mid$(TMP$,1,1)="/" Then Exit Sub
  Edit File GetCurrFileName$()
EndIf
End Sub
'-----------------------
Sub W_F5_Copy
Local FLAG%=1
Do
If FLS(3,PSide) Then 'Multi Select
   FLS(3,PSide)=FLS(3,PSide)-1
   FLAG=0
 If PSide Then
   LIndx=FindAndClearFlagInFLAtr(LFLAtr$,FLS(0,1),128)
 Else
   RIndx=FindAndClearFlagInFLAtr(RFLAtr$,FLS(0,0),128)
 EndIf
EndIf
Color c(15),c(1)
OpenWindow("Copy file:",4,13,72,13)
Chdir GetCurrFullPath$()
Print @(6*FW,16*FH);GetCurrFullPath$();
Print @(6*FW,18*FH);"to";
Print @(6*FW,20*FH);GetFullPath$(OpSide());
Print @(7*FW,21*FH);">";
Color c(15),c(0)
Print @(9*FW,17*FH);Space$(64);
Print @(9*FW,21*FH);Space$(64);
TMP$=GetCurrFileName$()
Print @(9*FW,17*FH);TMP$;
Print @(9*FW,21*FH);TMP$;
Color c(14),c(0):Print @(9*FW,21*FH);
If FLAG And 1 Then TMP$=InputE$(GetCurrFileName$(),64)
Color c(15),c(0)
If IsInFList(OpSide(),TMP$) Then
  Color c(15),c(8)
  Print @(9*FW,23*FH)" Warning! File already exists. To Overwrite, press [ENTER]"
  Color c(15),c(0)
  Do
    CKey=Asc(Inkey$)
    If CKey=10 Or CKey=13 Then Exit Do
    If CKey>0  Then Exit Sub
  Loop
EndIf
If TMP$<>"" Then
  If Mid$(TMP$,1,1)="/" Then
    CopyDir(GetFullPath$(PSide)+TMP$,GetFullPath$(OPSide())+TMP$)
  Else
    TMP$=GetFullPath$(OpSide())+TMP$
    On ERROR ignore
    Copy GetCurrFileName$() To TMP$
    W_ERROR_MSG : If MM.Errno Then On ERROR CLEAR
  EndIf
EndIf
Loop While FLS(3,PSide)
End Sub
'-----------------------
Sub W_F6_RenMOVE
Local FLAG%=1
Do
If FLS(3,PSide) Then 'Multi Select
   FLS(3,PSide)=FLS(3,PSide)-1
   FLAG=0
 If PSide Then
   LIndx=FindAndClearFlagInFLAtr(LFLAtr$,FLS(0,1),128)
 Else
   RIndx=FindAndClearFlagInFLAtr(RFLAtr$,FLS(0,0),128)
 EndIf
EndIf
Color c(15),c(1)
OpenWindow("Rename/Move:",4,13,72,13)
Print @(6*FW,16*FH);GetCurrFullPath$();
Print @(6*FW,18*FH);"to";
Print @(6*FW,20*FH);GetFullPath$(OpSide());
Print @(7*FW,21*FH);">";
Color c(15),c(0)
Print @(9*FW,17*FH);Space$(64);
Print @(9*FW,21*FH);Space$(64);
TMP$=GetCurrFileName$()
Print @(9*FW,17*FH);TMP$;
Print @(9*FW,21*FH);TMP$;
Color c(14),c(0):Print @(9*FW,21*FH);
If FLAG And 1 Then TMP$=InputE$(GetCurrFileName$(),64)
Color c(15),c(0)
If IsInFList(OpSide(),TMP$) Then
  Color c(15),c(8)
  Print @(9*FW,23*FH) " Warning! File already exists. To Overwrite, press [ENTER]"
  Color c(15),c(0)
  Do
    CKey=Asc(Inkey$)
    If CKey=10 Or CKey=13 Then Exit Do
    If CKey>0  Then Exit Sub
  Loop
EndIf
If TMP$<>"" Then
  If Mid$(TMP$,1,1)="/" Then
    CopyDir(GetFullPath$(PSide)+TMP$,GetFullPath$(OPSide())+TMP$)
    DeleteDir(GetFullPath$(PSide)+TMP$)
  Else
    TMP$=GetFullPath$(OpSide())+TMP$
    On ERROR ignore
    Copy GetCurrFullPathFile$() To TMP$
    If Not MM.Errno Then
      Kill GetCurrFullPathFile$()
    EndIf
    W_ERROR_MSG : If MM.Errno Then On ERROR CLEAR
  EndIf
EndIf
Loop While FLS(3,PSide)
End Sub
'----------------------
Sub W_F7_MkDIR
  Color c(15),c(1)
  OpenWindow("Make a Directory:",4,14,72,7)
  TMP$=GetCurrFullPath$()
  Chdir TMP$
  Print @(6*FW,16*FH);TMP$
  Print @(7*FW,18*FH);"> ";
  Color c(14),c(0):Print Space$(64);
  Print @(9*FW,18*FH);: TMP$=InputE$("",64)
  Color c(15),c(0)
  If TMP$<>"" Then Mkdir TMP$
End Sub
'----------------------
Sub W_F8_DELETE
Local FLAG%=1
Do
If FLS(3,PSide) Then 'Multi Select
   FLS(3,PSide)=FLS(3,PSide)-1
   FLAG=0
   If PSide Then
    LIndx=FindAndClearFlagInFLAtr(LFLAtr$,FLS(0,1),128)
   Else
    RIndx=FindAndClearFlagInFLAtr(RFLAtr$,FLS(0,0),128)
   EndIf
EndIf
  Color c(15),c(1)
  OpenWindow("Delete:",4,14,72,7)
  Chdir GetCurrFullPath$()
  Print @(6*FW,16*FH);GetCurrFullPath$();
  Print @(7*FW,18*FH);">";
  Color c(8),c(0):Print @(9*FW,18*FH);Space$(64);
  Print @(9*FW,18*FH);GetCurrFileName$();
  Color c(15)
  If Flag And 1 Then
    Do
      CKey=Asc(Inkey$)
      If CKey=10 Or CKey=13 Then Exit Do
      If CKey>0 Then Exit Sub
    Loop
  EndIf
  On ERROR ignore
  If Mid$(GetCurrFileName$(),1,1)="/" Then
    DeleteDir(GetCurrFullPathFile$())
  Else
    Kill GetCurrFileName$()
    W_ERROR_MSG : If MM.Errno Then On ERROR CLEAR
  EndIf
Loop While FLS(3,PSide)
End Sub
'----------------------
Sub W_F9_USER_COMMANDS
Local i,n
Do
PrintFKeyMenu("DATA_FKeyAlt2")
Color c(7),0
OpenWindow("User Commands Menu",0,0,80,38)
Restore DATA_USER_COMMANDS
For n=1 To 36
  Read TMP$
  If TMP$="" Then Inc n,-1: Exit For
  Print @(FW,n*FH);Mid$(TMP$,1,80);
Next
i=1
Do
If CKey Then
  TMP$=GetRecordFromData$("DATA_USER_COMMANDS",i)
  Print @(FW,i*FH,2)TMP$;Space$(78-Len(TMP$));
  PrintComLine(">"+TMP$)
  Color c(7)
EndIf
CKey=Asc(Inkey$)
Select Case CKey
Case 0 'NOP
Case 13'ENTER
  If TMP$=".." Then Exit Sub
  If Mid$(TMP$,1,2)="> " Then
    CLS
    End Mid$(TMP$,3,Len(TMP$))
  Else
    CLS : Color c(7)
    Execute TMP$
    Do : Loop While Inkey$=""
    Exit Do
  EndIf
Case 27'ESC
  Exit Sub
Case 128'UP
  Print @(FW,i*FH)TMP$;Space$(78-Len(TMP$));
  If i>1  Then Inc i,-1
Case 129'DN
  Print @(FW,i*FH)TMP$;Space$(78-Len(TMP$));
  If i<n Then Inc i,1
Case 145'F1
  End "Files"
Case 154'F10
  Exit Sub
End Select
Loop :Loop
End Sub
'----------------------
Sub W_QUIT
Color c(15),c(2)
OpenWindow(" Quit ",25,14,30,7)
WHBar(25,18,30)
Print @(28*FW,16*FH);"Do you want to quit FM ?";
Print @(28*FW,19*FH);"'Yes'- Press [ENTER] Key";
Color c(15),c(0)
Do
  Select Case Asc(Inkey$)
  Case 0
  Case 10,13,154'ENTER'
    CLS : End
  Case Else
    Exit Do
  End Select
Loop
End Sub

'WINDOW DISCK SELECT -------------------
Sub WDiskSelect(SIDE%,x%,y%)
Local i,N
Local DSK$(1) LENGTH 39
Color c(15),c(1):OpenWindow("DISK",x,y,36,6)
Inc x
Inc y:Print @(x*FW,y*FH)"    Drive   |   Size   |   Free   "
Inc y:Print @(x*FW,y*FH)"------------+----------+----------"
Drive "A:"
TMP$=" A: FlashFS |"
TMP$=TMP$+Str$(MM.Info(DISK SIZE)\1024,9)+" |"
TMP$=TMP$+Str$(MM.Info(FREE SPACE)\1024,9)+" "
DSK$(0)=TMP$
On ERROR SKIP 1
If MM.Info(SDCARD)="Ready" Then Inc N
On ERROR CLEAR
If N>0 Then
  Drive "B:"
  TMP$=" B: SD Card |"
  TMP$=TMP$+Str$(MM.Info(DISK SIZE)\1024,9)+" |"
  TMP$=TMP$+Str$(MM.Info(FREE SPACE)\1024,9)+" "
Else
  TMP$=" B: SD Card |   NO Disk Drive !   "
EndIf
DSK$(1)=TMP$
Inc y
Print @(x*FW,(y+0)*FH,2);DSK$(0);
Print @(x*FW,(y+1)*FH,0);DSK$(1);
Do
CKey=Asc(Inkey$)
Select Case CKey
Case 0
Case 10,13
  Color c(15),0
  Exit Do
Case 27
  Color c(15),0
  Exit Sub
Case Else
  Print @(x*FW,(y+i)*FH,0);DSK$(i);
  Inc i: If i>N Then i=0
  Print @(x*FW,(y+i)*FH,2);DSK$(i);
End Select
Loop
If i=0 Then TMP$="A:" Else TMP$="B:"
If SIDE Then LDisk$=TMP$ Else RDisk$=TMP$
PSide=SIDE
End Sub
'Window ERROR MSG -----------
Sub W_ERROR_MSG
Local integer x,l
If MM.Errno Then
  Color c(15),c(8)
  l=Len(MM.ErrMsg$)+4: x=40-(l\2)
  OpenWindow("ERROR:",x,24,l,6)
  Print @((x+2)*FW,26*FH);
  Print MM.ErrMsg$;
  Color c(15),c(0)
  Do : Loop While Inkey$=""
EndIf
On ERROR ABORT
End Sub
'================================================
' PANEL WORKING SUBROUTINS
'================================================
Sub GetCKey
Static AltFlag
    CKey=Asc(Inkey$)
    On ERROR SKIP
    If KeyDown(7) And &H11 Then Inc AltFlag Else AltFlag=0
    If MM.Errno Then On ERROR CLEAR
    If AltFlag=1 Then CKey=139'ALT
End Sub
'----------------------
Sub SortSwitch
Local integer x,SS,TR=1
If PSide Then SS=FLS(4,1) Else SS=FLS(4,0)
Inc SS: If SS>SOMAX Then SS=0
Timer =0
Do :If Timer>1000 Then Exit Do
If TR Then
  If PSide Then x=2 Else x=42
  Print @(x*FW,34*FH,2)GetRecordFromData$("DATA_SORT",SS+1);
  TR=0
EndIf
Select Case Asc(Inkey$)
Case 128,131,96'Up
  Timer =0: TR=1: Inc SS, 1: If SS>SOMAX Then SS=0
Case 129,130'Dn
  Timer =0: TR=1: Inc SS,-1: If SS<0 Then SS=SOMAX
End Select
Loop
If PSide Then
  FLS(4,1)=SS:LPanelShow:SetPControl("ENA")
Else
  FLS(4,0)=SS:RPanelShow:SetPControl("ENA")
EndIf
End Sub
'----------------------
Sub SelectingFile(FLAtr$,Indx)
If Indx=0 Then Exit Sub
Local N%,AT%
  AT=Peek(VAR FLAtr$,Indx)
  AT=AT Xor 128
  Poke VAR FLAtr$,Indx,AT
  N=FLS(3,PSide)
  If AT And 128 Then Inc N Else Inc N,-1
  FLS(3,PSide)=N
End Sub
'------------------------------------------------
'Panels Interface ReWrite
Sub PanelsReStart
  PrintFKeyMenu("DATA_FKeyMain")
  LPanelShow
  RPanelShow
  SetPControl("ENA")
End Sub
'Panels Interface Restore
Sub PanelsReStore
  PrintFKeyMenu("DATA_FKeyMain")
  LPrintPanel
  RPrintPanel
  SetPControl("ENA")
End Sub
'----------------------LPanel
Sub LPanelShow
  'SetPControl("DIS")
  LIndx=0:FLS(6,1)=0:FLS(7,1)=0
  GetFList(LDisk$,LDir$,LFList$(),LFLAtr$,1)
  FColorer(LFList$(),LFLAtr$,FLS(5,1))
  LPrintPanel
End Sub
Sub LPrintPanel
  OpenWindow(GetTrimPath$(1,36),0,0,40,38)
  WHBar(0,34,40)
  PFLPrint(1)
  Print @(2*FW,34*FH,2)GetRecordFromData$("DATA_SORT",FLS(4,1)+1);
  Print @(8*FW,37*FH,2)"Folders:";FLS(1,1);", Files:";FLS(2,1);
End Sub
'----------------------RPanel
Sub RPanelShow
  'SetPControl("DIS")
  RIndx=0:FLS(6,0)=0:FLS(7,0)=0
  GetFList(RDisk$,RDir$,RFList$(),RFLAtr$,0)
  FColorer(RFList$(),RFLAtr$,FLS(5,0))
  RPrintPanel
End Sub
Sub RPrintPanel
  OpenWindow(GetTrimPath$(0,36),40,0,40,38)
  WHBar(40,34,40)
  PFLPrint(0)
  Print @(42*FW,34*FH,2)GetRecordFromData$("DATA_SORT",FLS(4,0)+1);
  Print @(48*FW,37*FH,2)"Folders:";FLS(1,0);", Files:";FLS(2,0);
End Sub
'============================
Sub OpenWindow(Titl$,xc,yc,wc,hc)
  WFrame(xc,yc,wc,hc)
  Print @((xc+2)*FW,yc*FH,2)Titl$;
End Sub
'----------------------
Sub WClear(xc,yc,wc,hc)
Local integer x=xc*FW,y=yc*FH
  For y=yc*FH To (yc+hc-1)*FH Step FH
    Print @(x,y) Space$(wc);
  Next
End Sub
'----------------------
Sub WFrame(xc,yc,wc,hc)
Local integer y,x=xc*FW
  Print @(x,yc*FH)Chr$(201);String$(wc-2,205);Chr$(187);
  For y=(yc+1)*FH To (yc+hc-2)*FH Step FH
    Print @(x,y)Chr$(186);Space$(wc-2);Chr$(186);
  Next
  Print @(x,y)Chr$(200);String$(wc-2,205);Chr$(188);
End Sub
'----------------------
Sub WHBar(xc,yc,wc)
  Print @(xc*FW,yc*FH)Chr$(199);String$(wc-2,196);Chr$(182);
End Sub
'============================
'PRINT FILE LIST ON PANEL
Sub PFLPrint(SIDE)
If SIDE Then
  FLPrint(LFList$(),LFLAtr$,FLS(7,1),1,1,38,32)
Else
  FLPrint(RFList$(),RFLAtr$,FLS(7,0),41,1,38,32)
EndIf
End Sub
'----------------------------
Sub FLPrint(FList$(),FLAtr$,top,xc,yc,wc,hc)
Local integer y,Indx,FC
Indx=top
For y=yc To yc+hc
  Print @(xc*FW,y*FH);
  TMP$=FList$(Indx)
  If TMP$<>"" Then
    TMP$=Left$(TMP$,wc)
    TMP$=TMP$+Space$(wc-Len(TMP$))
    FC=Peek(VAR FLAtr$,Indx)
    If FC And 128 Then FC=SelColor Else FC=FC And 15 'Selected
    If Indx Then Color c(FC)
    Print TMP$;
    Inc Indx
  Else
    Print Space$(wc);
  EndIf
Next
Color c(15)
End Sub
'PRINT FILE NAME WITH ATRIBUTE ON PANEL
Sub PFNAPrint(SIDE,M,BC)
If SIDE Then
  FNAPrint(LFList$(),LFLAtr$,LIndx,M,BC,1,FLS(6,1)+1,38)
Else
  FNAPrint(RFList$(),RFLAtr$,RIndx,M,BC,41,FLS(6,0)+1,38)
EndIf
End Sub
'----------------------------
Sub FNAPrint(FList$(),FLAtr$,Indx,M,BC,xc,yc,wc)
Local integer FC
  TMP$=Left$(FList$(Indx),wc)
  TMP$=TMP$+Space$(wc-Len(TMP$))
  If Indx Then
    FC=Peek(VAR FLAtr$,Indx)
    If FC And 128 Then FC=SelColor Else FC=FC And 15 'Selected
    If M Then
      If FC=BC Then Color c(BC),c(0) Else Color c(BC),c(FC)
    Else
      If FC=BC Then Color c(0),c(BC) Else Color c(FC),c(BC)
    EndIf
  Else
    If M Then Color c(BC),c(0) Else Color c(15),c(BC)
  EndIf
  Print @(xc*FW,yc*FH,M)TMP$;
  Color c(15),0
End Sub
'----------------------------
'PRINT COMMAND LINE
Sub PrintComLine(COM$)
Local String CL$
If PSide Then CL$=LDisk$+LDir$ Else CL$=RDisk$+RDir$
If Len(COM$)>77 Then COM$=Mid$(COM$,1,76)+"}"
CL$=CL$+COM$
If Len(CL$)>80 Then CL$=Mid$(CL$,1,78-Len(COM$))+"~/"+COM$
Color c(7)
Print @(0,38*FH);CL$+Space$(80-Len(CL$))
Color c(15)
End Sub
'----------------------------
'PRINT FUNCTION KEY MENU
Sub PrintFKeyMenu(LineLabel$)
Local i,N=9,x=0,y=39*FH
Color c(15),0:Print @(0,y);
For i=1 To N
  Print "F";Str$(i);
  If i<N Then Print Space$(7);
Next
x=2*FW:Color c(7),c(0)
Restore LineLabel$
For i=1 To n
 Read LineLabel$
 Print @(x,y,2)LineLabel$;
 Inc x,9*FW
Next
Color c(15),0
End Sub
'------------------------------------------------
'PRINT Watch Time
Sub PrintTime
  Tick1s=0
  Colour c(14),c(1)
  Print @(75*FW,0);Mid$(Time$,1,5);
  Colour c(15),c(0)
End Sub
'INTERRUP ROUTINES
Sub ISR_Tick1s
  Inc Tick1s
End Sub
'============================
'SCREEN SAVER - Matrix
Sub SSMatrix
Local matr(CHR),fade(CHR),clr,x
For x=1 To CHR:matr(x)=CVR*Rnd:fade(x)=&hF*Rnd:Next
Do
  For x=1 To CHR
    clr=&h1000*(fade(x)-&hF) And &hFF00
    Colour clr
    Print @(x*FW-FW,matr(x)*FH)Chr$(Rnd*223+32);
    If matr(x)>CVR  Then matr(x)=0 Else Inc matr(x)
    If fade(x)=&hF0 Then fade(x)=0 Else Inc fade(x)
  Next
  Pause 10
Loop While Inkey$=""
Colour c(15)
End Sub
'------------------------------------------------
Sub GetFList(Disk$,Folder$,FList$(),FLAtr$,SIDE%)
Local integer RQt,DQt,FQt,S,i
Select Case FLS(4,SIDE)
Case 3,4,7,8
Local string  FDate$(RMAX) LENGTH 19
Local integer SIndx%(RMAX)
Case 5,6
Local integer FSize%(RMAX)
Local integer SIndx%(RMAX)
End Select
Drive Disk$: Chdir Folder$
FList$(RQt)=".."
TMP$=Dir$("*",DIR)
Do While TMP$<>""
  If RQt=(RMax-1) Then Exit Do
  Inc RQt: Inc DQt
  FList$(RQt)="/"+TMP$
  Select Case FLS(4,SIDE)
  Case 3,4
    FDate$(RQt)=MM.Info(MODIFIED TMP$)
  Case 5,6
    FSize%(RQt)=-1
  Case 7,8
    FDate$(RQt)="*"
  End Select
  TMP$=Dir$()
Loop
TMP$=Dir$("*",FILE)
Do While TMP$<>""
  If RQt=(RMax-1) Then Exit Do
  Inc RQt: Inc FQt
  FList$(RQt)=TMP$
  Select Case FLS(4,SIDE)
  Case 3,4
    FDate$(RQt)=MM.Info(MODIFIED TMP$)
  Case 5,6
    FSize%(RQt)=MM.Info(FILESIZE TMP$)
  Case 7,8
    FDate$(RQt)=LCase$(Mid$(TMP$,Len(TMP$)-3,4))
  End Select
  TMP$=Dir$()
Loop
FList$(RQt+1)=""
FLS(0,SIDE)=RQt:FLS(1,SIDE)=DQt:FLS(2,SIDE)=FQt:FLS(3,SIDE)=0
If RQt=0 Then Exit Sub
S=FLS(4,SIDE)
If S=1 Then Sort FList$(),,2,1,RQt
If S=2 Then Sort FList$(),,3,1,RQt
If S=3 Then Sort FDate$(),SIndx%(),0,1,RQt
If S=4 Then Sort FDate$(),SIndx%(),1,1,RQt
If S=5 Then Sort FSize%(),SIndx%(),0,1,RQt
If S=6 Then Sort FSize%(),SIndx%(),1,1,RQt
If S=7 Then Sort FDate$(),SIndx%(),0,1,RQt
If S=8 Then Sort FDate$(),SIndx%(),1,1,RQt
If S>2 Then ArrangeByIndex(FList$(),SIndx%(),RQt):FList$(0)=".."
End Sub
'------------------------------------------------
Sub ArrangeByIndex(A$(),Ind%(),Size%)
Local i,Ci,Vi
For i=1 To Size%
  Vi=Ind%(i)
  If Vi=i Or Vi=0 Then Ind%(i)=0:Continue For
  A$(0)=A$(i): Ci=i
  Do
    Ind%(Ci)=0
    If Vi=i Then A$(Ci)=A$(0):Exit Do
    A$(Ci)=A$(Vi):Ci=Vi
    Vi=Ind%(Ci)
  Loop
Next
End Sub
'------------------------------------------------
Sub FColorer(FList$(),FLAtr$,FColor%)
Local i%=1,FC%=FColor And 15
If FC Then
  For i=1 To 255
    Poke VAR FLAtr$,i,FC
  Next
Else 'Colorer ON
  Do
  TMP$=FList$(i)
  If TMP$="" Then Exit Do
  If Left$(TMP$,1)="/" Then
    FC=15
  Else
    Select Case LCase$(Right$(TMP$,4))
    Case ".bas"
      FC=7
    Case ".zip",".dat"
      FC=8
    Case ".txt",".cfg",".opt"
      FC=12
    Case ".mp3",".mod",".wav","flac"
      FC=6
    Case ".bmp",".jpg",".png"
      FC=9
    Case Else
      FC=15
    End Select
  EndIf
  Poke VAR FLAtr$,i,FC
  Inc i
  Loop
  For i=i To 255
    Poke VAR FLAtr$,i,0
  Next
EndIf
End Sub
'------------------------------------------------
Sub DeleteDir(path$)
Local string file$, subdir$
On ERROR IGNORE
For i=0 To 2 'Check twice, why?
  file$=Dir$(path$+"/",FILE)
  Do While file$<>""
    Kill path$+"/"+file$
    file$=Dir$()
  Loop
Next
Delete_Do:
  subdir$=Dir$(path$+"/*",DIR)
  If subdir$="" Then GoTo Delete_Lend
  DeleteDir(path$+"/"+subdir$)
  GoTo Delete_Do '--loop--
Delete_Lend:
  Rmdir path$
  Chdir ".."
  W_ERROR_MSG : If MM.Errno Then On ERROR CLEAR
End Sub
'------------------------------------------------
Sub CopyDir(src$,dest$)
Local string file$ LENGTH 63
Drive Left$(dest$,2)
If Not MM.Info(EXISTS DIR dest$) Then On ERROR SKIP: Mkdir dest$
W_ERROR_MSG :If MM.Errno Then On ERROR CLEAR: Exit Sub
Drive Left$(src$,2): Chdir src$
'CopyDir Files
file$=Dir$(src$+"/*",FILE)
Do While file$<>""
  On ERROR SKIP
  Copy src$+"/"+file$ To dest$+"/"+file$
  W_ERROR_MSG :If MM.Errno Then On ERROR CLEAR: Exit Sub
  file$=Dir$()
Loop
'CopyDir Dirs
Local dCount=0,i=0
Local STRING subDir$=Dir$("*",DIR)
Do While subDir$<>""
  Inc dCount
  subDir$=Dir$()
Loop
If dCount>=1 Then
  Local sDirs$(dCount) LENGTH 63
  subDir$=Dir$("*",DIR)
  Do While subDir$<>""
    sDirs$(i)=subDir$
    Inc i
    subDir$=Dir$()
  Loop
  For i=0 To dCount-1
    CopyDir(src$+"/"+sDirs$(i), dest$+"/"+sDirs$(i))
  Next i
EndIf
End Sub
'================================================
' FUNCTIONS
'================================================
Function GetFullPath$(SIDE%) As string
If SIDE Then GetFullPath$=LDisk$+LDir$ Else GetFullPath$=RDisk$+RDir$
End Function
'----------------------
Function GetCurrFullPath$() As string
If PSide Then
  GetCurrFullPath$=LDisk$+LDir$
Else
  GetCurrFullPath$=RDisk$+RDir$
EndIf
End Function
'----------------------
Function GetCurrFileName$() As string
If PSide Then
  GetCurrFileName$=LFList$(LIndx)
Else
  GetCurrFileName$=RFList$(RIndx)
EndIf
End Function
'----------------------
Function GetCurrFullPathFile$() As string
If PSide Then
  GetCurrFullPathFile$=LDisk$+LDir$+LFList$(LIndx)
Else
  GetCurrFullPathFile$=RDisk$+RDir$+RFList$(RIndx)
EndIf
End Function
'----------------------
Function OpSide() As integer
If PSide Then OpSide=0 Else OpSide=1
End Function
'----------------------
Function GetTrimPath$(SIDE%,N%) As string
Local TEMP$
If SIDE Then TEMP$=LDisk$+LDir$ Else TEMP$=RDisk$+RDir$
If Len(TEMP$)<=N% Then
  GetTrimPath$=TEMP$
Else
  GetTrimPath$=Mid$(TEMP$,1,N%-1)+"~"
EndIf
End Function
'----------------------
Function IsInFList(SIDE%,FName$) As integer
If SIDE Then
  IsInFList=FindInFList(LFList$(),FName$)
Else
  IsInFList=FindInFList(RFList$(),FName$)
EndIf
End Function
'-----------------------
Function FindInFList(Flist$(),FName$) As integer
Local i=0
  Do
    If Flist$(i)="" Then i=0:Exit Do
    If Flist$(i)=Fname$ Then Exit Do
    Inc i
  Loop
  FindInFList=i
End Function
'-----------------------
Function FindAndClearFlagInFLAtr(FLAtr$,Deep%,Flag%) As integer
Local B%,n%=0
For i=1 To Deep
  B=Peek(VAR FLAtr$,i)
  If B And Flag Then
    Poke VAR FLAtr$,i,B Xor Flag
    n=i
    Exit For
  EndIf
Next
FindAndClearFlagInFLAtr=n
End Function
'-----------------------
Function GetRecordFromData$(DataLabel$,Ind%) As string
Local i
  Restore DataLabel$
  For i=1 To Ind%
    Read GetRecordFromData$
  Next
End Function

'INPUT Line EDITOR --------------------------------
Function InputE$(inString$,maxLen As integer)
Local string  s$=inString$,k$ length 1
Local integer tLen=Len(s$),cPos=tLen
Local integer x=MM.Info(hPos),y=MM.Info(vPos)
Local integer delFlg,printFlg=1,ovrOn=1
If maxLen=0 Then maxLen=tLen
Do
If printFlg Then
  Print @(x,y);s$;" ";
  If cPos<tLen Then
    Print @(x+cPos*MM.Info(FONTWIDTH),y,2)Mid$(s$,cPos+1,1);
    If delFlg Then delFlg=0
  Else
    Print @(x+cPos*MM.Info(FONTWIDTH),y,2)" ";
    If delFlg Then delFlg=0:Print " ";Chr$(8);
  EndIf
  Print Chr$(8);
EndIf
printFlg=1:k$=Inkey$
Select Case Asc(k$)
Case 0
  printFlg=0
Case 8'BSpace
  If tLen>0 And cPos>0 Then
    s$=Left$(s$,cPos -1) +Mid$(s$,cPos+1)
    Inc cPos,-1:Inc tLen,-1:delFlg=1
  EndIf
Case 10,13'ENTER
  InputE$=s$
  Exit Function
Case 27'ESC
  InputE$=""
  'maxLen=-1
  'InputE$=inString$
  Exit Function
Case 32 To 126
  If cPos<tLen And ovrOn Then
     s$=Left$(s$,cPos) +k$ +Mid$(s$,cPos+2)
     Inc cPos
  ElseIf tLen<maxLen Then
     s$=Left$(s$,cPos) +k$ +Mid$(s$,cPos+1)
     Inc cPos:Inc tLen
  EndIf
Case 127'DEL
  If cPos <tLen Then
    s$=Left$(s$,cPos) +Mid$(s$,cPos+2)
    Inc tLen,-1:delFlg=1
 EndIf
Case 130'LEFT
  If cPos >0 Then Inc cPos,-1
Case 131'RIGHT
  If cPos <tLen Then Inc cPos
Case 132'INSERT
  ovrOn=ovrOn=0
Case 134'HOME
  cPos =0
Case 135'END
  cPos =tLen
End Select
Loop
End Function

'================================================
' MENU PROGRAMMS
'================================================
Sub Set_Time_And_Date
Local integer i,y,x
Color c(15),c(1)
OpenWindow(" Set Time & Date ",14,10,52,12)
Print @(18*FW,12*FH);"Time (HH:MM:SS)"
Print @(18*FW,14*FH);"Date (DD-MM-YY)"
Print @(18*FW,16*FH);"RTC GET TIME"
Print @(18*FW,18*FH);"RTC SET TIME"
Do
x=36:y=i*2+12
If Timer>1000 Then Timer =0: CKey=1
If CKey Then
  Print @(x*FW,y*FH)"-->"
  Print @(42*FW,12*FH,2)Time$;" ";
  Print @(42*FW,14*FH,2)Date$;" ";
  Print " ";Day$(Date$);
  Print @(42*FW,16*FH,2)"Get I2C RTC Time ";
  Print @(42*FW,18*FH,2)"DD/MM/YYYY HH:MM ";
EndIf
CKey=Asc(Inkey$)
Select Case CKey
Case 0 'NOP
Case 13'ENTER
  Color c(14),c(0)
  Select Case i
  Case 0
    Print @(42*FW,12*FH);:TMP$=InputE$(Time$,9)
    On ERROR ignore: Time$=TMP$
  Case 1
    Print @(42*FW,14*FH);:TMP$=InputE$(Date$,19)
    On ERROR ignore: Date$=TMP$
  Case 2
    On ERROR ignore: RTC GETTIME
  Case 3
    Print @(42*FW,18*FH);:TMP$=InputE$("DD/MM/YYYY HH:MM",17)
    On ERROR ignore: RTC SETTIME TMP$
  End Select
  Color c(15),c(1)
  W_ERROR_MSG : If MM.Errno Then On ERROR CLEAR: Exit Sub
Case 128,136'Up
  Print @(x*FW,y*FH);"   ";
  If i>0  Then Inc i,-1
Case 129,137'Dn
  Print @(x*FW,y*FH);"   ";
  If i<3 Then Inc i,1
Case Else
  Exit Sub
End Select
Loop
End Sub
'------------------------------------------------
Sub Sort_Order
Local integer SO,x,y,i
If PSide Then
  SO=FLS(4,1):x=0
Else
  SO=FLS(4,0):x=40
EndIf
Color c(0),c(15)
OpenWindow("Sort:",x,31-SOMAX,9,SOMAX+3)
Timer =0:x=x+2
Do
If CKey Then
  y=31-SOMAX
  For i=0 To SOMAX
    Inc y
    If i=SO Then
      Print @(x*FW,y*FH,2)GetRecordFromData$("DATA_SORT",i+1);
    Else
      Print @(x*FW,y*FH)GetRecordFromData$("DATA_SORT",i+1);
    EndIf
  Next
EndIf
CKey=Asc(Inkey$)
Select Case CKey
Case 0
Case 128,131,136'Up
  Timer =0:Inc SO,-1:If SO<0 Then SO=SOMAX
Case 129,130,137'Dn
  Timer =0:Inc SO, 1:If SO>SOMAX Then SO=0
Case Else
  Exit Do
End Select
If Timer>10000 Then Exit Do
Loop
If PSide Then FLS(4,1)=SO Else FLS(4,0)=SO
End Sub
'================================================
' DATA OF PROGRAMM
'================================================
DATA_MENU_COMMANDS:
Data ".."
Data "Sort_Order"
Data "Set_Time_and_Date"
Data ""
DATA_SORT:
Data "None ","Name>","Name<","Date>","Date<","Size>","Size<","Extn>","Extn<"
DATA_FKeyMain:
Data "Help  ","Menu  ","View  ","Edit  ","Copy  ","RenMov","MkDir ","Delete","User  "
DATA_FKeyAlt1:
Data "Left  ","Right ","View2 ","Edit  ","X.SEND","X.RCVE","Find  ","      ","      "
DATA_FKeyAlt2:
Data "FILES ","      ","      ","      ","      ","      ","      ","      ","      "
DATA_FKeyZero:
Data "      ","      ","      ","      ","      ","      ","      ","      ","     "
DATA_HELP:
Data "File Manager  v1.51 for PicoMiteVGA/HDMI MMBasic Ver.> 6.00.01"
Data " "
Data "by Jatlov Vadim (c)2025        @javavi ( javavict@gmail.com )"
Data "my thanks to twofingers, dddns, Volhout."
Data " "
Data "-------------------------------------------------------------"
Data "[Arrows],[PgUp],[PgDn],[Home],[End] Keys - Navigation."
Data "[Tab] Key - Switches between left & right panels."
Data "[~]   Key - File Sort Order Switch"
Data "[F1] ... [F9] Keys - Conrol Functions."
Data "[Alt] or [BS] Keys - Alternate Conrol Functions."
Data "[Enter] Key - Entering a folder, running a file."
Data "[Space] Key - Selecting files on the panel."
Data "[Pause] Key - Stop playing music files."
Data "[<][>][Home][End][Del][BS][Insert] Keys for Input line editor."
Data "[Esc]&[F10] Key - Escape from input & Exit to command prompt."
Data " "
Data "F2|Menu of build-in subprograms for expanding the functionality."
Data "F3|Viewing text files, images, listen to music files, etc."
Data "F9|User menu of castom commands to add frequenly used commands."
Data ""
