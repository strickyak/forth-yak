#define LINENOISE 1

#include "fy.h"
#include "vendor/linenoise/linenoise.h"

#include <unistd.h>

#include <map>
#include <string>

using std::map;
using std::string;

const char *Argv0;
bool QuitAfterSlurping;

char Mem[MEMLEN];

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
int MustOk;

map < U, string > link_map, cfa_map, dfa_map;   // Just for debugging.

InputKey input_key;

void SmartPrintNum(U u, FILE * fd = stdout)
{
  C x = (C) u;
  if (link_map.find(u) != link_map.end()) {
    fprintf(fd, "  L{%s}", link_map[x].c_str());
  } else if (cfa_map.find(u) != cfa_map.end()) {
    fprintf(fd, "  U{%s}", cfa_map[x].c_str());
  } else if (dfa_map.find(u) != dfa_map.end()) {
    fprintf(fd, "  D{%s}", dfa_map[x].c_str());
  } else if (-2 * MemLen <= x && x <= 2 * MemLen) {
    fprintf(fd, "  %llx", (ULL) u);
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
#ifdef LINENOISE
      ln_line_ = nullptr;
      ln_next_ = nullptr;
      linenoiseHistoryLoad(".fy.history.txt");
      linenoiseHistorySetMaxLen(1000);
#else
      isatty_ = (isatty(0) == 1);
      fflush(stdout);
      fprintf(stderr, " ok ");
#endif
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
    int ch;
#ifdef LINENOISE
    if (current_ != stdin) {
      ch = fgetc(current_);
    } else if (ln_next_ && *ln_next_) {
      ch = *ln_next_++;
    } else if (ln_next_) {
      ln_next_ = nullptr;
      return '\n';
    } else {
      if (ln_line_)
        linenoiseFree(ln_line_);
      ln_line_ = linenoise(" OK ");
      if (ln_line_) {
        linenoiseHistoryAdd(ln_line_);
        linenoiseHistorySave(".fy.history.txt");
        ln_next_ = ln_line_;
        if (ln_next_ && *ln_next_) {
          ch = *ln_next_++;
        } else if (ln_next_) {
          ln_next_ = nullptr;
          return '\n';
        } else {
          return Key();         // recurse.
        }
      } else {
        ch = EOF;
      }
    }
#else
    ch = fgetc(current_);
    if (ch == '\n' && isatty_) {
      next_ok_ = true;
    }
#endif
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
  CreateWord(name, X_ENTER_);
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
    case X_END:
      return;
      break;
    case X_DOT:{
        U x = Pop();
        printf("%lld. ", (long long) C(x));
        fflush(stdout);
      }
      break;
    case XCR:
      putchar('\n');
      fflush(stdout);
      break;
    case XDUP:
      // dup   ( a -- a a )
      Push(Peek());
      break;
    case XDROP:
      // drop  ( a -- )
      Ds += S;
      break;
    case X_2DUP:
      Push(Peek(1));
      Push(Peek(1));
      break;
    case X_2DROP:
      Ds += 2 * S;
      break;
    case XSWAP:{
        // swap  ( a b -- b a )
        U x = Peek();
        U y = Peek(1);
        Poke(y, 0);
        Poke(x, 1);
      }
      break;
      // ?dup  ( a -- a a | 0 ) dup if dup then ;
    case XOVER:
      // over  ( a b -- a b a )
      Push(Peek(1));
      break;
    case XROT:{
        // rot   ( a b c -- b c a )
        U tmp = Peek(2);
        Poke(Peek(1), 2);
        Poke(Peek(0), 1);
        Poke(tmp, 0);
      }
      break;
    case X_ROT:{
        // -rot  ( a b c -- c a b ) rot rot ;
        U tmp = Peek(0);
        Poke(Peek(1), 0);
        Poke(Peek(2), 1);
        Poke(tmp, 2);
      }
      break;
    case XNIP:
      // nip   ( a b -- b ) swap drop ;
      Poke(Pop());
      break;
    case XTUCK:{
        // tuck  ( a b -- b a b ) swap over ;
        // first swap.
        U x = Peek();
        U y = Peek(1);
        Poke(y, 0);
        Poke(x, 1);
        // then over.
        Push(Peek(1));
      }
      break;
    case XGT_R:{
        PushR(Pop());
      }
      break;
    case XR_GT:{
        Push(PopR());
      }
      break;
    case XR_AT:{
        Push(Get(Rs));
      }
      break;
    case XI:{
        Push(Get(Rs));
      }
      break;
    case XJ:{
        Push(Get(Rs + 2 * S));
      }
      break;
    case XK:{
        Push(Get(Rs + 4 * S));
      }
      break;
    case X_LIT_:{
        Push(Get(Ip));
        Ip += S;
      }
      break;
    case X_ENTER_:{
        PushR(Ip);
        Ip = W;
      }
      break;
    case X_EXIT_:{
        Ip = PopR();
      }
      break;
    case X_SEMICOLON:
      {
        U compiling = Get(StatePtr);
        if (!compiling)
          Fatal("cannot use `;` when not compiling");
      }
      Comma(LookupCfa("(exit)"));
      Put(StatePtr, 0);         // Interpreting state.
      break;
    case X_COLON:
      // ColonDefinition();
      {
        U compiling = Get(StatePtr);
        if (compiling)
          Fatal("cannot use `:` when already compiling");
        char *name = WordStr();
        CreateWord(name, X_ENTER_);
        Put(StatePtr, 1);       // Compiling state.
      }
      break;
    case XALIGN:
      Poke(Aligned(Peek()));
      break;
    case X_PLUS:
      fprintf(stderr, "{PLUS: %d %d %d}\n", CPeek(1), CPeek(), CPeek(1) + CPeek());
      DropPokeC((C) Peek(1) + (C) Peek());
      break;
    case X_MINUS:
      fprintf(stderr, "{MINUS: %d %d %d}\n", CPeek(1), CPeek(), CPeek(1) - CPeek());
      DropPokeC((C) Peek(1) - (C) Peek());
      break;
    case X_TIMES:
      fprintf(stderr, "{TIMES: %d %d %d}\n", CPeek(1), CPeek(), CPeek(1) * CPeek());
      DropPokeC(CPeek(1) * CPeek());
      break;
    case X_DIVIDE:
      fprintf(stderr, "{DIV: %d %d %d}\n", CPeek(1), CPeek(), CPeek(1) / CPeek());
      DropPokeC(CPeek(1) / CPeek());
      break;
    case XMOD:
      fprintf(stderr, "{MOD: %d %d %d}\n", CPeek(1), CPeek(), CPeek(1) % CPeek());
      DropPokeC(CPeek(1) % CPeek());
      break;
    case X_EQ:
      fprintf(stderr, "{EQ: %d %d %d}\n", CPeek(1), CPeek(), CPeek(1) == CPeek());
      DropPokeC(CPeek(1) == CPeek());
      break;
    case X_NE:
      fprintf(stderr, "{NE: %d %d %d}\n", CPeek(1), CPeek(), CPeek(1) != CPeek());
      DropPokeC(CPeek(1) != CPeek());
      break;
    case X_LT:
      fprintf(stderr, "{LT: %d %d %d}\n", CPeek(1), CPeek(), CPeek(1) < CPeek());
      DropPokeC(CPeek(1) < CPeek());
      break;
    case X_LE:
      fprintf(stderr, "{LE: %d %d %d}\n", CPeek(1), CPeek(), CPeek(1) <= CPeek());
      DropPokeC(CPeek(1) <= CPeek());
      break;
    case X_GT:
      fprintf(stderr, "{GT: %d %d %d}\n", CPeek(1), CPeek(), CPeek(1) > CPeek());
      DropPokeC(CPeek(1) > CPeek());
      break;
    case X_GE:
      fprintf(stderr, "{GE: %d %d %d}\n", CPeek(1), CPeek(), CPeek(1) >= CPeek());
      DropPokeC(CPeek(1) >= CPeek());
      break;
    case XDUMPMEM:
      DumpMem(true);
      break;
    case XWORDS:
      Words();
      break;
    case XR0:
      Push(Rs0);
      break;
    case XS0:
      Push(Ds0);
      break;
    case XMUST:
      if (Pop() == 0) {
        DumpMem(true);
        fprintf(stderr, " *** MUST failed\n");
        assert(0);
      } else {
        ++MustOk;
        fprintf(stderr, "   [MUST okay #%d]\n", MustOk);
      }
      break;
    case XIMMEDIATE:
      Mem[Get(LatestPtr) + S] ^= IMMEDIATE_BIT;
      break;
    case XHIDDEN:
      Mem[Pop() + S] ^= HIDDEN_BIT;
      break;
    case XKEY:
      Key();
      break;
    case XWORD:
      Word();
      break;
    case XHERE:
      Push(Get(HerePtr));
      break;
    case X_TICK:{
        char *word = WordStr();
        assert(word);
        U cfa = LookupCfa(word);
        assert(cfa);
        Push(cfa);
        fprintf(stderr, "_TICK: word=`%s` cfa=%d\n", word, cfa);
      }
      break;
    case X_COMMA:
      Comma(Pop());
      break;
    case XDO:{
        U compiling = Get(StatePtr);
        if (!compiling)
          Fatal("cannot use DO unless compiling");

        Comma(CheckU(LookupCfa("nop_do")));
        U swap = CheckU(LookupCfa("swap"));
        Comma(swap);
        U onto_r = CheckU(LookupCfa(">r"));
        Comma(onto_r);
        Comma(onto_r);
        Push(Get(HerePtr));     // Jump back target.
        Push(0);                // No repair.
        Push(0);                // No leave repair.
      }
      break;
    case X_DO:{                // ?DO
        U compiling = Get(StatePtr);
        if (!compiling)
          Fatal("cannot use ?DO unless compiling");

        Comma(CheckU(LookupCfa("nop_do")));
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
        Push(0);                // No leave repair.
      }
      break;

    case X_INCR_I_:
      ++Mem[Rs];
      break;
    case X_LOOP_:{
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
    case XLOOP:{
        U leave = Pop();
        U repair = Pop();
        U back = Pop();
        Comma(CheckU(LookupCfa("nop_loop")));
        Comma(CheckU(LookupCfa("(incr_i)")));
        if (repair) {
          // Repair ?DO target to jump after (incr_i) and before (loop).
          Put(repair, Get(HerePtr) - repair);
        }
        Comma(CheckU(LookupCfa("(loop)")));
        Comma(back - Get(HerePtr));
        if (leave) {
          // Repair leave target.
          Put(leave, Get(HerePtr) - leave);
        }
      }
      break;


    case X_PLUS_INCR_I_:
      Mem[Rs] += Pop();
      break;
    case XPLUS_LOOP:{
        U leave = Pop();
        U repair = Pop();
        U back = Pop();
        Comma(CheckU(LookupCfa("nop_loop")));
        Comma(CheckU(LookupCfa("(+incr_i)")));
        if (repair) {
          // Repair ?DO target to jump after (incr_i) and before (loop).
          Put(repair, Get(HerePtr) - repair);
        }
        Comma(CheckU(LookupCfa("(loop)")));
        Comma(back - Get(HerePtr));
        if (leave) {
          // Repair leave target.
          Put(leave, Get(HerePtr) - leave);
        }
      }
      break;

    case XLEAVE:{
        // TODO -- current requires exactly 1 IF...THEN around it.
        // Replace 
        U if_then = Pop();
        U old_leave = Pop();
        if (old_leave) {
          // Repair leave target.
          Put(old_leave, Get(HerePtr) - old_leave);
        }
        Comma(CheckU(LookupCfa("nop_leave")));
        Comma(CheckU(LookupCfa("r>"))); // Drop count from return stack.
        Comma(CheckU(LookupCfa("drop")));
        Comma(CheckU(LookupCfa("r>"))); // Drop limit from return stack.
        Comma(CheckU(LookupCfa("drop")));
        Comma(CheckU(LookupCfa("branch")));
        U new_leave = Get(HerePtr);
        Comma(0);               // Needs repairing with leave.

        U old_repair = Pop();
        if (old_repair) {
          // Repair the old branch to chain the following branch.
          Put(old_repair, Get(HerePtr) - old_repair);
          Comma(CheckU(LookupCfa("branch")));
          U new_repair = Get(HerePtr);
          Comma(0);             // Needs repairing.
          Push(new_repair);
        } else {
          Push(0);              // New repair is also empty.
        }
        Push(new_leave);
        Push(if_then);
      }
      break;
    case XUNLOOP:
      Rs += 2 * S;              // Pop count & limit off of the return stack.
      break;
    case XIF:{
        U compiling = Get(StatePtr);
        if (!compiling)
          Fatal("cannot use IF unless compiling");
        Comma(LookupCfa("nop_if"));
        Comma(LookupCfa("0branch"));
        Push(Get(HerePtr));     // Push position of EEEE to repair.
        Comma(0xEEEE);
      }
      break;
    case XELSE:{
        U repair = Pop();
        Comma(LookupCfa("nop_else"));
        Comma(LookupCfa("branch"));
        Push(Get(HerePtr));     // Push position of EEEE to repair.
        Comma(0xEEEE);
        Put(repair, Get(HerePtr) - repair);
        Comma(LookupCfa("nop"));
      }
      break;
    case XTHEN:{
        U repair = Pop();
        Put(repair, Get(HerePtr) - repair);
        Comma(LookupCfa("nop_then"));
      }
      break;
    case XBRANCH:
      Ip += Get(Ip);            // add offset to Ip.
      break;
    case XBRANCH0:
      if (Pop() == 0) {
        Ip += Get(Ip);          // add offset to Ip.
      } else {
        Ip += S;                // skip over offset.
      }
      break;
    case XNOP:
    case XNOP_DO:
    case XNOP_LOOP:
    case XNOP_LEAVE:
    case XNOP_IF:
    case XNOP_THEN:
    case XNOP_ELSE:
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
  // memset(Mem, 0, MemLen);

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

  CreateWord("+", X_PLUS);
  CreateWord(".", X_DOT);
  CreateWord("cr", XCR);
  CreateWord("dup", XDUP);
  CreateWord("drop", XDROP);
  CreateWord("2dup", X_2DUP);
  CreateWord("2drop", X_2DROP);
  CreateWord("swap", XSWAP);
  CreateWord("over", XOVER);
  CreateWord("rot", XROT);
  CreateWord("-rot", X_ROT);
  CreateWord("nip", XNIP);
  CreateWord("tuck", XTUCK);
  CreateWord(">r", XGT_R);
  CreateWord("r>", XR_GT);
  CreateWord("r@", XR_AT);
  CreateWord("i", XI);
  CreateWord("j", XJ);
  CreateWord("k", XK);
  CreateWord("(lit)", X_LIT_);
  CreateWord("(enter)", X_ENTER_);
  CreateWord("(exit)", X_EXIT_);
  CreateWord(";", X_SEMICOLON, IMMEDIATE_BIT);
  CreateWord(":", X_COLON);
  CreateWord("align", XALIGN);
  CreateWord("-", X_MINUS);
  CreateWord("*", X_TIMES);
  CreateWord("/", X_DIVIDE);
  CreateWord("mod", XMOD);
  CreateWord("=", X_EQ);
  CreateWord("==", X_EQ);
  CreateWord("!=", X_NE);
  CreateWord("<", X_LT);
  CreateWord("<=", X_LE);
  CreateWord(">", X_GT);
  CreateWord(">=", X_GE);
  CreateWord("dumpmem", XDUMPMEM);
  CreateWord("key", XKEY);
  CreateWord("word", XWORD);
  CreateWord("here", XHERE);
  CreateWord("words", XWORDS);
  CreateWord("'", X_TICK, IMMEDIATE_BIT);
  CreateWord("r0", XR0);
  CreateWord("s0", XS0);
  CreateWord("must", XMUST);
  CreateWord("immediate", XIMMEDIATE, IMMEDIATE_BIT);
  CreateWord(",", X_COMMA);
  CreateWord("hidden", XHIDDEN);
  CreateWord("branch", XBRANCH);
  CreateWord("0branch", XBRANCH0);
  CreateWord("do", XDO, IMMEDIATE_BIT);
  CreateWord("?do", X_DO, IMMEDIATE_BIT);
  CreateWord("(incr_i)", X_INCR_I_);
  CreateWord("(loop)", X_LOOP_);
  CreateWord("loop", XLOOP, IMMEDIATE_BIT);
  CreateWord("(+incr_i)", X_PLUS_INCR_I_);
  CreateWord("+loop", XPLUS_LOOP, IMMEDIATE_BIT);
  CreateWord("leave", XLEAVE, IMMEDIATE_BIT);
  CreateWord("unloop", XUNLOOP, IMMEDIATE_BIT);
  CreateWord("if", XIF, IMMEDIATE_BIT);
  CreateWord("else", XELSE, IMMEDIATE_BIT);
  CreateWord("then", XTHEN, IMMEDIATE_BIT);
  CreateWord("nop", XNOP);
  CreateWord("nop_do", XNOP_DO);
  CreateWord("nop_loop", XNOP_LOOP);
  CreateWord("nop_leave", XNOP_LEAVE);
  CreateWord("nop_if", XNOP_IF);
  CreateWord("nop_then", XNOP_THEN);
  CreateWord("nop_else", XNOP_ELSE);
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
  // MemLen = 0x10000;             // Default: 64 kib RAM.

  Argv0 = argv[0];
  ++argv, --argc;
  const char *text = "";
  bool interactive = false;
  while (argc > 0 && argv[0][0] == '-') {
    switch (argv[0][1]) {
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
