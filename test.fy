." test.fy
"

3 5 +   8 = must
5 8 -   -3 = must
4 9 *   36 = must
4 -9 *   -36 = must
100 12 /   8 = must
100 12 mod   4 = must
1 2 3 4 5 + + + +   15 = must
10 1+ 11 = must
10 1- 9 = must
10 4+ 14 = must
10 4- 6 = must


5 8 swap -   3 = must
5 8 over - +   8 = must 
3 4 8 9 2swap  4 = must  3 = must  9 = must  8 = must
44 66 ?dup  66 = must  66 = must  44 = must
44 0 ?dup  0 = must  44 = must

2 4 6   dup 6 = must  rot  dup 2 = must  rot  dup 4 = must   rot  dup 6 = must   drop drop drop
2 4 6   dup 6 = must -rot  dup 4 = must -rot  dup 2 = must  -rot  dup 6 = must   drop drop drop
1 2 3 4 5 nip nip + +   8 = must
1 2 3 4 5 tuck + * +    48 = must

100 4 drop 5 drop 6 drop 7 + 107 = must
3 dup dup dup + + -  -6 = must
1000   : foo 11 1 DO i + LOOP ; foo    1055 = must
1000   : foo 11 1 ?DO i + i + LOOP ; foo    1110 = must

: nando  IF  55  ELSE  666  THEN   ;
0  nando   666 = must
1  nando   55 = must
-1 nando   55 = must
10 100 < nando   55 = must
10 100 > nando   666 = must

: nzInc  dup IF  1 +  THEN   ;
0 nzInc nzInc nzInc 0 = must
1 nzInc nzInc nzInc 4 = must

: triangle
    10 0 DO
      i 0 ?DO
        i 6 > IF leave THEN
	j . i . i + dup . -999 .
      LOOP
    LOOP
    ;

1000 triangle    dup .   1098 = must

: addPosNegRange 10 -8 DO  i +   LOOP ;
1000 addPosNegRange    1009 = must

." CQ cq DE forth
two
three"
