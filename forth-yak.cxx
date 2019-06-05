#include "forth-yak.h"

#include <map>
#include <string>

#define NEW 1

using std::map;
using std::string;

const char *Argv0;
FILE *ForthInputFile;
bool QuitAfterSlurping;

char *Mem;
int MemLen;

U Ds;                           // data stack ptr
U D0;                           // data stack ptr base
U Rs;                           // return stack ptr
U R0;                           // return stack ptr base
U Ip;                           // instruction ptr
U W;                            // W register

U HerePtr;                      // points to Here variable
U LatestPtr;                    // points to Latest variable
U StatePtr;                     // points to State variable

int Debug;

map < U, string > link_map, cfa_map, dfa_map;   // Just for debugging.

void SmartPrintNum(U x, FILE * fd = stdout)
{
  if (link_map.find(x) != link_map.end()) {
    fprintf(fd, "  L{%s}", link_map[x].c_str());
  } else if (cfa_map.find(x) != cfa_map.end()) {
    fprintf(fd, "  U{%s}", cfa_map[x].c_str());
  } else if (dfa_map.find(x) != dfa_map.end()) {
    fprintf(fd, "  D{%s}", dfa_map[x].c_str());
  } else if (-MemLen <= x && x <= MemLen) {
    fprintf(fd, "  %llx", (ULL) x);
  } else {
    fprintf(fd, "  ---");
  }
};

void DumpMem(bool force = false)
{
  if (!force && !Debug)
    return;
  printf
      ("Dump: Rs=%llx  Ds=%llx  Ip=%llx  HERE=%llx LATEST=%llx STATE=%llx {\n",
       (ULL) Rs, (ULL) Ds, (ULL) Ip, (ULL) Get(HerePtr),
       (ULL) Get(LatestPtr), (ULL) Get(StatePtr));

  U rSize = (R0 - Rs) / S;
  printf("  R [%llx] : ", (ULL) rSize);
  for (int i = 0; i < rSize && i < 50; i++) {
    SmartPrintNum(Get(R0 - (i + 1) * S));
  }
  putchar('\n');
  U dSize = (D0 - Ds) / S;
  printf("  D [%llx] : ", (ULL) dSize);
  for (int i = 0; i < dSize && i < 50; i++) {
    SmartPrintNum(Get(D0 - (i + 1) * S));
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
  fflush(stdout);
}

void CheckEq(int line, U a, U b)
{
  if (a != b) {
    fprintf(stderr, "*** CheckEq Fails: line %d: %llx != %llx\n", line,
            (ULL) a, (ULL) b);
    DumpMem(true);
    assert(0);
  }
}

U CheckU(U x)
{
  if (!x) {
    fprintf(stderr, "*** CheckU Fails\n");
    DumpMem(true);
    assert(x);
  }
  return x;
}

void Fatal(const char *msg, int x)
{
  fprintf(stderr, " *** %s: Fatal: %s [%d]\n", Argv0, msg, x);
  DumpMem(true);
  assert(0);
}

void FatalS(const char *msg, const char *s)
{
  fprintf(stderr, " *** %s: Fatal: %s `%s`\n", Argv0, msg, s);
  DumpMem(true);
  assert(0);
}

void NewKey()
{
  Push((U) getchar());
}

U PopNewKeyCheckEOF()
{
  NewKey();
  U c = Pop();
  if (c & 256) {                // If EOF
    fprintf(stderr, "<<<<< Exiting on EOF >>>>>\n");
    exit(0);
  }
  D(stderr, "[char:%02x]", c);
  return c;
}

void NewWord()
{
  U c = PopNewKeyCheckEOF();
  while (c <= 32) {             // Skip white space (control chars are white space).
    c = PopNewKeyCheckEOF();
  }
  int i = 0;
  while (c > 32) {
    if (i > 31) {
      Fatal("Input word too big", i);
    }
    i++;
    Mem[i] = c;                 // Word starts at Mem[1]
    c = PopNewKeyCheckEOF();
  }
  Mem[i + 1] = 0;               // null-terminate the word.
  Mem[0] = i;                   // Save count at Mem[0]
  Push(1);                      // Pointer to word chars.
  Push(i);                      // Length.
  D(stderr, "<<<NewWord: %s>>>\n", Mem + 1);
}

char *NewWordStr()
{
  NewWord();
  Pop();                        // pop length
  U wordIndex = Pop();          // pop word; should be 1.
  fprintf(stderr, " >>%s<< ", &Mem[wordIndex]);
  return &Mem[wordIndex];
}

void CreateWord(const char *name, Opcode code, B flags = 0)
{
  if (strlen(name) > LEN_MASK) {
    FatalS("Creating word with name too long: `%s`", name);
  }
  U latest = Get(LatestPtr);
  U here = Get(HerePtr);
  fprintf(stderr,
          "CreateWord(%s, %llx): HerePtr=%llx HERE=%llx  latest=%llx\n",
          name, (ULL) code, (ULL) HerePtr, (ULL) here, (ULL) latest);
  Put(LatestPtr, here);
  link_map[here] = name;

  Put(here, latest);
  here += S;
  Mem[here] = B(strlen(name)) | flags;
  strcpy(&Mem[here + 1], name);
  here += 1 + strlen(name) + 1; // 1 for len and 1 for '\0'.

  U there = Aligned(here);
  while (here < there) {
    Mem[here++] = 0xEE;         // 0xEE for debugging.
  }

  Put(here, code);
  here += S;
  Put(HerePtr, here);

  cfa_map[there] = name;
  dfa_map[here] = name;
}

void Comma(U x)
{
  U here = Get(HerePtr);
  SmartPrintNum(x, stderr);
  fprintf(stderr, " Comma(%llx): HerePtr=%llx HERE=%llx\n", (ULL) x,
          (ULL) HerePtr, (ULL) here);
  Put(here, x);
  Put(HerePtr, here + S);
}

bool WordStrAsNumber(const char *s, U * out)
{
  U z = 0;
  U sign = 1;
  int base = 10;                // TODO hex
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

U LookupCfa(const char *s, B * flags_out = nullptr)
{
  U ptr = Get(LatestPtr);
  while (ptr) {
    B flags = Mem[ptr + S];
    if (flags & HIDDEN_BIT)
      continue;
    char *name = &Mem[ptr + S + 1];     // name follows link and lenth/flags byte.
    D(stderr, "LookupCfa(%s) trying ptr=%llx name=<%s>", s, (ULL) ptr,
      name);
    if (streq(s, name)) {
      // code addr follows name and '\0' and alignment.
      if (flags_out)
        *flags_out = flags;
      return Aligned(ptr + S + 1 + strlen(name) + 1);
    }
    ptr = Get(ptr);
  }
  return 0;
}

void ColonDefinition()
{
  {
    U compiling = Get(StatePtr);
    if (compiling)
      Fatal("cannot use `:` when already compiling", 0);
  }
#ifdef NEW
  char *name = NewWordStr();
#else
  char *name = NextWordInput();
#endif
  if (!name) {
    Fatal("EOF during colon definition", 0);
  }

  D(stderr, "COLON name <%s>\n", name);
  CreateWord(name, _ENTER_);
  Put(StatePtr, 1);             // Compiling state.
  while (1) {
#ifdef NEW
    char *word = NewWordStr();
#else
    char *word = NextWordInput();
#endif
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
  Put(StatePtr, 0);             // Interpreting state.
}

void Loop()
{
  while (true) {
    U cfa = Get(Ip);
    if (!cfa) {
      D(stderr, "Got cfa 0; exiting Loop.\n");
      break;
    }
    U op = Get(cfa);
    W = cfa + S;
    {
      SmartPrintNum(cfa, stderr);
      U compiling = Get(StatePtr);
      fprintf(stderr, "LOOP Ip:%llx Op:%llx W:%llx , %s\n", (ULL) Ip,
              (ULL) op, (ULL) W, compiling ? "compiling" : "");
    }
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
    case _SEMICOLON:
      {
        U compiling = Get(StatePtr);
        if (!compiling)
          Fatal("cannot use `;` when not compiling", 0);
      }
      Comma(LookupCfa("(exit)"));
      Put(StatePtr, 0);         // Interpreting state.
      break;
    case _COLON:
      // ColonDefinition();
      {
        U compiling = Get(StatePtr);
        if (compiling)
          Fatal("cannot use `:` when already compiling", 0);
        char *name = NewWordStr();
        CreateWord(name, _ENTER_);
        Put(StatePtr, 1);       // Compiling state.
      }
      break;
    case ALIGN:
      Poke(Aligned(Peek()));
      break;
    case _MINUS:
      DropPoke(Peek() - Peek(1));
      break;
    case _TIMES:
      DropPoke(Peek() * Peek(1));
      break;
    case _DIVIDE:
      DropPoke(Peek() / Peek(1));
      break;
    case MOD:
      DropPoke(Peek() % Peek(1));
      break;
    case _EQ:
      DropPoke(Peek() == Peek(1));
      break;
    case _NE:
      DropPoke(Peek() != Peek(1));
      break;
    case DUMPMEM:
      DumpMem(true);
      break;
    case MUST:
      if (Pop() == 0) {
        DumpMem(true);
      }
      break;
    case IMMEDIATE:
      Mem[Get(LatestPtr) + S] ^= IMMEDIATE_BIT;
      break;
    case HIDDEN:
      Mem[Pop() + S] ^= HIDDEN_BIT;
      break;
    case KEY:
      NewKey();
      break;
    case WORD:
      NewWord();
      break;
    case DO:
      Fatal("TODO", op);
      break;
    case _DO:
      Fatal("TODO", op);
      break;
    case LOOP:
      Fatal("TODO", op);
      break;
    case UNLOOP:
      Fatal("TODO", op);
      break;
    case IF:{
        U compiling = Get(StatePtr);
        if (!compiling)
          Fatal("cannot use IF unless compiling", 0);
        Comma(LookupCfa("0branch"));
        Push(Get(HerePtr));     // Push position of EEEE to repair.
        Comma(0xEEEE);
      }
      break;
    case ELSE:{
        U repair = Pop();
        Comma(LookupCfa("branch"));
        Push(Get(HerePtr));     // Push position of EEEE to repair.
        Comma(0xEEEE);
        Put(repair, Get(HerePtr) - repair);
        Comma(LookupCfa("nop"));
      }
      break;
    case THEN:{
        U repair = Pop();
        Put(repair, Get(HerePtr) - repair);
        Comma(LookupCfa("nop"));
      }
      break;
    case BRANCH:
      Ip += Get(Ip);            // add offset to Ip.
      break;
    case BRANCH0:
      if (Pop() == 0) {
        Ip += Get(Ip);          // add offset to Ip.
      } else {
        Ip += S;                // skip over offset.
      }
      break;
    case NOP:
      break;
    default:{
        Fatal("Bad op", op);
      }
      break;
    }
  }
}

void ExecuteCfa(U cfa)
{
  PushR(0);
  PushR(cfa);
  Ip = Rs;

  Loop();

  U r1 = PopR();
  CheckEq(__LINE__, r1, cfa);
  U r2 = PopR();
  CheckEq(__LINE__, r2, 0);
}

void ExecuteWordStr(const char *s)
{
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

U Allot(int n)
{
  U z = Get(HerePtr);
  Put(HerePtr, z + n);
  fprintf(stderr, "Allot(%llx) : Here %llx -> Here %llx\n", (ULL) n,
          (ULL) z, (ULL) Get(HerePtr));
  return z;
}

void Init()
{
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
  CreateWord(";", _SEMICOLON, IMMEDIATE_BIT);
  CreateWord(":", _COLON);
  CreateWord("align", ALIGN);
  CreateWord("-", _MINUS);
  CreateWord("*", _TIMES);
  CreateWord("/", _DIVIDE);
  CreateWord("mod", MOD);
  CreateWord("=", _EQ);
  CreateWord("!=", _NE);
  CreateWord("dumpmem", DUMPMEM);
  CreateWord("must", MUST);
  CreateWord("immediate", IMMEDIATE, IMMEDIATE_BIT);
  CreateWord("hidden", HIDDEN);
  CreateWord("branch", BRANCH);
  CreateWord("0branch", BRANCH0);
  CreateWord("do", DO);
  CreateWord("?do", _DO);
  CreateWord("loop", LOOP);
  CreateWord("unloop", UNLOOP);
  CreateWord("if", IF, IMMEDIATE_BIT);
  CreateWord("else", ELSE, IMMEDIATE_BIT);
  CreateWord("then", THEN, IMMEDIATE_BIT);
  CreateWord("nop", NOP);
}

void Interpret1()
{
  const char *word = NewWordStr();
  B flags = 0;
  U cfa = LookupCfa(word, &flags);
  U compiling = Get(StatePtr);
  fprintf(stderr, "Interpret1: word=`%s` flags=%d cfa=%d , %s\n", word,
          flags, cfa, compiling ? "compiling" : "");
  if (cfa) {
    // Found a word.
    if (compiling) {
      if (flags & IMMEDIATE_BIT) {
        ExecuteCfa(cfa);
      } else {
        Comma(cfa);
      }
    } else {
      ExecuteCfa(cfa);
    }
  } else {
    // Word not found -- is it a literal?
    U x;
    if (WordStrAsNumber(word, &x)) {
      if (compiling) {
        Comma(CheckU(LookupCfa("(lit)")));
        Comma(x);
      } else {
        Push(x);                // immediately: push x on stack.
      }
    } else {
      FatalS("Word not found", word);
    }
  }
}

void Interpret()
{
  while (true) {
    Interpret1();
  }
}

void ShutDown()
{
  delete Mem;
}

void PrintIntSizes()
{
  printf("short %d\n", sizeof(short));
  printf("int %d\n", sizeof(int));
  printf("long %d\n", sizeof(long));
  printf("long long %d\n", sizeof(long long));
  printf("-42 => char %d\n", (int) (char) (-42));
  printf("-42 => unsigned char %d\n", (int) (unsigned char) (-42));
}

int main(int argc, const char *argv[])
{
  MemLen = CELLSIZE < 4 ? 0x10000 : 1000000;    // Default: a million bytes, unless 16-bit.

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

/*
    for (int i = 0; i < argc; i++) {
      fprintf(stderr, "[Forth input from %s]\n", argv[i]);
      ForthInputFile = fopen(argv[i], "r");
      if (!ForthInputFile) {
        FatalS("cannot open input file", argv[i]);
      }
      Run();
      fclose(ForthInputFile);
    }
*/
  if (!QuitAfterSlurping) {
    fprintf(stderr, "[Forth input from stdin]\n");
    ForthInputFile = stdin;
#ifdef NEW
    Interpret();
#else
    Run();
#endif
  }
  ShutDown();
}
