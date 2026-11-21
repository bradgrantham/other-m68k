10 REM SONG: FRERE JACQUES AS A ROUND FOR TWO VOICES
20 a$ = "L4 C D E C C D E C E F G2 E F G2"
30 b$ = "L8 G A G F L4 E C L8 G A G F L4 E C"
40 c$ = "L4 C <G >C2 C <G >C2"
50 tune$ = a$ + b$ + c$
60 PLAY "T140 O4" + tune$, "T140 O3 R1 R1" + tune$
70 PRINT "Playing. The second voice comes in late."
80 IF PLAYING(0) THEN 80
