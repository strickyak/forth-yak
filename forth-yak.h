#include <assert.h>
#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#define D    if(Debug)fprintf

extern int Debug;

#ifndef CELLSIZE
#define CELLSIZE 4
#endif

#if CELLSIZE == 2
typedef int16_t C;              // Cell
typedef uint16_t U;             // Unsigned cell
#elif CELLSIZE == 4
typedef int32_t C;              // Cell
typedef uint32_t U;             // Unsigned cell
typedef float F;                // Floating point
#elif CELLSIZE == 8
typedef int64_t C;              // Cell
typedef uint64_t U;             // Unsigned cell
typedef double F;               // Floating point
#else
error-- unknown cellsize;
#endif
typedef unsigned char B;        // Byte
typedef unsigned long long ULL;

constexpr size_t S = sizeof(C);
constexpr size_t LINELEN = 500;

extern char *Mem;
extern int MemLen;

typedef union {
  U c;
  char b[S];
} Both;

extern U HerePtr;               // points to Here variable
extern U LatestPtr;             // points to Latest variable
extern U StatePtr;              // points to State variable
extern U Ds;                    // data stack ptr
extern U Rs;                    // return stack ptr
extern U Ds0;                   // data stack base
extern U Rs0;                   // return stack base
extern U Ip;                    // instruction ptr
extern U W;                     // W register
extern const char *Argv0;

typedef enum {
  _END,                         // 0
  _PLUS,
  _DOT,
  DUP,                          // 3
  DROP,
  _LIT_,
  _ENTER_,                      // 6
  _EXIT_,
  _SEMICOLON,
  _COLON,
  ALIGN,
  _MINUS,
  _TIMES,
  _DIVIDE,
  MOD,
  _EQ,
  _NE,
  _LT,
  _LE,
  _GT,
  _GE,
  DUMPMEM,
  WORDS,
  R0,
  S0,
  MUST,
  IMMEDIATE,
  HIDDEN,
  KEY,
  WORD,
  DO,
  _DO,
  LOOP,
  UNLOOP,
  IF,
  ELSE,
  THEN,
  BRANCH,
  BRANCH0,
  NOP,
} Opcode;

constexpr B LEN_MASK = 0x1F;    // max length is 31.
constexpr B HIDDEN_BIT = 0x20;
constexpr B IMMEDIATE_BIT = 0x80;

inline bool streq(const char *p, const char *q)
{
  D(stderr, "  (streq <%s> <%s>) ", p, q);
  return 0 == strcmp(p, q);
};

  // Get & Put.

inline U Get(U i)
{
  return *(U *) (Mem + i);
};

inline void Put(U i, U x)
{
  *(U *) (Mem + i) = x;
};

  // Peek, Poke, Push, Pop.
inline U Pop()
{
  U p = Ds;
  Ds += S;
  return Get(p);
}

inline void Push(U x, int i = 0)
{
  Ds -= S;
  Put(Ds, x);
}

inline void Poke(U x, int i = 0)
{
  Put(Ds + (i * S), x);
}

inline U Peek(int i = 0)
{
  return Get(Ds + (i * S));
}

inline void DropPoke(U x)
{
  Ds += S;
  Put(Ds, x);
}

inline U PopR()
{
  Rs += S;
  return Get(Rs - S);
}

inline void PushR(U x, int i = 0)
{
  Rs -= S;
  Put(Rs, x);
}

inline void PokeR(U x, int i = 0)
{
  Put(Rs + S * i, x);
}

inline U PeekR(int i = 0)
{
  return Get(Rs + S * i);
}

inline U Aligned(U x)
{
  constexpr U m = S - 1;
  return (x + m) & (~m);
}

// Class InputKey handles all FORTH input for KEY and WORD.
// Initialize it with a list of files to slurp, and whether
// to read from stdin after slurping those files.
// After the last EOF is read, this will exit(0) the entire program.
class InputKey {
public:
  void Init(const char *text, int filec, const char *filev[], bool add_stdin);
  U Key();
private:
  void Advance();

  const char *text_;
  int filec_;
  const char **filev_;
  bool add_stdin_;
  FILE *current_;
  bool isatty_;
  bool next_ok_;
};
