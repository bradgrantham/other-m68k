// ctors: static constructors and destructors in a program, C++ and C style. Every step
// prints a numbered line, so the output is the order the start-up and exit code ran them:
//
//    1 .preinit_array entry        before every constructor
//    2 constructor(101)            constructors with a priority, lowest number first
//    3 constructor(102)
//    4 first constructed           static objects, in the order of their definitions
//    5 second constructed
//    6 main
//    7 atexit handler              exit() undoes things in the reverse order: what main
//    8 second destroyed            registered, the objects (last constructed first),
//    9 first destroyed
//   10 destructor                  then the destructor functions, the plain one first
//   11 destructor(101)
//
// Build like the other examples; a C++ source is compiled with $(CXXFLAGS) (sdk/Makefile:
// no exceptions, no RTTI) and linked with the same libraries, so there is no libstdc++.
#include <stdio.h>
#include <stdlib.h>

static int step;                // .bss: crt0 cleared it before the first constructor

static void say(const char *what) { printf("%2d %s\n", ++step, what); }

struct Tracer {
    const char *name;
    explicit Tracer(const char *n) : name(n) { printf("%2d %s constructed\n", ++step, name); }
    ~Tracer() { printf("%2d %s destroyed\n", ++step, name); }
};

static void pre() { say(".preinit_array entry"); }
__attribute__((section(".preinit_array"), used)) static void (*pre_entry)() = pre;

__attribute__((constructor(102))) static void later() { say("constructor(102)"); }
__attribute__((constructor(101))) static void early() { say("constructor(101)"); }
__attribute__((destructor)) static void plain_end() { say("destructor"); }
__attribute__((destructor(101))) static void last_end() { say("destructor(101)"); }

static Tracer first("first");
static Tracer second("second");

static void at_exit() { say("atexit handler"); }

int main()
{
    atexit(at_exit);
    say("main");
    return 0;
}
