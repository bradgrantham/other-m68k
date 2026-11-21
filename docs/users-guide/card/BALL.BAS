10 REM BALL: A BALL THAT BOUNCES OFF THE WALLS
20 CLS 0
30 x = 160: y = 120: dx = 3: dy = 2
40 FCIRCLE x, y, 6, 0
50 x = x + dx: y = y + dy
60 IF x < 8 OR x > 311 THEN dx = -dx: SOUND 880, 30
70 IF y < 8 OR y > 231 THEN dy = -dy: SOUND 440, 30
80 FCIRCLE x, y, 6, RGB(255, 200, 0)
90 VSYNC
100 IF INKEY$ = "" THEN 40
110 TEXT
