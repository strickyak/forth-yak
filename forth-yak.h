#include <assert.h>
#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#define D    if(Debug)fprintf

namespace forth_yak {
  extern int Debug;

#ifndef CELLSIZE
#define CELLSIZE 4
#endif

#if CELLSIZE == 2
  typedef int16_t C;            // Cell
  typedef uint16_t U;           // Unsigned cell
#elif CELLSIZE == 4
  typedef int32_t C;            // Cell
  typedef uint32_t U;           // Unsigned cell
  typedef float F;              // Floating point
#elif CELLSIZE == 8
  typedef int64_t C;            // Cell
  typedef uint64_t U;           // Unsigned cell
  typedef double F;             // Floating point
#else
   error-- unknown cellsize;
#endif
  typedef unsigned char B;      // Byte

  constexpr size_t S = sizeof(C);
  constexpr size_t LINELEN = 500;

  extern char *Mem;
  extern int MemLen;

  typedef union {
    C c;
    char b[S];
  } Both;

  extern C HerePtr;             // points to Here variable
  extern C LatestPtr;           // points to Latest variable
  extern C StatePtr;            // points to State variable
  extern C Ds;                  // data stack ptr
  extern C Rs;                  // return stack ptr
  extern C Ip;                  // instruction ptr
  extern C W;                   // W register
  extern const char *Argv0;

  typedef enum {
    _END,                       // 0
    _PLUS,
    _DOT,
    DUP,                        // 3
    DROP,
    _LIT_,
    _ENTER_,                    // 6
    _EXIT_,
    _COLON,                     // 8
    ALIGN,
    _MINUS,
    _TIMES,
    _DIVIDE,
    MOD,
    _EQ,
    _NE,
    DO,
    _DO,
    LOOP,
    UNLOOP,
    IF,
    ELSE,
    THEN,
    BRANCH,
    BRANCH0,
  } Opcode;

  inline bool streq(const char *p, const char *q) {
    D(stderr, "  (streq <%s> <%s>) ", p, q);
    return 0 == strcmp(p, q);
  };

  // Get & Put.

  inline C Get(C i) {
    return *(C *) (Mem + i);
  };

  inline void Put(C i, C x) {
    *(C *) (Mem + i) = x;
  };

  // Peek, Poke, Push, Pop.
  inline C Pop() {
    C p = Ds;
    Ds += S;
    return Get(p);
  }

  inline void Push(C x, int i = 0) {
    Ds -= S;
    Put(Ds, x);
  }

  inline void Poke(C x, int i = 0) {
    Put(Ds + S * i, x);
  }

  inline C Peek(int i = 0) {
    return Get(Ds + S * i);
  }

  inline void DropPoke(C x) {
    Ds += S;
    Put(Ds, x);
  }

  inline C PopR() {
    Rs += S;
    return Get(Rs - S);
  }

  inline void PushR(C x, int i = 0) {
    Rs -= S;
    Put(Rs, x);
  }

  inline void PokeR(C x, int i = 0) {
    Put(Rs + S * i, x);
  }

  inline C PeekR(int i = 0) {
    return Get(Rs + S * i);
  }

  inline C Aligned(C x) {
    constexpr C m = S - 1;
    return (x + m) & (~m);
  }
}                               // namespace forth_yak
