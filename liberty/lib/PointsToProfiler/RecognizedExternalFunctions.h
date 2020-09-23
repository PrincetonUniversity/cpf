// This is a list of standard library functions that the profiler
// handles completely.  If the program-under-test calls an external
// function which is NOT on this list, then there is a possibility
// that said function might have introduced pointers to an object
// which was not otherwise instrumented.
//
// This list is NOT required for correctness, but is useful for
// precision.  It allows us to discern between undefined behavior
// in the program-under-test and incomplete profile coverage.
//
#ifndef SPECPRIV_PROFILER_RECOGNIZED_EXTERNAL_FUNCTIONS_H
#define SPECPRIV_PROFILER_RECOGNIZED_EXTERNAL_FUNCTIONS_H
  "__assert_fail",
  "__cxa_atexit",
  "__cxa_begin_catch",
  "__errno_location",
  "__fxstat", //__fxstat (int vers, int fd, struct stat *buf) reads file descripter fd from buf
  "__isoc99_fscanf",
  "__isoc99_scanf",
  "__isoc99_sscanf",
  "__rawmemchr",
  "__sysv_signal",
  "__xstat",//return information about a file, in the buffer pointed to by statbuf.
  "_exit",
  "_IO_getc",
  "_IO_putc",
  "_obstack_begin",
  "_setjmp",
  "_ZNSt8ios_base4InitC1Ev",
  "_ZSt9terminatev", //std::terminate()
  "_ZNSo3putEc", //std::basic_ostream<char, std::char_traits<char> >::put(char)
  "_Znwm", // C++ operator new
  "_Znam", // C++ operator new[]
  "_ZnwmRKSt9nothrow_t", // C++ operator new no throw
  "_ZnamRKSt9nothrow_t", // C++ operator new[] no throw
  "_ZdlPv", // C++ operator delete
  "_ZdaPv", // C++ operator delete[]
  "_ZdlPvm", // C++ operator delete with size
  "_ZdaPvm", // C++ operator delete[] with size
  "_ZdlPvRKSt9nothrow_t", // C++ operator delete no throw
  "_ZdaPvRKSt9nothrow_t", // C++ operator delete[] no throw
  "abort",
  "access",
  "atexit",
  "atof",
  "atoi",
  "atol",
  "atoll",
  "atoq",
  "bsearch",
  "calloc",
  "chdir",
  "chmod",
  "clearerr",
  "clock",
  "close",
  "closedir",
  "ctime",
  "difftime",
  "dup",
  "dup2",
  "exit",
  "fcntl",
  "fclose",
  "fdopen",
  "feof",
  "ferror",
  "fflush",
  "fgetc",
  "fgetc_unlocked",
  "fgets",
  "fileno",
  "fprintf",
  "fscanf",
  "fsync",
  "fopen",
  "fopen64",
  "fputc_unlocked",
  "freopen",
  "fputc",
  "fputs",
  "fread",
  "free",
  "fseek",
  "ftell",
  "ftruncate",
  "fwrite",
  "gcvt",
  "getc",
  "getcwd",
  "getegid",
  "getenv",
  "geteuid",
  "getgid",
  "getpagesize",
  "getpwnam",
  "getpid",
  "getuid",
  "getrlimit",
  "gettimeofday",
  "gmtime",
  "ioctl",
  "isatty",
  "isnan",
  "kill",
  "link",
  "llvm.bswap.i16",
  "llvm.bswap.i32",
  "llvm.memcpy.p0i8.p0i8.i64",
  "llvm.memmove.p0i8.p0i8.i64",
  "llvm.memset.p0i8.i64",
  "llvm.trap",
  "llvm.uadd.with.overflow.i32",
  "llvm.uadd.with.overflow.i64",
  "llvm.umul.with.overflow.i32",
  "llvm.umul.with.overflow.i64",
  "llvm.stacksave",   // introduces an opaque pointer
  "llvm.stackrestore",
  "llvm.va_copy",
  "llvm.va_start",    // introduces an opaque pointer
  "llvm.va_end",
  "llvm.round.f64",
  "llvm.exp2.f64",
  "lseek",
  "localtime",
  "malloc",
  "memcmp",
  "mkdir",
  "nice",
  "open",
  "opendir",
  "pclose",
  "perror",
  "pipe",
  "popen",
  "printf",
  "putchar",
  "putenv",
  "puts",
  "qsort",
  "spec_qsort",
  "rand",
  "read",
  "readdir",
  "realloc",
  "remove",
  "rewind",
  "rmdir",
  "scanf",
  "select",
  "setbuf",
  "setlocale",
  "setgid",
  "setrlimit",
  "setuid",
  "setvbuf",
  "signal",
  "sleep",
  "sprintf",
  "snprintf",
  "sqrt",
  "sscanf",
  "srand",
  "strcat",
  "strcpy",
  "strerror",
  "strftime",
  "strncat",
  "strncpy",
  "strtod",
  "strtok", // modifies string in place; modifies internal library state
  "strtol",
  "strtoul",
  "sysconf",
  "sysv_signal",
  "time",
  "times",
  "tmpfile",
  "truncate",
  "ungetc",
  "unlink",
  "vfprintf",
  "vsnprintf",
  "vsprintf",
  "waitpid",
  "write",
// Note that the last entry still has a comma.
#include "./../Analysis/PureFun.h"
#endif



