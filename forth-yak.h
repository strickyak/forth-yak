#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <memory.h>
#include <stdlib.h>

#define D    if(Debug)fprintf

namespace forth_yak {
extern int Debug;

#ifndef CELLSIZE
#define CELLSIZE 4
#endif

#if CELLSIZE == 2
typedef int16_t   C;  // Cell
typedef uint16_t  U;  // Unsigned cell
#elif CELLSIZE == 4
typedef int32_t   C;  // Cell
typedef uint32_t  U;  // Unsigned cell
typedef float     F;  // Floating point
#elif CELLSIZE == 8
typedef int64_t   C;  // Cell
typedef uint64_t  U;  // Unsigned cell
typedef double    F;  // Floating point
#else
error -- unknown cellsize
#endif

typedef unsigned char B;  // Byte

constexpr size_t S = sizeof(C);
constexpr size_t LINELEN = 500;

extern char* Mem;
extern int MemLen;

typedef union {
	C c;
	char b[S];
} Both;

inline C Get(C i) {
	Both x;
	memcpy(x.b, Mem+i, S);
}

inline void Put(C i, C x) {
	Both z;
	z.c = x;
	memcpy(Mem+i, z.b, S);
}

extern C HerePtr;  // points to Here variable
extern C LatestPtr;  // points to Latest variable
extern C Ds;  // data stack ptr
extern C Rs;  // return stack ptr
extern C Ip;  // instruction ptr
extern C W;  // W register
extern const char* Argv0;

inline bool streq(const char* p, const char* q) {
	D(stderr, "  (streq <%s> <%s>) ", p, q);
	return 0 == strcmp(p, q);
}

typedef enum {
	NONE_OpCode,
	THREE,
	EIGHT,
	PLUS,
	DOT,
} Opcode;

typedef void OpFn();
extern OpFn* Ops[LINELEN];

} // namespace forth_yak
