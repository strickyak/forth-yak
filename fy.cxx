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

const char *SmartPrintNum(U u, FILE * fd = stdout)
{
  static char buf[99];
  C x = (C) u;
  if (link_map.find(u) != link_map.end()) {
    sprintf(buf, "  L{%s}", link_map[x].c_str());
  } else if (cfa_map.find(u) != cfa_map.end()) {
    sprintf(buf, "  U{%s}", cfa_map[x].c_str());
  } else if (dfa_map.find(u) != dfa_map.end()) {
    sprintf(buf, "  D{%s}", dfa_map[x].c_str());
  } else if (-2 * MemLen <= x && x <= 2 * MemLen) {
    sprintf(buf, "  %llx", (ULL) u);
  } else {
    sprintf(buf, "  ---");
  }
  if (fd)
    fprintf(fd, "%s", buf);
  return buf;
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

#ifdef OPT
#define DISPATCH cfa = Get(Ip); Ip += S; op = Get(cfa); W = cfa + S; goto *dispatch_table[op];
#else
#define DISPATCH {\
    Show();\
    cfa = Get(Ip);\
    Ip += S;\
    op = Get(cfa);\
    W = cfa + S;\
    goto *dispatch_table[op];\
    }

void Show()
{
  U cfa = Get(Ip);
  U op = Get(cfa);
  U return_size = Rs0 - Rs;
  U data_size = Ds0 - Ds;
  fprintf(stderr, "Ip=%lld -> %lld(%s) -> %lld [%llu; %llu]", (ULL) Ip, (ULL) cfa, SmartPrintNum(cfa, nullptr),
          (ULL) op, (ULL) return_size / S, (ULL) data_size / S);
  for (U i = 0; i < data_size && i < 32 * S; i += S) {
    fprintf(stderr, " %s", SmartPrintNum(Get(Ds0 - (i + 1) * S), nullptr));
  }
  fprintf(stderr, "\n");
}
#endif

void DispatchLoop()
{
  static void *dispatch_table[] = {
#include "generated-dispatch-table.inc"
  };

  U cfa;
  U op;

  DISPATCH;
  while (true) {
#include "generated-dispatchers.inc"
  }
}

void ExecuteCfa(U cfa)
{
  U stop = LookupCfa("(stop)");
  PushR(stop);
  PushR(cfa);
  Ip = Rs;

  DispatchLoop();

  U r1 = PopR();
  CheckEq(__LINE__, r1, cfa);
  U r2 = PopR();
  CheckEq(__LINE__, r2, stop);
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

  DispatchLoop();

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

#include "generated-creators.inc"
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
        Comma(CheckU(LookupCfa("lit")));
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
