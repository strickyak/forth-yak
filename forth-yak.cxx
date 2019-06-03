#include "forth-yak.h"

#include <map>
#include <string>

namespace forth_yak {

    using std::map;
    using std::string;

    FILE* ForthInputFile;

    char *Mem;
    int MemLen;
    C Ds;			// data stack ptr
    C D0;			// data stack ptr base
    C Rs;			// return stack ptr
    C R0;			// return stack ptr base
    C Ip;			// instruction ptr
    C W;			// W register
    const char *Argv0;

    C HerePtr;			// points to Here variable
    C LatestPtr;		// points to Latest variable
    C StatePtr;			// points to State variable

    int Debug;

    map<C, string> link_map, cfa_map, dfa_map;
    
    void SmartPrintNum(C x) {
	    if (link_map.find(x) != link_map.end()) {
		printf("  L{%s}", link_map[x].c_str());
	    } else if (cfa_map.find(x) != cfa_map.end()) {
		printf("  C{%s}", cfa_map[x].c_str());
	    } else if (dfa_map.find(x) != dfa_map.end()) {
		printf("  D{%s}", dfa_map[x].c_str());
	    } else if (-MemLen <= x && x <= MemLen) {
		printf("  %d", x);
	    } else {
		printf("  ---");
	    }
    }
    void DumpMem() {
        if (!Debug) return;
	printf("Dump: Rs=%d  Ds=%d  Ip=%d  HERE=%d LATEST=%d STATE=%d {\n", Rs, Ds, Ip, Get(HerePtr), Get(LatestPtr), Get(StatePtr));

        C rSize = (R0 - Rs) / S;
        printf("  R [%d] : ", rSize);
	for (int i = 0; i < rSize && i < 50; i++ ) {
		SmartPrintNum(Get(R0 - i*S));
	}
	putchar('\n');
        C dSize = (D0 - Ds) / S;
        printf("  D [%d] : ", dSize);
	for (int i = 0; i < dSize && i < 50; i++ ) {
		SmartPrintNum(Get(D0 - i*S));
	}
	putchar('\n');

        int expect = -1;
	for (int j = 0; j < MemLen; j+=16) {
	  bool something = false;
	  for (int i = j; i < j+16; i++) {
	    if (Mem[i]) something = true;
	  }
	  if (!something) continue;

	  printf("%c[%4d=%04x] ", (j == expect) ? ' ' : '*', j, j);
	  expect = j + 16;

	  for (int i = j; i < j+16; i++) {
	    char m = Mem[i];
	    char c = (32 <= m && m <= 126) ? m : '~';
	    putchar(c);
	    if ((i % S) == (S - 1)) putchar(' ');
	  }
	putchar(' ');
	putchar(' ');
	  for (int i = j; i < j+16; i++) {
	    char m = Mem[i];
	    printf("%02x ", m&255);
	    if ((i % S) == (S - 1)) putchar(' ');
	  }
	  for (int i = j; i < j+16; i+=S) {
	    C x = Get(i);

	    SmartPrintNum(x);

	  }
	putchar('\n');
	}
	printf( "}\n");
    }

    void CheckEq(int line, int a, int b) {
    	if (a != b) {
	  fprintf(stderr, "*** CheckEq Fails: line %d: %d != %d\n", line, a, b);
	  ++Debug;
	  DumpMem();
	  assert(0);
	}
    }

    void Fatal(const char *msg, int x) {
	fprintf(stderr, " *** %s: Fatal: %s [%d]\n", Argv0, msg, x);
	  ++Debug;
	  DumpMem();
	  assert(0);
    }
    
    void FatalS(const char *msg, const char* s) {
	fprintf(stderr, " *** %s: Fatal: %s `%s`\n", Argv0, msg, s);
	  ++Debug;
	  DumpMem();
	  assert(0);
    }
    
    int LineIndex;

    char *NextWordInLine() {
	for (; LineIndex < LINELEN - 1; LineIndex++) {
	    if (Mem[LineIndex] == '\0')
		return nullptr;
	    if (Mem[LineIndex] <= 32)
		continue;

	    int WordStart = LineIndex;
	    for (; LineIndex < LINELEN - 1; LineIndex++) {
		if (Mem[LineIndex] <= 32) {
		    Mem[LineIndex] = '\0';
		    LineIndex++;

D(stderr, "NextWordInLine -> <%s>\n", Mem+WordStart);
		    return Mem + WordStart;
		}
	    }
	}
	return nullptr;
    }

    char *NextWordInput() {
      while (1) {
        char* s = NextWordInLine();
        if (s) {
		fprintf(stderr, "[reading word  `%s`]\n", s);
		return s;
	}
	LineIndex = 0;
	auto cp = fgets(Mem, LINELEN, ForthInputFile);
	if (cp == nullptr) {
	  if (ForthInputFile == stdin) {
	    fprintf(stderr, " *EOF* \n");
	    exit(0);
	  } else {
	    fprintf(stderr, "[reading *EOF*]");
            return nullptr;
	  }
	}
      }
    }

    void CreateWord(const char* name, Opcode code) {
	C latest = Get(LatestPtr);
	C here = Get(HerePtr);
fprintf(stderr, "CreateWord(%s, %d): HerePtr=%d HERE=%d  latest=%d\n", name, code, HerePtr, here, latest);
	Put(LatestPtr, here);
	link_map[here] = name;

	Put(here, latest);
	here += S;
	strcpy(Mem + here, name);
	here += strlen(name) + 1;

	C there = Aligned(here);
	while (here < there) {
		Mem[here++] = 0xEE;  // 0xEE for debugging.
	}

	Put(here, code);
	here += S;
	Put(HerePtr, here);

	cfa_map[there] = name;
	dfa_map[here] = name;
    }

    void Comma(C x) {
    	C here = Get(HerePtr);
fprintf(stderr, "Comma(%d): HerePtr=%d HERE=%d\n", x, HerePtr, here);
	Put(here, x);
	Put(HerePtr, here+S);
    }

    bool WordStrAsNumber(const char *s, C * out) {
	C z = 0;
	C sign = 1;
	int base = 10;  // TODO hex
	if (*s == '-') {
	    sign = -1;
	    s++;
	}
	while (*s) {
	    if ('0' <= *s && *s <= '9') {
		z = base * z + (*s - '0');
	    } else {
		*out = 0;
		return false;
	    }
	    ++s;
	}
	*out = sign * z;
	return true;
    }

    C LookupCfa(const char *s) {
	C ptr = Get(LatestPtr);
	while (ptr) {
	    char *name = &Mem[ptr + S];	// name follows link.
	    D(stderr, "LookupCfa(%s) trying ptr=%d name=<%s>", s, ptr, name);
	    if (streq(s, name)) {
		// code addr follows name and '\0' and alignment.
		return Aligned(ptr + S + strlen(name) + 1);
	    }
	    ptr = Get(ptr);
	}
	return 0;
    }

    void ColonDefinition() {
		char* name = NextWordInput();
		if (!name) { Fatal("EOF during colon definition", 0); }

D(stderr, "COLON name <%s>\n", name);
		CreateWord(name, _ENTER_);
		Put(StatePtr, 1); // Compiling state.  // not yet used
		while (1) {
			char* word = NextWordInput();
			if (!word) {
				Fatal("EOF during colon definition", 0);
			}
D(stderr, "COLON word <%s>\n", word);
			if (streq(word, ";")) {
				break;
			}

D(stderr, "lookup up <%s>\n", word);
			C x = LookupCfa(word);
D(stderr, "looked up <%s>\n", word);
			if (x) {
				Comma(x);
			} else {
			    if (WordStrAsNumber(word, &x)) {
			        C lit = LookupCfa("(lit)");
				assert(lit);
				Comma(lit);
				Comma(x);
			    } else {
			    	FatalS("No such word", word);
			    }
			}
		}
		Comma(LookupCfa("(exit)"));
		Put(StatePtr, 0); // Interpreting state.  // not yet used
    }

    void Loop() {
      while(true) {
        C cfa = Get(Ip);
	if (!cfa) {
		D(stderr, "Got cfa 0; exiting Loop.\n");
		break;
	}
        C op = Get(cfa);
	W = cfa + S;
	fprintf(stderr, "Ip:%d W:%d Op:%d\n", Ip, op, W);
	Ip += S;

	switch (op) {
	case END: {
		return;
	}
	break;
	case PLUS: {
		C x = Pop();
		C y = Peek();
		Poke(x+y);
	}
	break;
	case DOT: {
		C x = Pop();
		printf("%lld ", (long long) x);
		fflush(stdout);
	}
	case DUP: {
		Push(Peek());
	}
	break;
	case DROP: {
		Pop();
	}
	break;
	case _LIT_: {
		Push(Get(Ip));
		Ip += S;
	}
	break;
	case _ENTER_: {
		PushR(Ip);
		Ip = W;
	}
	break;
	case _EXIT_: {
		Ip = PopR();
	}
	break;
	case COLON:
	  ColonDefinition();
	  break;
	default: {
	  Fatal("Bad op", op);
	}
	break;
	}
      }
    }

    void ExecuteWordStr(const char *s) {
	C cfa = LookupCfa(s);
	if (!cfa) {
	    C x;
	    if (WordStrAsNumber(s, &x)) {
	    	// perform literal number.
		Ds -= S;
		Put(Ds, x);
		D(stderr, " Literal(%lld) ", (long long) x);
		return;
	    }
	    FatalS("No such word", s);
	    return;
	}
	C op = Get(cfa);
	D(stderr, " ExecuteWordStr(%s)@%lld(op%d) ", s, (long long) cfa, op);

	PushR(0);
	PushR(cfa);
	Ip = Rs;
	PushR(0);
	C rs = Rs;
	//fprintf(stderr, "\nOp(%d) << Rs=%d Ds=%d Ip=%d\n", op, Rs, Ds, Ip);

	Loop();

	//fprintf(stderr, "\n       >> Rs=%d Ds=%d Ip=%d\n", Rs, Ds, Ip);
	CheckEq(__LINE__, rs,  Rs);
	CheckEq(__LINE__, Ip, rs+2*S);
	C r0 = PopR();
	CheckEq(__LINE__, r0, 0);
	C r1 = PopR();
	CheckEq(__LINE__, r1, cfa);
	C r2 = PopR();
	CheckEq(__LINE__, r2, 0);
    }

    C Allot(int n) {
    	C z = Get(HerePtr);
	Put(HerePtr, z + n);
	fprintf(stderr, "Allot(%d) : Here %d -> Here %d\n", n, z, Get(HerePtr));
	return z;
    }

    void Init() {
	Mem = new char[MemLen];
	memset(Mem, 0, MemLen);

	C ptr = LINELEN;
	HerePtr = ptr;
	ptr += S;
	LatestPtr = ptr;
	ptr += S;
	StatePtr = ptr;
	ptr += S;
	Put(HerePtr, ptr);
	Put(LatestPtr, 0);
	Put(StatePtr, 0);

	R0 = Rs = MemLen - 32;
	D0 = Ds = MemLen - 32 - 1024;

	CreateWord("+", PLUS);
	CreateWord(".", DOT);
	CreateWord("dup", DUP);
	CreateWord("drop", DROP);
	CreateWord("(lit)", _LIT_);
	CreateWord("(enter)", _ENTER_);
	CreateWord("(exit)", _EXIT_);
	CreateWord(":", COLON);
    }

    void Run() {
	const char *s;
	do {
	    auto cp = memset(Mem, 0, LINELEN);
	    fprintf(stderr, " ok ");
	    if (cp == nullptr) {
		break;
	    }
	    LineIndex = 0;
	    while (s = NextWordInput()) {
		D(stderr, "Word(%s) ", s);
		ExecuteWordStr(s);
	    }
	} while(s);
    }

    void ShutDown() {
	delete Mem;
    }

    int Main(int argc, const char *argv[]) {
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

	for( int i = 0; i < argc; i++) {
		fprintf(stderr, "[Forth input from %s]\n", argv[i]);
		ForthInputFile = fopen(argv[i], "r");
		if (!ForthInputFile) {
			FatalS("cannot open input file", argv[i]);
		}
		Run();
		fclose(ForthInputFile);
	}
	fprintf(stderr, "[Forth input from stdin]\n");
	ForthInputFile = stdin;
	Run();
	ShutDown();
    }
}				// namespace forth_yak

int main(int argc, const char *argv[])
{
    return forth_yak::Main(argc, argv);
}
