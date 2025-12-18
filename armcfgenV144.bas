'
'  CFGEN.BAS  V1.43a
'    V1.1  Improved algorithms thanks to TZ
'    V1.11 Tidy Up
'    V1.12 Fixed bug in symbol table loop (thanks Jim)
'    V1.2  Added getting static data from  .rodata
'    V1.3  deal with .dinit between .text and .rodata
'    V1.31  minor formatting issue on output
'    V1.4  Output symbol names of static data
'    V1.41 Correct minor formatting issues when .rodata not used
'    V1.42 Sort function name output, block output in CONSTANTS declared in Join mode
'    V1.43 CMM2 version
'    V1.43a Commandline allowed
'           symbols$ LENGTH increase to 61 from 31
'           symbolsort$ LENGTH increase to 75 from 45
'           sname$ added as CSUB name for MERGE i.e. sname$=UCase$(cname$)
'    V1.44 Deal with functions starting on 16-bit boundaries
'
'  Program to convert a .ELF file to Basic statements formated for use as CFunctions
'  Runs under MMBasic for DOS V4.5
'
'  It is recommended that the .ELF file is copied to the same directory as this file
'  and that the filename is shortened to make life easier
'
'  To run open a DOS box on the computer
'  navigate to the directory containing the MMBasic.exe file and this program
'  run MMBasic and you will get the normal ">" prompt
'  Load this program
'  Type run
'
'  Then give the filename of the .elf file including the extension
'  Choose "join" or "merge" to define the output format
'      "join" is typically used for individual CFunctions, each function in the .ELF file is given
'      its own header, all the CFunctions are output to a single file but can then be copied and
'      pasted as required. When "join is chosen the "main" function is a dunumsymbolsy and is not included in the output
'      "merge" is used for CFunctions such as drivers where one C function can be called from another
'
'  Choose the name of the file for the Basic output. Do not include ".BAS", this will be appended automatically
'
'  You can run MMBasic for DOS from MMEDIT, but in this case you will need to specify the correct directory information for the .ELF file
'
Dim section_header, section_header_size, section_header_num
Dim string_table_sector, string_table, text_section, text_position, text_size, text_link, symtab_section, strtab_section
Dim symtab_offset, symtab_num, symstr_offset, first

Dim rodata_position, rodata_size, rodata_section,rodata_address$
Dim dinit_section, dinit_address$, pad=0

Dim text_add$ length 31
Dim symbols$(100,2) length 61
'************************************************************************ change 1
Dim symbolsort$(100) length 75
'************************************************************************
inname$ = ""
cname$ = ""
sname$ = ""
mode = 0

Place = Instr(MM.CmdLine$," ")
If Place > 0 Then
  Place = Place + 1
  CmdName$ = Mid$(MM.CmdLine$,Place,Instr(Place,MM.CmdLine$," ")-Place)
  Place = Instr(UCase$(CmdName$),".ELF")
  If  Place > 0 Then
    Print CmdName$
    inname$ = CmdName$
    cname$ = Left$(CmdName$,Place - 1)
  EndIf
  If Instr(MM.CmdLine$,"-j") > 0 Then
    m$ = "join"
  Else If Instr(MM.CmdLine$,"-m") > 0 Then
    m$ = "merge"
  EndIf
EndIf

If inname$ = "" Then
  Line Input ".ELF filename ? ",inname$
EndIf
If UCase$(Right$(inname$,4))<>".ELF" Then inname$=inname$+".elf"
Do While (m$<>"join") And (m$<>"merge") And (m$<>"m") And (m$<>"j")
   Line Input "mode(join, merge) ? ",m$
Loop

'mode =0
If m$="merge" Or m$="m" Then mode=1

If cname$ = "" Then
  Line Input "CFunction name ? ",cname$
EndIf

'line input ".ELF filename ? ",inname$
'if ucase$(right$(inname$,4))<>".ELF" then inname$=inname$+".elf"
'do
'   line input "mode(join, merge) ? ",m$
'loop while (m$<>"join") and (m$<>"merge") and (m$<>"j") and (m$<>"m")
'mode =0
'if m$="merge" or m$="m" then mode=1
'line input "CFunction name ? ",cname$

sname$=UCase$(cname$) '''''''''''''''''''''''''''''''''''''''''''''''''''''         'G.A.
If UCase$(Right$(cname$,4))<>".BAS" Then cname$=cname$+".bas"
Print "Created "+cname$
Open cname$ For output As #2
Print #2,"'File "+cname$ +" written "+Date$+" "+Time$ +" v1.44"

first=1 'first time into the various loops
Print "processing ",inname$
Open inname$ For random As 1
zeroseek 1, 0
'
dat$ = Input$(32, 1) ' Get the .elf identification and unused header
dat$=Input$(4,1)
section_header=getnum4(dat$)
dat$=Input$(10,1) 'step past the flags ,header size, and program header info
dat$=Input$(2,1)
section_header_size=getnum2(dat$)
dat$=Input$(2,1)
section_header_num=getnum2(dat$)
dat$=Input$(2,1)
string_table_sector=getnum2(dat$)
'
i=section_header+ (Section_header_size*string_table_sector) + 16 'get to the offset in the string table sector
zeroseek 1,i
dat$=Input$(4,1)
string_table= getnum4(dat$) 'get the offset to the string table
'************************************************************* change 2
' Delete from here: moved to after test for invalid mode usage
'Print "Functions found:"
'print "Address",chr$(9)+"Function"
'*************************************************************
For j=0 To section_header_num-1
    i=section_header+ (Section_header_size*j) 'get to the next sector header
    zeroseek 1,i 'point to the .text section header
    dat$=Input$(4,1) 'get the pointer to the sector name string
    k=getnum4(dat$)
    zeroseek 1,k+string_table 'point to the name string for this sector
    s$=""
    d$=Input$(1,1)
    Do While Asc(d$)<>0 'read in the zero terminated string
        S$=S$+D$
        d$=Input$(1,1)
    Loop
    If s$=".text" And text_section=0 Then 'store text sector number
      text_section=j
    EndIf
    If s$=".symtab" And symtab_section=0 Then 'store symbol table sector number
      symtab_section=j
    EndIf
    If s$=".strtab" And strtab_section=0 Then 'store symbol table strings sector number
      strtab_section=j
    EndIf

    If s$=".rodata" And rodata_section=0 Then 'store rodata sector number
      rodata_section=j
    EndIf
    If s$=".dinit" And dinit_section=0 Then 'store rodata sector number
      dinit_section=j
    EndIf

Next j
'
i=section_header+ (Section_header_size*text_section) 'get to the .text offset
zeroseek 1,i 'point to the .text section header
dat$=Input$(12,1)
dat$=Input$(4,1)
text_add$=getadd$(dat$)
dat$=Input$(4,1)
text_position=getnum4(dat$)
dat$=Input$(4,1)
text_size= getnum4(dat$)
dat$=Input$(4,1)
text_link= getnum4(dat$)
'
i=section_header+ 16+ (Section_header_size*(symtab_section))
zeroseek 1,i 'point to the symbol table section header
dat$=Input$(4,1) 'get the offset to the symbol table
symtab_offset=getnum4(dat$)
dat$=Input$(4,1)
symtab_num=getnum4(dat$)/16
'
i=section_header+ 16+ (Section_header_size*(strtab_section))
zeroseek 1,i 'point to the string table section header
dat$=Input$(4,1) 'get the offset to the symbol table
symstr_offset=getnum4(dat$)

'

i=section_header+ (Section_header_size*(rodata_section))
zeroseek 1,i 'point to the string table section header
dat$=Input$(12,1)
dat$=Input$(4,1)
rodata_address$=getadd$(dat$)
dat$=Input$(4,1)
rodata_position=getnum4(dat$)
dat$=Input$(4,1)
rodata_size= getnum4(dat$)

If rodata_size<>0 And mode=0 Then
  Print "Error: Constant data not allowed in JOIN mode"
  Close #2
  End
EndIf
i=section_header+ (Section_header_size*(dinit_section))
zeroseek 1,i 'point to the string table section header
dat$=Input$(12,1)
dat$=Input$(4,1)
dinit_address$=getadd$(dat$)
dat$=Input$(4,1)
dat$=Input$(4,1)
dinit_size= getnum4(dat$)
'
If (rodata_address$>dinit_address$) And (dinit_address$>test_add$) And rodata_size<>0 Then
   Print dinit_size," bytes padding needed for dinit"
   pad=dinit_size
EndIf

'

numsymbols=0 'counter for the stored symbols array
For i=0 To symtab_num-1 'loop through all the symbols
    zeroseek 1,symtab_offset+(16*i) 'point to the next symbol in the .symtab section
    dat$=Input$(4,1)
    k=getnum4(dat$)
    dat$=Input$(4,1)
    a$=getadd$(dat$)
    dat$=Input$(4,1)
    dat$=Input$(1,1)
    l=Asc(dat$)
    zeroseek 1,k+symstr_offset 'point to the symbol name
  s$=""
  d$=Input$(1,1)
  Do While Asc(d$)<>0
    S$=S$+D$
    d$=Input$(1,1)
  Loop
    If (l=18 And Left$(s$,1)<>"_") Or l=1 Or l=2 Then 'Only store function names or data
      symbols$(numsymbols,1)=a$
      symbols$(numsymbols,2)=s$
'     Print l,a$," ",s$
'**************************************************************************************** change 3
'  now puts symbols and addresses into an array sorting as we go for output in the correct order
'
        If l<>1 Then
      insertsort(numsymbols,symbols$(numsymbols,1)+Chr$(9)+symbols$(numsymbols,2))
    EndIf
        numsymbols=numsymbols+1
        If s$="main" Then main_offset$=a$
    EndIf
Next i
'
Print "Functions found:"
Print "Address",Chr$(9)+"Function"
For i=0 To numsymbols-1
Print symbolsort$(i)
'  print hex$(val(("&H"+left$(symbolsort$(i),8)))-1),right$(symbolsort$(i),len(symbolsort$(i))-8)
Next i
'****************************************************************************************
'
main$= Right$("00000000" + Hex$(subtract(Right$(main_offset$,5), Right$(text_add$,5))), 8) 'calculate the offset
'
zeroseek 1,text_position
k=0
currentfunction=0
s$=text_add$
For i=1 To text_size Step 4
    For j=0 To numsymbols-1
        If Val("&H"+s$)=Val("&H"+symbols$(j,1))-1 Then 'address matches the start of a function
            currentfunction=j
            If k<>0 And mode Then Print #2,"" 'The last function didn't end with a newline so output one
            If k<>0  And (Not mode) And symbols$(j,2)<>"main" Then Print #2,""
            k=0
            If first And mode Then ' First time in for a merged file output the Cfunction name
                Print #2,"CSUB "+sname$   '''''''''''''''''''''''''''''''''''G.A.
              Print #2,Chr$(9)+main$
            EndIf
            If mode Then Print #2,Chr$(9)+"'"+symbols$(j,2)
            If Not mode And symbols$(j,2)<>"main" Then
            If Not first Then
                Print #2,"End CSUB"
                Print #2,"'"
            EndIf
            Print #2,"CSUB "+symbols$(j,2)
            Print #2,Chr$(9)+"00000000"
        EndIf
        first=0
    EndIf
    Next j
    If k=0 Then Print #2,Chr$(9);
    dat$=getadd$(Input$(4,1))
    If Not mode And symbols$(currentfunction,2)<>"main" Then Print #2, dat$+" ";
    If mode Then Print #2, dat$+" ";
    k=k+1
    If k=8 Then 'deal with line feed every eight words
        If Not mode And symbols$(currentfunction,2)<>"main" Then Print #2,""
        If mode Then Print #2,""
        k=0
    EndIf
    increment4(s$) 'step on the program counter
Next i

If pad<>0 Then
If k<>0 Then Print #2,""
Print #2,Chr$(9)+"'.dinit"
k=0
For i=1 To pad Step 4
  If k=0 Then Print #2,Chr$(9);
  dat$="00000000"
  Print #2,dat$+" ";
  k=k+1
  If k=8 Then
    Print #2,""
    k=0
  EndIf
Next i

EndIf
'
If k<>0 Then Print #2,""
If rodata_size<>0 Then Print #2,Chr$(9)+"'.rodata"
zeroseek 1,rodata_position
k=0
s$=rodata_address$
For i=1 To rodata_size Step 4
    For j=0 To numsymbols-1
        If s$=symbols$(j,1) Then
            If k<>0 And mode Then Print #2,"" 'The last function didn't end with a newline so output one
          dat$=symbols$(j,2)
          m=Instr(dat$,".")
          dat$=Left$(dat$,numsymbols-1)
          Print #2,Chr$(9)+"'"+dat$
          k=0
        EndIf
    Next j
  If k=0 Then Print #2,Chr$(9);
  dat$=getadd$(Input$(4,1))
  Print #2,dat$+" ";
  k=k+1
  If k=8 Then
    Print #2,""
    k=0
  EndIf
  increment4(s$) 'step on the program counter
Next i
If k<>0 Then Print #2,""

Print #2,"End CSUB"
Close 1
Close #2
'
Quit
End
'******************************************************************************** change 4
Sub insertsort(n,new$)
  Local i,j
  i=1
  Do While new$> symbolsort$(i-1) And i<=n
    i=i+1
  Loop
  If i-1<n Then
    For j=n+1 To i+1 Step -1
      symbolsort$(j-1)=symbolsort$(j-2)
    Next j
  EndIf
  symbolsort$(i-1)=new$
End Sub
'************************************************************************************
Function subtract(b$,c$)
  Local f,g
   f=Val("&H"+b$)
   g=Val("&H"+c$)
  subtract=(f-g-1)/4
End Function

Function getjumpaddress$(a$)
  Local f
  f=Val("&H"+a$)*4+1048576
  getjumpaddress$=Left$(text_add$,3)+Right$(Hex$(f),5)
End Function

Function getadd$(e$)
   Local a,b,c,d
   a=Asc(Left$(e$,1))+256
   b=Asc(Mid$(e$,2,1))+256
   c=Asc(Mid$(e$,3,1))+256
   d=Asc(Right$(e$,1))+256
   getadd$=Right$(Hex$(d),2) +Right$(Hex$(c),2) +Right$(Hex$(b),2) +Right$(Hex$(a),2)
End Function

Function getnum4(e$)
   Local a,b,c,d
   a=Asc(Left$(e$,1))
   b=Asc(Mid$(e$,2,1))
   c=Asc(Mid$(e$,3,1))
   d=Asc(Right$(e$,1))
   getnum4=((((d*256)+c)*256)+b)*256+a
End Function

Function getnum2(d$)
   Local a,b
   a=Asc(Left$(d$,1))
   b=Asc(Right$(d$,1))
   getnum2=b*256+a
End Function

Sub increment4(value$)
    Local V$(7) LENGTH 1, i, NewValue$ LENGTH 8
    Local vv
    For i= 0 To 7: V$(i) = Mid$(value$, i+1, 1): Next
    V$(7) = Chr$(Asc(V$(7)) + 4)
    For i = 7 To 1 Step -1
       vv = Asc(V$(i))
       If vv > 70 Then ' "F"
          V$(i) = Chr$(vv - 23): V$(i-1) = Chr$(Asc(V$(i-1)) + 1)
       Else
         If vv >57 And vv < 65 Then V$(i) = Chr$(vv + 7) ' "9" "A"
       EndIf
       NewValue$ = V$(i)+ NewValue$
    Next
    value$ = V$(0) + NewValue$
End Sub

Function subaddress$(p$,b)
   Local a
   a=Val("&H"+Right$(p$,4))+65536-b
   subaddress$=Left$(p$,4)+Right$(Hex$(a),4)
End Function

Sub zeroseek(i,j)
    Seek 1,j+1
End Sub
