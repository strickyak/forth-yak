1 2 3 4 5 + + + +   15 = must

3 5 +   8 = must
5 8 -   -3 = must

5 8 swap -   3 = must
5 8 over - +   8 = must 

2 4 6   dup 6 = must  rot  dup 2 = must  rot  dup 4 = must   rot  dup 6 = must   drop drop drop
2 4 6   dup 6 = must -rot  dup 4 = must -rot  dup 2 = must  -rot  dup 6 = must   drop drop drop

4 9 *   36 = must
4 -9 *   -36 = must
100 12 /   8 = must
100 12 mod   4 = must

100 4 drop 5 drop 6 drop 7 + 107 = must
3 dup dup dup + + -  -6 = must
1000   : foo 11 1 DO i + LOOP ; foo    1055 = must
1000   : foo 11 1 ?DO i + i + LOOP ; foo    1110 = must
