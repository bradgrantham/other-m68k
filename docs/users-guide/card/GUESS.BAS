10 REM GUESS: THE GRIFFIN THINKS OF A NUMBER
20 RANDOMIZE
30 secret = INT(RND(1) * 100) + 1
40 tries = 0
50 PRINT "I am thinking of a number from 1 to 100."
60 INPUT "Your guess"; guess
70 tries = tries + 1
80 IF guess < secret THEN PRINT "Too low.": GOTO 60
90 IF guess > secret THEN PRINT "Too high.": GOTO 60
100 PRINT "You got it in"; tries; "tries."
