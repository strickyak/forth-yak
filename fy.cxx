#include "fy.h"

#include <unistd.h>

#include <map>
#include <string>

using std::map;
using std::string;

const char *Argv0;
bool QuitAfterSlurping;

char *Mem;
int MemLen;

U Ds;                           // data stack ptr
U Ds0;                          // data stack ptr base
U Rs;                           // return stack ptr
U Rs0;                          // return stack ptr base
U Ip;                           // instruction ptr
U W;                            // W register

U HerePtr;                      // points to Here variable
U LatestPtr;                    // points to Latest variable
U StatePtr;                     // points to State variable

int Debug;

map < U, string > link_map, cfa_map, dfa_map;   // Just for debugging.

InputKey input_key;

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
       (ULL) Rs, (ULL) Ds, (ULL) Ip, (ULL) Get(HerePtr), (ULL) Get(LatestPtr), (ULL) Get(StatePtr));

  U rSize = (Rs0 - Rs) / S;
  printf("  R [%llx] : ", (ULL) rSize);
  for (int i = 0; i < rSize && i < 50; i++) {
    SmartPrintNum(Get(Rs0 - (i + 1) * S));
  }
  putchar('\n');
  U dSize = (Ds0 - Ds) / S;
  printf("  D [%llx] : ", (ULL) dSize);
  for (int i = 0; i < dSize && i < 50; i++) {
    SmartPrintNum(Get(Ds0 - (i + 1) * S));
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

    printf("%c[%4d=%04x] ", (j == expect) ? ' ' : '=', j, j);
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
    fprintf(stderr, "*** CheckEq Fails: line %d: %llx != %llx\n", line, (ULL) a, (ULL) b);
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

int Fatality;

void Fatal(const char *msg)
{
  ++Fatality;
  fprintf(stderr, " *** %s: Fatal: %s\n", Argv0, msg);
  if (Fatality < 2)
    DumpMem(true);
  assert(0);
}

void FatalU(const char *msg, int x)
{
  ++Fatality;
  fprintf(stderr, " *** %s: FatalU: %s [0x%llx]\n", Argv0, msg, (ULL) x);
  if (Fatality < 2)
    DumpMem(true);
  assert(0);
}

void FatalI(const char *msg, int x)
{
  ++Fatality;
  fprintf(stderr, " *** %s: FatalI: %s [%d]\n", Argv0, msg, x);
  if (Fatality < 2)
    DumpMem(true);
  assert(0);
}

void FatalS(const char *msg, const char *s)
{
  ++Fatality;
  if (Fatality < 2)
    fprintf(stderr, " *** %s: FatalS: %s `%s`\n", Argv0, msg, s);
  if (Fatality < 2)
    DumpMem(true);
  assert(0);
}

void InputKey::Init(const char *text, int filec, const char *filev[], bool add_stdin)
{
  text_ = text;
  filec_ = filec;
  filev_ = filev;
  add_stdin_ = add_stdin;
  current_ = nullptr;
  isatty_ = false;
  next_ok_ = false;
  Advance();
}

void InputKey::Advance()
{
  if (current_ == stdin) {
    current_ = nullptr;
    return;
  }
  if (current_) {
    // Close current file after it is done.
    fclose(current_);
  }
  if (filec_ == 0) {
    if (add_stdin_) {
      current_ = stdin;
      isatty_ = (isatty(0) == 1);
      fflush(stdout);
      fprintf(stderr, " ok ");
    } else {
      current_ = nullptr;
    }
    return;
  }
  current_ = fopen(filev_[0], "r");
  if (!current_) {
    FatalS("cannot open input file", filev_[0]);
  }
  isatty_ = false;
  --filec_, ++filev_;
}

U InputKey::Key()
{
  fflush(stdout);
  if (*text_) {
    return *(const unsigned char *) (text_++);
  }
  while (true) {
    fflush(stdout);
    if (!current_) {
      fprintf(stderr, "  *EOF*  \n");
      exit(0);
    }
    if (next_ok_) {
      fprintf(stderr, " ok ");
      next_ok_ = false;
    }
    int ch = fgetc(current_);
    if (ch == '\n' && isatty_) {
      next_ok_ = true;
    }
    if (ch != EOF) {
      // fprintf(stderr, "<%c=%d>", ch, ch);
      return (U) ch;
    }
    Advance();
  }
}

void Key()
{
  Push(input_key.Key());
}

U PopNewKeyCheckEOF()
{
  Key();
  U c = Pop();
  if (c & 256) {                // If EOF
    fprintf(stderr, "<<<<< Exiting on EOF >>>>>\n");
    exit(0);
  }
  D(stderr, "[char:%02x]", c);
  return c;
}

void Word()
{
  U c = PopNewKeyCheckEOF();
  while (c <= 32) {             // Skip white space (control chars are white space).
    c = PopNewKeyCheckEOF();
  }
  int i = 0;
  while (c > 32) {
    if (i > 31) {
      FatalI("Input word too big", i);
    }
    i++;
    Mem[i] = c;                 // Word starts at Mem[1]
    c = PopNewKeyCheckEOF();
  }
  Mem[i + 1] = 0;               // null-terminate the word.
  Mem[0] = i;                   // Save count at Mem[0]
  Push(1);                      // Pointer to word chars.
  Push(i);                      // Length.
  D(stderr, "<<<Word: %s>>>\n", Mem + 1);
}

char *WordStr()
{
  Word();
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
  fprintf(stderr, " Comma(%llx): HerePtr=%llx HERE=%llx\n", (ULL) x, (ULL) HerePtr, (ULL) here);
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

void Words()
{
  U ptr = Get(LatestPtr);
  while (ptr) {
    B flags = Mem[ptr + S];
    if (flags & HIDDEN_BIT)
      continue;
    char *name = &Mem[ptr + S + 1];     // name follows link and lenth/flags byte.
    printf(" %s", name);
    ptr = Get(ptr);
  }
}

U LookupCfa(const char *s, B * flags_out = nullptr)
{
  U ptr = Get(LatestPtr);
  while (ptr) {
    B flags = Mem[ptr + S];
    if (flags & HIDDEN_BIT)
      continue;
    char *name = &Mem[ptr + S + 1];     // name follows link and lenth/flags byte.
    D(stderr, "LookupCfa(%s) trying ptr=%llx name=<%s>", s, (ULL) ptr, name);
    if (strcaseeq(s, name)) {
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
      Fatal("cannot use `:` when already compiling");
  }
  char *name = WordStr();
  if (!name) {
    Fatal("EOF during colon definition");
  }

  D(stderr, "COLON name <%s>\n", name);
  CreateWord(name, _ENTER_);
  Put(StatePtr, 1);             // Compiling state.
  while (1) {
    char *word = WordStr();
    if (!word) {
      Fatal("EOF during colon definition");
    }
    D(stderr, "COLON word <%s>\n", word);
    if (strcaseeq(word, ";")) {
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
      U here = Get(HerePtr);
      fprintf(stderr, " :Terp: Ip:%llx Op:%llx W:%llx; Rs=%llx Ds=%llx; here=%llx %s\n", (ULL) Ip, (ULL) op, (ULL) W,
              (ULL) Rs, (ULL) Ds, (ULL) here, compiling ? "compiling" : "");
    }
    Ip += S;

    switch (op) {
    case _END:
      return;
      break;
    case _PLUS:
      DropPoke(Peek(1) + Peek());       // Unsigned should be same as signed 2's complement.
      break;
    case _DOT:{
        U x = Pop();
        printf("%lld. ", (long long) C(x));
        fflush(stdout);
      }
      break;
    case CR:
      putchar('\n');
      fflush(stdout);
      break;
    case DUP:{
        Push(Peek());
      }
      break;
    case DROP:{
        Ds += S;
      }
      break;
    case _2DUP:{
        Push(Peek(1));
        Push(Peek(1));
      }
      break;
    case _2DROP:{
        Ds += 2 * S;
      }
      break;
    case SWAP:{
        U x = Peek();
        U y = Peek(1);
        Poke(y, 0);
        Poke(x, 1);
      }
      break;
    case OVER:{
        Push(Peek(1));
      }
      break;
    case GT_R:{
        PushR(Pop());
      }
      break;
    case R_GT:{
        Push(PopR());
      }
      break;
    case I:{
        Push(Get(Rs));
      }
      break;
    case J:{
        Push(Get(Rs + 2 * S));
      }
      break;
    case K:{
        Push(Get(Rs + 4 * S));
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
          Fatal("cannot use `;` when not compiling");
      }
      Comma(LookupCfa("(exit)"));
      Put(StatePtr, 0);         // Interpreting state.
      break;
    case _COLON:
      // ColonDefinition();
      {
        U compiling = Get(StatePtr);
        if (compiling)
          Fatal("cannot use `:` when already compiling");
        char *name = WordStr();
        CreateWord(name, _ENTER_);
        Put(StatePtr, 1);       // Compiling state.
      }
      break;
    case ALIGN:
      Poke(Aligned(Peek()));
      break;
    case _MINUS:
      DropPoke(Peek(1) - Peek());       // Unsigned should be same as signed 2's complement.
      break;
    case _TIMES:
      fprintf(stderr, "TIMES: %d %d %d", (C) Peek(1), (C) Peek(), (C) Peek(1) * (C) Peek());
      DropPoke(U((C) Peek(1) * (C) Peek()));
      break;
    case _DIVIDE:
      fprintf(stderr, "DIV: %d %d %d", (C) Peek(1), (C) Peek(), (C) Peek(1) / (C) Peek());
      DropPoke(U((C) Peek(1) / (C) Peek()));
      break;
    case MOD:
      fprintf(stderr, "MOD: %d %d %d", (C) Peek(1), (C) Peek(), (C) Peek(1) % (C) Peek());
      DropPoke(U((C) Peek(1) % (C) Peek()));
      break;
    case _EQ:
      DropPoke(Peek(1) == Peek());
      break;
    case _NE:
      DropPoke(Peek(1) != Peek());
      break;
    case _LT:
      DropPoke(Peek(1) < Peek());
      break;
    case _LE:
      DropPoke(Peek(1) <= Peek());
      break;
    case _GT:
      DropPoke(Peek(1) > Peek());
      break;
    case _GE:
      DropPoke(Peek(1) >= Peek());
      break;
    case DUMPMEM:
      DumpMem(true);
      break;
    case WORDS:
      Words();
      break;
    case R0:
      Push(Rs0);
      break;
    case S0:
      Push(Ds0);
      break;
    case MUST:
      if (Pop() == 0) {
        DumpMem(true);
        fprintf(stderr, " *** MUST failed\n");
        assert(0);
      }
      break;
    case IMMEDIATE:
      Mem[Get(LatestPtr) + S] ^= IMMEDIATE_BIT;
      break;
    case HIDDEN:
      Mem[Pop() + S] ^= HIDDEN_BIT;
      break;
    case KEY:
      Key();
      break;
    case WORD:
      Word();
      break;
    case HERE:
      Push(Get(HerePtr));
      break;
    case _TICK:{
        char *word = WordStr();
        assert(word);
        U cfa = LookupCfa(word);
        assert(cfa);
        Push(cfa);
        fprintf(stderr, "_TICK: word=`%s` cfa=%d\n", word, cfa);
      }
      break;
    case _COMMA:
      Comma(Pop());
      break;
    case DO:{
        U swap = CheckU(LookupCfa("swap"));
        Comma(swap);
        U onto_r = CheckU(LookupCfa(">r"));
        Comma(onto_r);
        Comma(onto_r);
        Push(Get(HerePtr));     // Jump back target.
        Push(0);                // No repair.
      }
      break;
    case _DO:{                 // ?DO
        U swap = CheckU(LookupCfa("swap"));
        Comma(swap);
        U onto_r = CheckU(LookupCfa(">r"));
        Comma(onto_r);
        Comma(onto_r);
        U branch = CheckU(LookupCfa("branch"));
        Comma(branch);
        U repair = Get(HerePtr);
        Comma(0);
        Push(Get(HerePtr));     // Jump back target is after the `branch`.
        Push(repair);
      }
      break;
    case _INCR_I_:
      ++Mem[Rs];
      break;
    case _LOOP_:{
        U count = Mem[Rs];
        U limit = Mem[Rs + S];

        if (count < limit) {
          Ip += Get(Ip);        // add offset to Ip.
        } else {
          Ip += S;              // skip over offset.
          Rs += 2 * S;          // pop count & limit from Return stack.
        }
      }
      break;
    case LOOP:{
        U repair = Pop();
        U back = Pop();
        Comma(CheckU(LookupCfa("(incr_i)")));
        if (repair) {
          // Repair ?DO target to jump after (incr_i) and before (loop).
          Put(repair, Get(HerePtr) - repair);
        }
        Comma(CheckU(LookupCfa("(loop)")));
        Comma(back - Get(HerePtr));
      }
      break;
    case LEAVE:
      FatalI("TODO", op);
      break;
    case UNLOOP:
      FatalI("TODO", op);
      break;
    case IF:{
        U compiling = Get(StatePtr);
        if (!compiling)
          Fatal("cannot use IF unless compiling");
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
        FatalI("Bad op", op);
      }
      break;
    }                           // switch (op)
  }                             // while(true)
}                               // void Loop()

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
  fprintf(stderr, "Allot(%llx) : Here %llx -> Here %llx\n", (ULL) n, (ULL) z, (ULL) Get(HerePtr));
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

  Rs0 = Rs = MemLen - S;        // Waste top word.
  Ds0 = Ds = MemLen - S - 128 * S;      // 128 slots on Return Stack.
  Put(Rs0, 0xEEEE);             // Debugging mark.
  Put(Ds0, 0xEEEE);             // Debugging mark.

  CreateWord("+", _PLUS);
  CreateWord(".", _DOT);
  CreateWord("cr", CR);
  CreateWord("dup", DUP);
  CreateWord("drop", DROP);
  CreateWord("2dup", _2DUP);
  CreateWord("2drop", _2DROP);
  CreateWord("swap", SWAP);
  CreateWord("over", OVER);
  CreateWord(">r", GT_R);
  CreateWord("r>", R_GT);
  CreateWord("i", I);
  CreateWord("j", J);
  CreateWord("k", K);
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
  CreateWord("==", _EQ);
  CreateWord("!=", _NE);
  CreateWord("<", _LT);
  CreateWord("<=", _LE);
  CreateWord(">", _GT);
  CreateWord(">=", _GE);
  CreateWord("dumpmem", DUMPMEM);
  CreateWord("key", KEY);
  CreateWord("word", WORD);
  CreateWord("here", HERE);
  CreateWord("words", WORDS);
  CreateWord("'", _TICK, IMMEDIATE_BIT);
  CreateWord("r0", R0);
  CreateWord("s0", S0);
  CreateWord("must", MUST);
  CreateWord("immediate", IMMEDIATE, IMMEDIATE_BIT);
  CreateWord(",", _COMMA);
  CreateWord("hidden", HIDDEN);
  CreateWord("branch", BRANCH);
  CreateWord("0branch", BRANCH0);
  CreateWord("do", DO, IMMEDIATE_BIT);
  CreateWord("?do", _DO, IMMEDIATE_BIT);
  CreateWord("(incr_i)", _INCR_I_);
  CreateWord("(loop)", _LOOP_);
  CreateWord("loop", LOOP, IMMEDIATE_BIT);
  CreateWord("leave", LEAVE, IMMEDIATE_BIT);
  CreateWord("unloop", UNLOOP, IMMEDIATE_BIT);
  CreateWord("if", IF, IMMEDIATE_BIT);
  CreateWord("else", ELSE, IMMEDIATE_BIT);
  CreateWord("then", THEN, IMMEDIATE_BIT);
  CreateWord("nop", NOP);
}

void Interpret1()
{
  const char *word = WordStr();
  B flags = 0;
  U cfa = LookupCfa(word, &flags);
  U compiling = Get(StatePtr);
  fprintf(stderr, "Interpret1: word=`%s` flags=%d cfa=%d , %s\n", word, flags, cfa, compiling ? "compiling" : "");
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

void PrintIntSizes()
{
  printf("short %d\n", sizeof(short));
  printf("int %d\n", sizeof(int));
  printf("long %d\n", sizeof(long));
  printf("long long %d\n", sizeof(long long));
  printf("-42 => char %d\n", (int) (char) (-42));
  printf("-42 => unsigned char %d\n", (int) (unsigned char) (-42));
}

void Main(int argc, const char *argv[])
{
  MemLen = 0x10000;             // Default: 64 kib RAM.

  Argv0 = argv[0];
  ++argv, --argc;
  const char *text = "";
  bool interactive = false;
  while (argc > 0 && argv[0][0] == '-') {
    switch (argv[0][1]) {
    case 'm':
      MemLen = atoi(&argv[0][2]);
      break;
    case 'd':
      Debug = atoi(&argv[0][2]);
      break;
    case 'S':
      PrintIntSizes();
      break;
    case 'i':
      interactive = true;
      break;
    case 'c':
      text = &argv[0][2];
      break;
    default:
      FatalS("Bad flag", argv[0]);
    }
    ++argv, --argc;
  }

  if (!interactive) {
    interactive = (argc == 0) && (!*text);
  }
  input_key.Init(text, argc, argv, interactive);
  Init();
  Interpret();
}

void Test()
{
  Init();
}

int main(int argc, const char *argv[])
{
#ifdef TEST
  Test();
#else
  Main(argc, argv);
#endif
}
