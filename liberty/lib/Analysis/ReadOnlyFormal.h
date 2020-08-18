// stdio.h
{ "vfprintf", 1 },
{ "fprintf", 1 },
{ "fputc", 1 },
{ "printf", 0 },
{ "snprintf", 2 },
{ "sprintf", 1 },
{ "fwrite", 0 },
{ "fread", 3},
{ "puts", 0 },
{ "fputs", 0 },
// string.h
{ "strcat", 1 },
{ "strtod", 0 },
// llvm intrinsics, becoming more popular every day.
{ "llvm.memcpy.p0i8.p0i8.i32", 1 },
{ "llvm.memcpy.p0i8.p0i8.i64", 1 },
{ "llvm.memmove.p0i8.p0i8.i32", 1 },
{ "llvm.memmove.p0i8.p0i8.i64", 1 },
