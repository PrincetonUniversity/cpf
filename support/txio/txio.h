#ifndef LIBERTY_PUREIO_H
#define LIBERTY_PUREIO_H

#include <stdio.h>
#include <stdint.h>

/* Precisely express an instant in time.
 *
 * It was an intentional decision to type-pun
 * pointers to Epoch objects as integers.
 * This is to avoid pessimistic behavior in our
 * alias analysis stack.
 */
typedef uint64_t EpochId;

/* Epoch management functions.
 *
 * Conceptually, the alias-analysis stack may
 * treat these as 'pure' functions, which means
 * that they do not read or write memory and
 * do not perform side effects, and depend only
 * on the values of their parameters.
 */

/* Construct an epoch which will be used for
 * a suspended operation
 */
EpochId __epoch(EpochId e, uint32_t n, ...) __attribute__((pure));

/* Construct the root epoch.
 */
EpochId __root_epoch(void) __attribute__((pure));

/* Construct a new epoch with 'e' as a prefix
 * and 'n' integers as a suffix.  'e' may be
 * null.
 */
EpochId __open_subepoch(EpochId e, const char *dbgname, uint32_t n, ...) __attribute__((pure));

/* Declare that there will be no more epochs in this
 * prefix.  This enables the runtime library to
 * commit eagerly.
 */
void __close_epoch(EpochId, uint32_t n_subepochs) __attribute__((pure));

/* Like __txio_close_epoch, but also blocks until all
 * suspended operations up to 'e' have committed.
 *
 * The AA stack may NOT treat this function as pure;
 * we must treat it like it has side effects.
 */
void __close_epoch_blocking(EpochId, uint32_t n_subepochs);

/* Methods to announce that an epoch will only touch some
 * files.  This enables commutativity testing early on.
 */
EpochId  __announce_restricted(EpochId, ...);

/* These are the TXIO-versions of IO functions
 * from the standard library.
 *
 * Conceptually, the alias-analysis stack may
 * treat these as 'pure' functions, which means
 * that they have no side effects and do not
 * write to memory, though they may read from
 * memory accessible from their parameters.
 */

int __txio_vfprintf(EpochId, FILE*, const char*, va_list ap);
int __txio_vprintf(EpochId, const char*, va_list ap);
int __txio_fprintf(EpochId, FILE*, const char*, ...) __attribute__(( format (printf,3,4) ));
int __txio_printf(EpochId, const char*, ...) __attribute__(( format (printf,2,3) ));
int __txio_fputs(EpochId, FILE*, const char*);
int __txio_puts(EpochId, const char*);
int __txio_fwrite(EpochId, void*, size_t, size_t, FILE*);
int __txio_fflush(EpochId, FILE*);
int __txio_fclose(EpochId, FILE*);
int __txio_fputc(EpochId, int, FILE*);
int __txio_putc(EpochId, int, FILE*);
int __txio_putchar(EpochId, int);
int __txio__IO_putc(EpochId, int, FILE*);
void __txio_abort(EpochId);
void __txio_llvm_trap(EpochId);
void __txio_exit(EpochId, int);
int __txio_perror(EpochId, const char*);
void __txio___assert_fail(EpochId, const char *, const char *, uint32_t, const char *);
int __txio_remove(EpochId, const char *);
int __txio_unlink(EpochId, const char *);

size_t __txio_write(EpochId, int, void *, size_t);
int __txio_close(EpochId, int);

int __txio_fseek(EpochId, FILE *, long, int);
int __txio_lseek(EpochId, int, long, int);
void __txio_rewind(EpochId, FILE *);

int __txio_open(EpochId, const char *, int, ...);
FILE *__txio_fopen(EpochId, const char *, const char *);

size_t __txio_fread(EpochId, void *, size_t, size_t, FILE*);
int __txio_read(EpochId, int, void *, int);

int __txio_fgetc(EpochId, FILE*);
int __txio__IO_getc(EpochId, FILE*);
int __txio_getc(EpochId, FILE*);
int __txio_getchar(EpochId);

struct stat;
int __txio___fxstat(EpochId, int, int, struct stat *);
int __txio___xstat(EpochId, int, const char *, struct stat *);

char *__txio_fgets(EpochId, char *, int, FILE*);

int __txio_ferror(EpochId, FILE*);


void __txio___deferred_store_i32(EpochId, uint32_t *, uint32_t);
void __txio___deferred_store_i64(EpochId, uint64_t *, uint64_t);
void __txio___deferred_store_float(EpochId, float *, float);
void __txio___deferred_store_double(EpochId, double *, double);

void __txio___deferred_add_i32(EpochId, uint32_t *, uint32_t);
void __txio___deferred_add_i64(EpochId, uint64_t *, uint64_t);
void __txio___deferred_add_float(EpochId, float *, float);
void __txio___deferred_add_double(EpochId, double *, double);

uint32_t __txio___deferred_load_i32(EpochId, uint32_t *);
uint64_t __txio___deferred_load_i64(EpochId, uint64_t *);
float __txio___deferred_load_float(EpochId, float *);
double __txio___deferred_load_double(EpochId, double *);

void __txio___deferred_call(EpochId e, void (*fcn)(void*), void *arg );

void __txio_fadd_vec(EpochId e, float *dst, float *src, unsigned n);

// Not yet implemented.

int __txio_fileno(EpochId, FILE *);
int __txio_feof(EpochId, FILE*);
long __txio_ftell(EpochId, FILE*);

FILE *__txio_tmpfile(EpochId);

int __txio_fscanf(EpochId, FILE*, const char *, ...);
int __txio_scanf(EpochId, const char *, ...);
int __txio___isoc99_fscanf(EpochId, FILE*, const char *, ...);


#endif

