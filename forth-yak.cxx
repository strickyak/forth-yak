#include "forth-yak.h"

#include <map>
#include <string>

namespace forth_yak {

  using std::map;
  using std::string;

  const char *Argv0;
  FILE *ForthInputFile;
  bool QuitAfterSlurping;

  char *Mem;
  int MemLen;

  U Ds;                         // data stack ptr
  U D0;                         // data stack ptr base
  U Rs;                         // return stack ptr
  U R0;                         // return stack ptr base
  U Ip;                         // instruction ptr
  U W;                          // W register

  U HerePtr;                    // points to Here variable
  U LatestPtr;                  // points to Latest variable
  U StatePtr;                   // points to State variable

  int Debug;

   map < U, string > link_map, cfa_map, dfa_map;        // Just for debugging.

  void SmartPrintNum(U x) {
    if (link_map.find(x) != link_map.end()) {
      printf("  L{%s}", link_map[x].c_str());
    } else if (cfa_map.find(x) != cfa_map.end()) {
      printf("  U{%s}", cfa_map[x].c_str());
    } else if (dfa_map.find(x) != dfa_map.end()) {
      printf("  D{%s}", dfa_map[x].c_str());
    } else if (-MemLen <= x && x <= MemLen) {
      printf("  %llx", (ULL) x);
    } else {
      printf("  ---");
    }
  };

  void DumpMem() {
    if (!Debug)
      return;
    printf
        ("Dump: Rs=%llx  Ds=%llx  Ip=%llx  HERE=%llx LATEST=%llx STATE=%llx {\n",
         (ULL) Rs, (ULL) Ds, (ULL) Ip, (ULL) Get(HerePtr),
         (ULL) Get(LatestPtr), (ULL) Get(StatePtr));

    U rSize = (R0 - Rs) / S;
    printf("  R [%llx] : ", (ULL) rSize);
    for (int i = 0; i < rSize && i < 50; i++) {
      SmartPrintNum(Get(R0 - i * S));
    }
    putchar('\n');
    U dSize = (D0 - Ds) / S;
    printf("  D [%llx] : ", (ULL) dSize);
    for (int i = 0; i < dSize && i < 50; i++) {
      SmartPrintNum(Get(D0 - i * S));
    }
    putchar('\n');

    int expect = -1;
    for (int j = 0; j < MemLen; j += 16) {
      bool something = false;
      for (int i = j; i < j + 16; i++) {
        if (Mem[i])
          something = true;
      }
      if (!something)
        continue;

      printf("%c[%4d=%04x] ", (j == expect) ? ' ' : '*', j, j);
      expect = j + 16;

      for (int i = j; i < j + 16; i++) {
        char m = Mem[i];
        char c = (32 <= m && m <= 126) ? m : '~';
        putchar(c);
        if ((i % S) == (S - 1))
          putchar(' ');
      }
      putchar(' ');
      putchar(' ');
      for (int i = j; i < j + 16; i++) {
        char m = Mem[i];
        printf("%02x ", m & 255);
        if ((i % S) == (S - 1))
          putchar(' ');
      }
      for (int i = j; i < j + 16; i += S) {
        U x = Get(i);

        SmartPrintNum(x);

      }
      putchar('\n');
    }
    printf("}\n");
  }

  void CheckEq(int line, U a, U b) {
    if (a != b) {
      fprintf(stderr, "*** CheckEq Fails: line %d: %llx != %llx\n", line,
              (ULL) a, (ULL) b);
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

  void FatalS(const char *msg, const char *s) {
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
        char c = Mem[LineIndex];
        if (c <= 32) {
          Mem[LineIndex] = '\0';
          LineIndex++;

          D(stderr, "NextWordInLine -> <%s>\n", Mem + WordStart);
          return Mem + WordStart;
        } else if ('A' <= c && c <= 'Z') {
          Mem[LineIndex] = c - 'A' + 'a';       // convert to lower.
        }
      }
    }
    return nullptr;
  }

  char *NextWordInput() {
    while (1) {
      char *s = NextWordInLine();
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

  void CreateWord(const char *name, Opcode code) {
    U latest = Get(LatestPtr);
    U here = Get(HerePtr);
    fprintf(stderr,
            "CreateWord(%s, %llx): HerePtr=%llx HERE=%llx  latest=%llx\n",
            name, (ULL) code, (ULL) HerePtr, (ULL) here, (ULL) latest);
    Put(LatestPtr, here);
    link_map[here] = name;

    Put(here, latest);
    here += S;
    strcpy(Mem + here, name);
    here += strlen(name) + 1;

    U there = Aligned(here);
    while (here < there) {
      Mem[here++] = 0xEE;       // 0xEE for debugging.
    }

    Put(here, code);
    here += S;
    Put(HerePtr, here);

    cfa_map[there] = name;
    dfa_map[here] = name;
  }

  void Comma(U x) {
    U here = Get(HerePtr);
    fprintf(stderr, "Comma(%llx): HerePtr=%llx HERE=%llx\n", (ULL) x,
            (ULL) HerePtr, (ULL) here);
    Put(here, x);
    Put(HerePtr, here + S);
  }

  bool WordStrAsNumber(const char *s, U * out) {
    U z = 0;
    U sign = 1;
    int base = 10;              // TODO hex
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

  U LookupCfa(const char *s) {
    U ptr = Get(LatestPtr);
    while (ptr) {
      char *name = &Mem[ptr + S];       // name follows link.
      D(stderr, "LookupCfa(%s) trying ptr=%llx name=<%s>", s, (ULL) ptr,
        name);
      if (streq(s, name)) {
        // code addr follows name and '\0' and alignment.
        return Aligned(ptr + S + strlen(name) + 1);
      }
      ptr = Get(ptr);
    }
    return 0;
  }

  void ColonDefinition() {
    char *name = NextWordInput();
    if (!name) {
      Fatal("EOF during colon definition", 0);
    }

    D(stderr, "COLON name <%s>\n", name);
    CreateWord(name, _ENTER_);
    Put(StatePtr, 1);           // Compiling state.  // not yet used
    while (1) {
      char *word = NextWordInput();
      if (!word) {
        Fatal("EOF during colon definition", 0);
      }
      D(stderr, "COLON word <%s>\n", word);
      if (streq(word, ";")) {
        break;
      }

      D(stderr, "lookup up <%s>\n", word);
      U x = LookupCfa(word);
      D(stderr, "looked up <%s>\n", word);
      if (x) {
        Comma(x);
      } else {
        if (WordStrAsNumber(word, &x)) {
          U lit = LookupCfa("(lit)");
          assert(lit);
          Comma(lit);
          Comma(x);
        } else {
          FatalS("No such word", word);
        }
      }
    }
    Comma(LookupCfa("(exit)"));
    Put(StatePtr, 0);           // Interpreting state.  // not yet used
  }

  void Loop() {
    while (true) {
      U cfa = Get(Ip);
      if (!cfa) {
        D(stderr, "Got cfa 0; exiting Loop.\n");
        break;
      }
      U op = Get(cfa);
      W = cfa + S;
      fprintf(stderr, "Ip:%llx W:%llx Op:%llx\n", (ULL) Ip, (ULL) op,
              (ULL) W);
      Ip += S;

      switch (op) {
      case _END:
        return;
        break;
      case _PLUS:{
          U x = Pop();
          U y = Peek();
          Poke(x + y);
        }
        break;
      case _DOT:{
          U x = Pop();
          printf("%lld. ", (long long) x);
          fflush(stdout);
        }
      case DUP:{
          Push(Peek());
        }
        break;
      case DROP:{
          Pop();
        }
        break;
      case _LIT_:{
          Push(Get(Ip));
          Ip += S;
        }
        break;
      case _ENTER_:{
          PushR(Ip);
          Ip = W;
        }
        break;
      case _EXIT_:{
          Ip = PopR();
        }
        break;
      case _COLON:
        ColonDefinition();
        break;
      case ALIGN:
        Poke(Aligned(Peek()));
        break;
      case _MINUS:
        DropPoke(Peek() - Peek(S));
        break;
      case _TIMES:
        DropPoke(Peek() * Peek(S));
        break;
      case _DIVIDE:
        DropPoke(Peek() / Peek(S));
        break;
      case MOD:
        DropPoke(Peek() % Peek(S));
        break;
      case _EQ:
        DropPoke(Peek() == Peek(S));
        break;
      case _NE:
        DropPoke(Peek() != Peek(S));
        break;
      case DO:
      case _DO:
      case LOOP:
      case UNLOOP:
      case IF:
      case ELSE:
      case THEN:
      case BRANCH:
      case BRANCH0:
      default:{
          Fatal("Bad op", op);
        }
        break;
      }
    }
  }

  void ExecuteWordStr(const char *s) {
    U cfa = LookupCfa(s);
    if (!cfa) {
      U x;
      if (WordStrAsNumber(s, &x)) {
        // perform literal number.
        Ds -= S;
        Put(Ds, x);
        D(stderr, " Literal(%llx) ", (ULL) x);
        return;
      }
      FatalS("No such word", s);
      return;
    }
    U op = Get(cfa);
    D(stderr, " ExecuteWordStr(%s)@%llx(op%llx) ", s, (ULL) cfa, (ULL) op);

    PushR(0);
    PushR(cfa);
    Ip = Rs;
    PushR(0);
    U rs = Rs;

    Loop();

    CheckEq(__LINE__, rs, Rs);
    CheckEq(__LINE__, Ip, rs + 2 * S);
    U r0 = PopR();
    CheckEq(__LINE__, r0, 0);
    U r1 = PopR();
    CheckEq(__LINE__, r1, cfa);
    U r2 = PopR();
    CheckEq(__LINE__, r2, 0);
  }

  U Allot(int n) {
    U z = Get(HerePtr);
    Put(HerePtr, z + n);
    fprintf(stderr, "Allot(%llx) : Here %llx -> Here %llx\n", (ULL) n,
            (ULL) z, (ULL) Get(HerePtr));
    return z;
  }

  void Init() {
    Mem = new char[MemLen];
    memset(Mem, 0, MemLen);

    U ptr = LINELEN;
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

    CreateWord("+", _PLUS);
    CreateWord(".", _DOT);
    CreateWord("dup", DUP);
    CreateWord("drop", DROP);
    CreateWord("(lit)", _LIT_);
    CreateWord("(enter)", _ENTER_);
    CreateWord("(exit)", _EXIT_);
    CreateWord(":", _COLON);
    CreateWord("align", ALIGN);
    CreateWord("-", _MINUS);
    CreateWord("*", _TIMES);
    CreateWord("/", _DIVIDE);
    CreateWord("mod", MOD);
    CreateWord("=", _EQ);
    CreateWord("!=", _NE);
    CreateWord("do", DO);
    CreateWord("?do", _DO);
    CreateWord("loop", LOOP);
    CreateWord("unloop", UNLOOP);
    CreateWord("if", IF);
    CreateWord("else", ELSE);
    CreateWord("then", THEN);
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
    } while (s);
  }

  void ShutDown() {
    delete Mem;
  }

  void PrintIntSizes() {
    printf("short %d\n", sizeof(short));
    printf("int %d\n", sizeof(int));
    printf("long %d\n", sizeof(long));
    printf("long long %d\n", sizeof(long long));
    printf("-42 => char %d\n", (int) (char) (-42));
    printf("-42 => unsigned char %d\n", (int) (unsigned char) (-42));
  }

  int Main(int argc, const char *argv[]) {
    MemLen = 1000000;           // Default: a million bytes.

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
      case 'q':
        QuitAfterSlurping = true;
        break;
      case 'S':
        PrintIntSizes();
        break;
      default:
        Fatal("Bad flag", argv[0][1]);
      }
      ++argv, --argc;
    }
    Init();

    for (int i = 0; i < argc; i++) {
      fprintf(stderr, "[Forth input from %s]\n", argv[i]);
      ForthInputFile = fopen(argv[i], "r");
      if (!ForthInputFile) {
        FatalS("cannot open input file", argv[i]);
      }
      Run();
      fclose(ForthInputFile);
    }
    if (!QuitAfterSlurping) {
      fprintf(stderr, "[Forth input from stdin]\n");
      ForthInputFile = stdin;
      Run();
    }
    ShutDown();
  }
}                               // namespace forth_yak

int main(int argc, const char *argv[])
{
  return forth_yak::Main(argc, argv);
}
