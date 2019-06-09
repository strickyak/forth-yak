: prime ( n -- b )
  dup 2 < IF
    drop 0
  ELSE
      dup \ keep a copy of the number in question.
        2  ?DO \ count up to one less.
        dup i mod 0 = IF
          UNLOOP drop 0 EXIT
	  THEN
      LOOP
      drop 1
  THEN 
;

: primes ( limit -- )
   2 ?DO
     i prime IF
       i .
     THEN
   LOOP
;
 
100000 primes
