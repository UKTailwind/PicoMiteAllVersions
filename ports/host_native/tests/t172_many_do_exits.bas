' Stress test EXIT DO fixup limit (same 16-entry array as EXIT FOR)
DIM x%
x% = 0
DO
  x% = x% + 1
  IF x% = 99 THEN EXIT DO
  IF x% = 98 THEN EXIT DO
  IF x% = 97 THEN EXIT DO
  IF x% = 96 THEN EXIT DO
  IF x% = 95 THEN EXIT DO
  IF x% = 94 THEN EXIT DO
  IF x% = 93 THEN EXIT DO
  IF x% = 92 THEN EXIT DO
  IF x% = 91 THEN EXIT DO
  IF x% = 90 THEN EXIT DO
  IF x% = 89 THEN EXIT DO
  IF x% = 88 THEN EXIT DO
  IF x% = 87 THEN EXIT DO
  IF x% = 86 THEN EXIT DO
  IF x% = 85 THEN EXIT DO
  IF x% = 84 THEN EXIT DO
  IF x% = 83 THEN EXIT DO
  IF x% = 5 THEN EXIT DO
LOOP
PRINT "x="; x%
' Second test: many false exits, real exit is #18
DIM y%
y% = 0
DO
  y% = y% + 1
  IF y% > 100 THEN EXIT DO
  IF y% = 200 THEN EXIT DO
  IF y% = 201 THEN EXIT DO
  IF y% = 202 THEN EXIT DO
  IF y% = 203 THEN EXIT DO
  IF y% = 204 THEN EXIT DO
  IF y% = 205 THEN EXIT DO
  IF y% = 206 THEN EXIT DO
  IF y% = 207 THEN EXIT DO
  IF y% = 208 THEN EXIT DO
  IF y% = 209 THEN EXIT DO
  IF y% = 210 THEN EXIT DO
  IF y% = 211 THEN EXIT DO
  IF y% = 212 THEN EXIT DO
  IF y% = 213 THEN EXIT DO
  IF y% = 214 THEN EXIT DO
  IF y% = 215 THEN EXIT DO
  IF y% = 10 THEN EXIT DO
LOOP
PRINT "y="; y%
