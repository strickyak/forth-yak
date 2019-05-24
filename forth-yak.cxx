#include "forth-yak.h"

namespace forth_yak {

char* Mem;
int MemLen;
C HerePtr;  // points to Here variable
C LatestPtr;  // points to Latest variable
C Ds;  // data stack ptr
C Rs;  // return stack ptr
C Ip;  // instruction ptr
C W;  // W register
const char* Argv0;
OpFn* Ops[LINELEN];
int Debug;

void Fatal(const char* msg, int x) {
	fprintf(stderr, "***\n* Fatal: %s: %s [%d]\n", Argv0, msg, x);
	exit(13);
}

void DumpMem() {
  D(stderr, "Dump: Rs=%d  Ds=%d  Ip=%d {\n", Rs, Ds, Ip);
  for (int i = 0; i < MemLen; i++) {
  	char m = Mem[i];
  	if (m != 0) {
		char c = ' ';
		if ( 32 <= m && m <= 126 ) {
			c = m;
		}
		C x;
		if (i + S < MemLen) {
			x = Get(i);
		}

		if (i >= Rs) {
			D(stderr, "  [%8d+Rs] %3d %c %12lld\n", i-Rs, m, c, x);
		} else if (i >= Ds) {
			D(stderr, "  [%8d+Ds] %3d %c %12lld\n", i-Ds, m, c, x);
		} else {
			D(stderr, "  [%8d] %3d %c %12lld\n", i, m, c, x);
		}
	}
  }
  D(stderr, "}\n");
}

void OpPLUS() {
	C x = Get(Ds);
	C y = Get(Ds+S);
	Put(Ds, 0);
	Put(Ds+S, x+y);
	Ds += S;
}
void OpDOT() {
	C x = Get(Ds);
	Ds += S;
	printf("%lld ", (long long)x);
	fflush(stdout);
}

void MakeOp(Opcode code, OpFn fn, const char* name) {
	Ops[code] = fn;

	C latest = Get(LatestPtr);
	C here = Get(HerePtr);
	Put(LatestPtr, here);

	Put(here, latest);
	here += S;
	strcpy(Mem+here, name);
	here += strlen(name) + 1;
	Put(here, code);
	here += S;
	Put(HerePtr, here);
}

C LookupWord(const char* s) {
	C ptr = Get(LatestPtr);
	while (ptr) {
D(stderr, " <<lookupWord(%s) trying ptr=%d ", s, ptr);
		char* name = &Mem[ptr + S];  // name follows link.
		if (streq(s, name)) {
			// code addr follows name and '\0'.
			return ptr + S + strlen(name) + 1;
		}
		ptr = Get(ptr);
	}
	return 0;
}

bool IsNumber(const char* s, C* out) {
	C z = 0;
	C sign = 1;
	if (*s == '-') {
		sign = -1;
		s++;
	}
	while (*s) {
		if ('0' <= *s && *s <= '9') {
			z = 10*z + (*s - '0');
		} else {
			*out = 0;
			return false;
		}
		++s;
	}
	*out = z;
	return true;
}

void Execute(const char* s) {
	C x = LookupWord(s);
	if (!x) {
		if (IsNumber(s, &x)) {
			Ds -= S;
			Put(Ds, x);
			D(stderr, " Literal(%lld) ", (long long) x);
			return;
		}
		Fatal("No such word", 0);
		return;
	}
	C op = Get(x);
	D(stderr, " Execute(%s)@%lld(op%d) ", s, (long long) x, op);
	if (0 < op && op < MemLen) {
		if (Ops[op]) {
			Ops[op]();
		} else {
			Fatal("undefined op", op);
		}
	} else {
		Fatal("bad op", op);
	}
}

void Init() {
	Mem = new char[MemLen];
	memset(Mem, 0, MemLen);

	C ptr = LINELEN;
	HerePtr = ptr;
	ptr += S;
	LatestPtr = ptr;
	ptr += S;
	Put(HerePtr, ptr);
	Put(LatestPtr, 0);

	Rs = MemLen - 32;
	Ds = MemLen - 32 - 1024;

	MakeOp(PLUS, OpPLUS, "+");
	MakeOp(DOT, OpDOT, ".");
}

int LineIndex;
char* NextWordInLine() {
	for (; LineIndex < LINELEN-1; LineIndex++)  {
		if (Mem[LineIndex] == '\0') return nullptr;
		if (Mem[LineIndex] <= 32) continue;

		int WordStart = LineIndex;	
		for(;LineIndex < LINELEN-1; LineIndex++) {
			if (Mem[LineIndex] <= 32) {
				Mem[LineIndex] = '\0';
				LineIndex++;
				return Mem+WordStart;
			}

		}
	}
}

void Run() {
	while (true) {
		memset(Mem, 0, LINELEN);
		fprintf(stderr, " ok ");
		auto cp = fgets(Mem, LINELEN, stdin);
		if (cp == nullptr) {
			fprintf(stderr, " *EOF* \n");
			break;
		}
		LineIndex = 0;
		const char* s;
		while (s = NextWordInLine()) {
			D(stderr, "Word(%s) ", s);
			Execute(s);
			DumpMem();
		}
	}
}

void Flush() {
	delete Mem;
}

int Main(int argc, const char* argv[]) {
	// printf("short %d\n", sizeof(short));
	// printf("int %d\n", sizeof(int));
	// printf("long %d\n", sizeof(long));
	// printf("long long %d\n", sizeof(long long));
	// printf("-42 => char %d\n", int(char(-42)));
	// printf("-42 => unsigned char %d\n", (int)(unsigned char)(-42));

	MemLen = 1000000;

	Argv0 = argv[0];
	++argv, --argc;
	while (argc > 0 && argv[0][0] == '-') {
		switch (argv[0][1]) {
		case 'm':
			MemLen = atoi(&argv[0][2]);
			break;
		case 'x':
			Debug = atoi(&argv[0][2]);
			break;
		default:
			Fatal("Bad flag", argv[0][1]);
		}
		++argv, --argc;
	}

	Init();
	Run();
	Flush();
}
} // namespace forth_yak

int main(int argc, const char* argv[]) {
	return forth_yak::Main(argc, argv);
}
