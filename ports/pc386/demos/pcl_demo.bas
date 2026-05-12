10 REM pcl_demo.bas - HP PCL over PC386 LPT1:
20 E$ = CHR$(27)
30 OPEN "LPT1:" FOR OUTPUT AS #1
40 PRINT #1, E$; "E";
50 PRINT #1, E$; "&l2A";
60 PRINT #1, E$; "&l0O";
70 PRINT #1, E$; "&l6D";
80 PRINT #1, E$; "&a0L";
90 PRINT #1, E$; "(s0p10h12v0s0b4099T";
100 PRINT #1, "PC386 MMBasic PCL demo"
110 PRINT #1, "Printed through LPT1: to the HP raw port 9100"
120 PRINT #1, ""
130 PRINT #1, E$; "(s3B"; "Bold text from MMBasic"; E$; "(s0B"
140 PRINT #1, E$; "&d0D"; "Underlined text"; E$; "&d@"
150 PRINT #1, E$; "(s16.67H"; "Compressed 16.67 cpi text: 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ"
160 PRINT #1, E$; "(s10H"
170 PRINT #1, ""
180 PRINT #1, E$; "(s18V"; "Large 18 point text"; E$; "(s12V"
190 PRINT #1, ""
200 PRINT #1, "Raw BASIC values:"
210 FOR I = 1 TO 8
220 PRINT #1, "  I ="; I; "   I*I ="; I * I
230 NEXT I
240 PRINT #1, ""
250 PRINT #1, "End of demo."
260 PRINT #1, CHR$(12);
270 CLOSE #1
280 PRINT "PCL demo sent to LPT1:"
