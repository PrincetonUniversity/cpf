#ifndef MEM_PROF_FUNCTIONS_H
#define MEM_PROF_FUNCTIONS_H

#include <sys/types.h>
#include <sys/resource.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/timeb.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/times.h>
#include <sys/time.h>
#include <utime.h>
#include <string.h>
#include <termios.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*load_func)(uint64_t addr, uint64_t size);
typedef void (*store_func)(uint64_t addr, uint64_t size);

void functions_init(void);

/* String functions */
size_t memprof_strlen(const char *str);
char * memprof_strchr(char *s, int c);
char * memprof_strrchr(char *s, int c);
int    memprof_strcmp(const char *s1, const char *s2);
int    memprof_strncmp(const char *s1, const char *s2, size_t n);
char * memprof_strcpy(char *dest, const char *src);
char * memprof_strncpy(char *dest, const char *src, size_t n);
char * memprof_strcat(char *s1, const char *s2);
char * memprof_strncat(char *s1, const char *s2, size_t n);
char * memprof_strstr(char *s1, char *s2);
size_t memprof_strspn(const char *s1, const char *s2);
size_t memprof_strcspn(const char *s1, const char *s2);
char * memprof_strtok(char *s, const char *delim);
double memprof_strtod(const char *nptr, char **endptr);
long int memprof_strtol(const char *nptr, char **endptr, int base);
char * memprof_strdup(const char *s1);
char * memprof_strpbrk(char *s1, char *s2);

/* Malloc functions */
void * memprof_malloc(size_t n);
void   memprof_free(void *ptr);
void * memprof_calloc(size_t nelem, size_t elsize);
void * memprof_realloc(void * ptr, size_t n);

int    memprof_brk(void *end_data_segment);
void * memprof_sbrk(intptr_t increment);

/* Mem* and b* functions */
void *memprof_memset (void *dest, int c, size_t n);
void *memprof_memcpy (void *dest, const void *src, size_t n);
void *memprof___builtin_memcpy (void *dest, const void *src, size_t n);
void *memprof_memmove (void *dest, const void *src, size_t n);
int   memprof_memcmp(const void *s1, const void *s2, size_t n);

void  memprof_bzero(void *s, size_t n);
void  memprof_bcopy(const void *s1, void *s2, size_t n);

/* IO */
ssize_t memprof_read(int fd, void *buf, size_t count);
int     memprof_open(const char *pathname, int flags, mode_t mode);
int     memprof_close(int fd);
ssize_t memprof_write(int fd, const void *buf, size_t count);
off_t   memprof_lseek(int fildes, off_t offset, int whence);

FILE *  memprof_fopen(const char *path, const char *mode);
int     memprof_fflush(FILE *stream);
int     memprof_fclose(FILE *stream);
int     memprof_ferror(FILE *stream);
int     memprof_feof(FILE *stream);
long    memprof_ftell(FILE *stream);
size_t  memprof_fread(void * ptr, size_t size, size_t nitems, FILE *stream);
size_t  memprof_fwrite(const void *ptr, size_t size, size_t nitems, FILE *stream);
int     memprof_fseek(FILE *stream, long offset, int whence);
void    memprof_rewind(FILE *stream);

int     memprof_fgetc(FILE *stream);
int     memprof_fputc(int c, FILE *stream);
char *  memprof_fgets(char *s, int n, FILE *stream);
int     memprof_fputs(const char *s, FILE *stream);

int     memprof_ungetc(int c, FILE *stream);
int     memprof_putchar(int c);
int     memprof_getchar(void);

int     memprof_fileno(FILE *stream);
char *  memprof_gets(char *s);
int     memprof_puts(const char *s);

int     memprof_select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
int     memprof_remove(const char *path);

void    memprof_setbuf(FILE * stream, char * buf);
char * memprof_tmpnam(char *s);
FILE* memprof_tmpfile(void);
char *  memprof_ttyname(int fildes);

FILE *  memprof_fdopen(int fildes, const char *mode);
void    memprof_clearerr(FILE *stream);

int memprof_truncate(const char *path, off_t length);
int memprof_ftruncate(int fildes, off_t length);

int memprof_dup(int oldfd);
int memprof_dup2(int oldfd, int newfd);
int memprof_pipe(int filedes[2]);

int memprof_chmod(const char *path, mode_t mode);
int memprof_fchmod(int fildes, mode_t mode);
int memprof_fchown(int fd, uid_t owner, gid_t group);
int memprof_access(const char *pathname, int mode);
long memprof_pathconf(char *path, int name);
int memprof_mkdir(const char *pathname, mode_t mode);
int memprof_rmdir(const char *pathname);
mode_t memprof_umask(mode_t mask);
int memprof_fcntl(int fd, int cmd, struct flock *lock);

/* Printf */
int memprof_printf(const char *format, ...);
int memprof_fprintf(FILE *stream, const char *format, ...);
int memprof_sprintf(char *str, const char *format, ...);
int memprof_snprintf(char *str, size_t size, const char *format, ...);

int memprof_vprintf(const char *format, va_list ap);
int memprof_vfprintf(FILE *stream, const char *format, va_list ap);
int memprof_vsprintf(char *str, const char *format, va_list ap);
int memprof_vsnprintf(char *str, size_t size, const char *format, va_list ap);

/* Scanf */
int memprof_fscanf(FILE *stream, const char *format, ... );
int memprof_scanf(const char *format, ... );
int memprof_sscanf(const char *s, const char *format, ... );

int memprof_vfscanf(FILE *stream, const char *format, va_list ap);
int memprof_vscanf(const char *format, va_list ap);
int memprof_vsscanf(const char *s, const char *format, va_list ap);

/* Time */
time_t memprof_time(time_t *t);
char * memprof_ctime(const time_t *clock);
char * memprof_ctime_r(const time_t *clock, char *buf);
int memprof_ftime(struct timeb *tp);
struct tm *memprof_localtime(const time_t *timer);
int memprof_gettimeofday(struct timeval *tv, struct timezone *tz);
size_t memprof_strftime(char *s, size_t maxsize,
              const char *format, const struct tm *timeptr);
double memprof_difftime(time_t time1, time_t time0);
clock_t memprof_clock(void);
clock_t memprof_times(struct tms *buf);
int memprof_utime(const char *filename, const struct utimbuf *buf);
int memprof_utimes(char *filename, struct timeval *tvp );
struct tm *memprof_gmtime(const time_t *timer);

/* Convert byte ordering */
uint32_t memprof_htonl(uint32_t hostlong);
uint16_t memprof_htons(uint16_t hostshort);
uint32_t memprof_ntohl(uint32_t netlong);
uint16_t memprof_ntohs(uint16_t netshort);

/* MISC */
int  memprof_rename(const char *oldpath, const char *newpath);
int  memprof_getrusage(int who, struct rusage *r_usage);
int  memprof_rand(void);
void memprof_srand(unsigned seed);
long int memprof_lrand48(void);
double memprof_drand48(void);
int  memprof_toupper(int c);
int  memprof_tolower(int c);
void memprof_abort(void);
int memprof_atexit(void (*func)(void));
void memprof_exit(int status);
void memprof__exit(int status);
int  memprof_atoi(const char *str);
long  memprof_atol(const char *str);
double memprof_atof(const char *str);
int  memprof_unlink(const char *pathname);
int  memprof_isatty(int desc);
int memprof_tcgetattr(int fd, struct termios *termios_p);
int memprof_tcsetattr(int fd, int optional_actions, struct termios *termios_p);
int memprof_setuid(uid_t uid);
uid_t memprof_getuid(void);
uid_t memprof_geteuid(void);
int memprof_setgid(gid_t uid);
gid_t memprof_getgid(void);
gid_t memprof_getegid(void);
pid_t memprof_getpid(void);
pid_t memprof_getppid(void);
int memprof_chdir(const char *path);

/* Math */
double memprof_ldexp(double x, int exp);
float  memprof_ldexpf(float x, int exp);
long double memprof_ldexpl(long double x, int exp);
double memprof_log10(double x);
float  memprof_log10f(float x);
long double memprof_log10l(long double x);
double memprof_log(double x);
float memprof_logf(float x);
long double memprof_logl(long double x);

double memprof_exp(double x);
float memprof_expf(float x);
long double memprof_expl(long double x);

double memprof_cos(double x);
float memprof_cosf(float x);
long double memprof_cosl(long double x);
double memprof_sin(double x);
double memprof_tan(double x);
float memprof_sinf(float x);
long double memprof_sinl(long double x);

double memprof_atan(double x);
float memprof_atanf(float x);
long double memprof_atanl(long double x);

double memprof_floor(double x);
float memprof_floorf(float x);
long double memprof_floorl(long double x);
double memprof_ceil(double x);
float memprof_ceilf(float x);
long double memprof_ceill(long double x);

double memprof_atan2(double y, double x);
float memprof_atan2f(float y, float x);
long double memprof_atan2l(long double y, long double x);

double memprof_sqrt(double x);
float memprof_sqrtf(float x);
long double memprof_sqrtl(long double x);

double memprof_pow(double x, double y);
float memprof_powf(float x, float y);
long double memprof_powl(long double x, long double y);

double memprof_fabs(double x);
float memprof_fabsf(float x);
long double memprof_fabsl(long double x);

double memprof_modf(double x, double *iptr);
float memprof_modff(float x, float *iptr);
long double memprof_modfl(long double x, long double *iptr);

double memprof_frexp(double num, int *exp);
float memprof_frexpf(float num, int *exp);
long double memprof_frexpl(long double num, int *exp);

/* Misc */

int memprof_getrlimit(int resource, struct rlimit *rlp);
int memprof_setrlimit(int resource, const struct rlimit *rlp);

char *memprof_getenv(const char *name);
char *memprof_getcwd(char *buf, size_t size);

int memprof_system(const char *command);
FILE *memprof_popen(const char *command, const char *mode);
int memprof_pclose(FILE *stream);

typedef void (*sighandler_t)(int);
sighandler_t memprof_signal(int signum, sighandler_t handler);

int memprof_ioctl(int d, int request, ...);
long memprof_sysconf(int name);
int memprof_kill(pid_t pid, int sig);

pid_t memprof_fork(void);
pid_t memprof_wait(int *status);
unsigned int memprof_sleep(unsigned int seconds);

int memprof_execvp(const char *file, char *const argv[]);
int memprof_execv(const char *file, char *const argv[]);
int memprof_execl(const char *path, const char *arg0, ... /*, (char *)0 */);

int memprof_getopt(int argc, char * const argv[], const char *optstring);

/* Compiler/Glibc Internals */
void memprof___assert_fail(const char * assertion, const char * file, unsigned int line, const char * function);
const unsigned short int **memprof___ctype_b_loc(void);
int  memprof__IO_getc(_IO_FILE * __fp);
int  memprof__IO_putc(int __c, _IO_FILE *__fp);
int * memprof___errno_location (void);

int memprof___fxstat (int __ver, int __fildes, struct stat *__stat_buf);
int memprof___xstat (int __ver, __const char *__filename,
                    struct stat *__stat_buf);

long memprof___sysconf(int name);

/* MJB: Can not wrap setjmp - returning invalidates env */
/*int memprof__setjmp (struct __jmp_buf_tag __env[1]);*/
void memprof_longjmp(jmp_buf env, int val);
void memprof_perror(const char *s);

#include <stdint.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>

/* Useful utility functions */

/* The call to exit is to make the compiler not complain about failure to return a value */
#define UNIMPLEMENTED(name) \
    fprintf(stderr, "\"%s\" not implemented!\n", name); \
    abort(); \
    exit(-1);


static void memprof_load_region(const void *ptr, size_t n) {
    if (n == 0) {
	return;
    }

    LOAD(ptr, n);
}

static void memprof_write_region(const void *dest, ssize_t n) {
    if (n < 0) {
	fprintf(stderr, "Illegal value for n\n");
	abort();
    }

    if (n == 0) {
	return;
    }

    STORE(dest, n);
}

static void memprof_allocate_region(const void *dest, ssize_t n) {
    if (n < 0) {
	fprintf(stderr, "Illegal value for n\n");
	abort();
    }

    if (n == 0) {
	return;
    }

#ifndef ALLOCATE
    STORE(dest, n);
#else
    ALLOCATE(dest, n);
#endif
}

static void memprof_deallocate_region(const void *dest, ssize_t n) {
    if (n < 0) {
	fprintf(stderr, "Illegal value for n\n");
	abort();
    }

    if (n == 0) {
	return;
    }

#ifdef DEALLOCATE
    DEALLOCATE(dest, n);
#endif
}


static void memprof_copy_region(const void *dest, const void *src, size_t n) {
    memprof_load_region(src, n);

    memprof_write_region(dest, n);
}

/* Tokens are expected to be uint8_t, just used for the memory address */
static void memprof_load_token(const void *token) {
    memprof_load_region(token, 1);
}

static void memprof_write_token(const void *token) {
    memprof_write_region(token, 1);
}

void functions_init(void) {
    memprof_write_region(&errno, sizeof(errno));
    memprof_write_region(&stdin, sizeof(stdin));
    memprof_write_region(&stderr, sizeof(stderr));
    memprof_write_region(&stdout, sizeof(stdout));
    memprof_write_region(&sys_nerr, sizeof(sys_nerr));

    {
        const unsigned short int *ctype_ptr = (*__ctype_b_loc()) - 128;
	memprof_write_region(ctype_ptr, 384 * sizeof(*ctype_ptr));
    }
    {
	const int32_t * itype_ptr = (*__ctype_tolower_loc()) - 128;
	memprof_write_region(itype_ptr, 384 * sizeof(*itype_ptr));
    }
    {
	const int32_t * itype_ptr = (*__ctype_toupper_loc()) - 128;
	memprof_write_region(itype_ptr, 384 * sizeof(*itype_ptr));
    }
}


/* String family of functions */

static void memprof_load_string(const char *ptr) {
    if (ptr == NULL)
	return;

    memprof_load_region(ptr, strlen(ptr) + 1);
}

size_t memprof_strlen(const char *str) {
    size_t len = strlen(str);
    memprof_load_region(str, len + 1);
    return len;
}

char * memprof_strchr(char *s, int c) {
    char *result = strchr(s, c);
    memprof_load_string(s);
    return result;
}

char * memprof_strrchr(char *s, int c) {
    char *result = strrchr(s, c);
    memprof_load_string(s);
    return result;
}

int    memprof_strcmp(const char *s1, const char *s2) {
    int result = strcmp(s1, s2);
    memprof_load_string(s1);
    memprof_load_string(s2);
    return result;
}

int    memprof_strncmp(const char *s1, const char *s2, size_t n) {
    int result = strncmp(s1, s2, n);
    memprof_load_region(s1, n);
    memprof_load_region(s2, n);
    return result;
}

char * memprof_strcpy(char *dest, const char *src) {
    char *result = strcpy(dest, src);
    memprof_copy_region(dest, src, strlen(src) + 1);
    return result;
}

char * memprof_strncpy(char *dest, const char *src, size_t n) {
    char *result = strncpy(dest, src, n);
    memprof_load_region(src, n);
    memprof_write_region(dest, n);
    return result;
}


char * memprof_strcat(char *s1, const char *s2) {
    char *result;
    int s1_len = strlen(s1);
    int s2_len = strlen(s2);
    memprof_load_string(s1);
    memprof_load_string(s2);
    result = strcat(s1, s2);
    memprof_write_region(s1 + s1_len, s2_len + 1);
    return result;
}


char * memprof_strncat(char *s1, const char *s2, size_t n) {
    char * result;
    memprof_load_string(s1);
    memprof_load_region(s2, n);
    result = strncat(s1, s2, n);
    memprof_write_region(s1 + strlen(s1), n + 1);
    return result;
}

char * memprof_strstr(char *s1, char *s2) {
    memprof_load_string(s1);
    memprof_load_string(s2);
    return strstr(s1, s2);
}

size_t memprof_strspn(const char *s1, const char *s2) {
    memprof_load_string(s1);
    memprof_load_string(s2);
    return strspn(s1, s2);
}

size_t memprof_strcspn(const char *s1, const char *s2) {
    memprof_load_string(s1);
    memprof_load_string(s2);
    return strcspn(s1, s2);
}

char * memprof_strtok(char *s, const char *delim) {
    char *result;
    memprof_load_string(delim);
    result = strtok(s, delim);
    if (result == NULL)
	return NULL;
    memprof_load_string(result);
    return result;
}

char * memprof_strdup(const char *s1) {
    char *result;
    char *newresult;
    size_t slen;

    memprof_load_string(s1);
    result = strdup(s1);
    if (result == NULL)
	return NULL;

    /* Since strdup malloced the memory without going through memprof_malloc
     * we need to do that, since the pointer will go through memprof_free.
     */
    slen = strlen(s1) + 1;
    newresult = (char *) memprof_malloc(slen);
    strncpy(newresult, result, slen);
    free(result);
    result = newresult;

    memprof_write_region(result, strlen(s1) + 1);
    return result;
}

char * memprof_strpbrk(char *s1, char *s2) {
    char * result;
    memprof_load_string(s1);
    memprof_load_string(s2);
    result = strpbrk(s1, s2);
    return result;
}

/* Malloc Family of functions */

typedef struct memory_allocation_t {
    size_t * start;
    size_t * size;
    char *   data;
} mem_alloc_t;

static __inline__ mem_alloc_t get_memory_allocation_true(void * true_ptr) {
    size_t * real_ptr = (size_t *) true_ptr;
    mem_alloc_t memory_alloc = {real_ptr, real_ptr + 1, (char *) (real_ptr + 2)};
    return memory_alloc;
}

static __inline__ mem_alloc_t get_memory_allocation_user(void * user_ptr) {
    size_t * real_ptr = (size_t *) user_ptr;
    real_ptr = real_ptr - 2;
    return get_memory_allocation_true(real_ptr);
}

static uint8_t malloc_token;

void * memprof_malloc(size_t size) {
    void * result = malloc(size + (2 * sizeof(size_t)));
    mem_alloc_t mem_alloc = get_memory_allocation_true(result);
    *(mem_alloc.start) = 0xDEADBEEF;
    *(mem_alloc.size) = size;
    memprof_allocate_region(mem_alloc.data, size);

#ifdef DEBUG_MEMORY
    fprintf(stderr, "M %p\n", mem_alloc.start);
#endif

#ifndef IGNORE_MALLOC
    memprof_load_token(&malloc_token);
    memprof_write_token(&malloc_token);
#endif

    return mem_alloc.data;
}

void * memprof_calloc(size_t nelem, size_t elsize) {
    void *result = memprof_malloc(nelem * elsize);
    memprof_memset(result, 0, nelem * elsize);
    return result;
}

void   memprof_free(void * ptr) {
    mem_alloc_t mem_alloc = get_memory_allocation_user(ptr);

#ifdef DEBUG_MEMORY
    fprintf(stderr, "F %p\n", mem_alloc.start);
#endif

    if (*(mem_alloc.start) != 0xDEADBEEF) {
	fprintf(stderr, "Attempt to a memprof_free a pointer not allocated by memprof_malloc\n");
	abort();
    }

    memprof_deallocate_region(mem_alloc.data, *mem_alloc.size);

#ifndef IGNORE_MALLOC
    memprof_load_token(&malloc_token);
    memprof_write_token(&malloc_token);
#endif

    free(mem_alloc.start);
}

void * memprof_realloc(void *ptr, size_t size) {
    mem_alloc_t orig_alloc = get_memory_allocation_user(ptr);
    mem_alloc_t new_alloc;
    void * result;
    size_t orig_size = *(orig_alloc.size);
    ssize_t diff = size - orig_size;
    char * new_begin_offset;

#ifdef DEBUG_MEMORY
    fprintf(stderr, "R %p\n", orig_alloc.start);
#endif

    if (*(orig_alloc.start) != 0xDEADBEEF) {
	fprintf(stderr, "Attempt to a memprof_realloc a pointer not allocated by memprof_malloc\n");
	abort();
    }

    /* The size of the memory area comes at the start of it, so that is
     * the pointer that actually needs to be realloc'd.
     */
    result = realloc(orig_alloc.start, size + (2 * sizeof(size_t)));

    /* From this moment on, do NOT dereference orig_alloc in any way, realloc may have freed it */

    new_alloc = get_memory_allocation_true(result);
    new_begin_offset = new_alloc.data + orig_size;
    *(new_alloc.size) = 0xDEADBEEF;
    *(new_alloc.size) = size;

    if (orig_alloc.start != new_alloc.start) {
	if (diff <= 0) {
	    abort();
	}

	memprof_allocate_region(new_alloc.data, *(new_alloc.size));

	memprof_copy_region(new_alloc.data, orig_alloc.data, orig_size);
	memprof_write_region(new_begin_offset, diff);

	memprof_deallocate_region(orig_alloc.data, orig_size);
    }

#ifndef IGNORE_MALLOC
    memprof_load_token(&malloc_token);
    memprof_write_token(&malloc_token);
#endif

    return new_alloc.data;
}

int    memprof_brk(void *end_data_segment) {
    return brk(end_data_segment);
}

void * memprof_sbrk(intptr_t increment) {
    return sbrk(increment);
}

/* Mem* and b* family of functions */

void *memprof_memset(void *s, int c, size_t n) {
    void *result = memset(s, c, n);
    memprof_write_region(s, n);
    return result;
}

void *memprof_memmove(void *s1, const void *s2, size_t n) {
    void *result = memmove(s1, s2, n);
    memprof_copy_region(s1, s2, n);
    return result;
}

void *memprof_memcpy(void *s1, const void *s2, size_t n) {
    void *result = memcpy(s1, s2, n);
    memprof_copy_region(s1, s2, n);
    return result;
}

void *memprof___builtin_memcpy(void *s1, const void *s2, size_t n) {
    return memprof_memcpy(s1, s2, n);
}

int   memprof_memcmp(const void *s1, const void *s2, size_t n) {
    int result = memcmp(s1, s2, n);
    memprof_load_region(s1, n);
    memprof_load_region(s2, n);
    return result;
}

void memprof_bzero(void *s, size_t n) {
    bzero(s, n);
    memprof_write_region(s, n);
}

void memprof_bcopy(const void *s1, void *s2, size_t n) {
    bcopy(s1, s2, n);
    memprof_copy_region(s2, s1, n);
}

/* IO */

static uint8_t fd_tokens[1000];

ssize_t memprof_read(int fd, void *buf, size_t count) {
    ssize_t result;
    memprof_load_token(&fd_tokens[fd]);
    result = read(fd, buf, count);
    memprof_write_token(&fd_tokens[fd]);
    memprof_write_region(buf, result);
    return result;
}

ssize_t memprof_write(int fd, const void *buf, size_t count) {
    ssize_t result;
    memprof_load_token(&fd_tokens[fd]);
    memprof_load_region(buf, count);
    result = write(fd, buf, count);
    memprof_write_token(&fd_tokens[fd]);
    return result;
}

int  memprof_open(const char *pathname, int flags, mode_t mode) {
    int result;
    memprof_load_string(pathname);
    result = open(pathname, flags, mode);
    memprof_write_token(&fd_tokens[result]);
    return result;
}

int memprof_close(int fd) {
    memprof_write_token(&fd_tokens[fd]);
    return close(fd);
}

FILE *memprof_fopen(const char *path, const char *mode) {
    FILE *fp;
    memprof_load_string(path);
    memprof_load_string(mode);
    fp = fopen(path, mode);
    if (fp != NULL) {
	memprof_write_token(&fd_tokens[fileno(fp)]);
    }
    return fp;
}

FILE *  memprof_fdopen(int fildes, const char *mode) {
    FILE *fp;
    memprof_load_string(mode);
    fp = fdopen(fildes, mode);
    if (fp != NULL) {
	memprof_write_token(&fd_tokens[fildes]);
    }
    return fp;
}

size_t memprof_fread(void *ptr, size_t size, size_t nitems, FILE *stream) {
    size_t result;
    result = fread(ptr, size, nitems, stream);
    memprof_load_token(&fd_tokens[fileno(stream)]);
    memprof_write_region(ptr, result * size);
    memprof_write_token(&fd_tokens[fileno(stream)]);
    return result;
}

size_t  memprof_fwrite(const void *ptr, size_t size, size_t nitems, FILE *stream) {
    size_t result;
    memprof_load_token(&fd_tokens[fileno(stream)]);
    result = fwrite(ptr, size, nitems, stream);
    memprof_load_region(ptr, size * result);
    memprof_write_token(&fd_tokens[fileno(stream)]);
    return result;
}

void memprof_rewind(FILE *stream) {
    memprof_write_token(&fd_tokens[fileno(stream)]);
    rewind(stream);
}

int memprof_fseek(FILE *stream, long offset, int whence) {
    memprof_load_token(&fd_tokens[fileno(stream)]);
    memprof_write_token(&fd_tokens[fileno(stream)]);
    return fseek(stream, offset, whence);
}

off_t memprof_lseek(int filedes, off_t offset, int whence) {
    memprof_load_token(&fd_tokens[filedes]);
    memprof_write_token(&fd_tokens[filedes]);
    return lseek(filedes, offset, whence);
}

int memprof_fclose(FILE *stream) {
    memprof_write_token(&fd_tokens[fileno(stream)]);
    return fclose(stream);
}

int memprof_fflush(FILE *stream) {
    /* MJB: Since we're going to just supress fflush, ignore it here */
    return fflush(stream);
}

int memprof_ferror(FILE *stream) {
    memprof_load_token(&fd_tokens[fileno(stream)]);
    return ferror(stream);
}

void memprof_clearerr(FILE *stream) {
    memprof_write_token(&fd_tokens[fileno(stream)]);
    clearerr(stream);
}

int memprof_feof(FILE *stream) {
    memprof_load_token(&fd_tokens[fileno(stream)]);
    return feof(stream);
}

int memprof_fgetc(FILE *stream) {
    memprof_load_token(&fd_tokens[fileno(stream)]);
    memprof_write_token(&fd_tokens[fileno(stream)]);
    return fgetc(stream);
}

int memprof_fputc(int c, FILE *stream) {
    int result = memprof_fprintf(stream, "%c", c);
    if (result == 0) {
	return EOF;
    }
    return (unsigned char) c;
}

long memprof_ftell(FILE *stream) {
    long result;
    memprof_load_token(&fd_tokens[fileno(stream)]);
    result = ftell(stream);
    return result;
}

char * memprof_fgets(char *s, int n, FILE *stream) {
    memprof_load_token(&fd_tokens[fileno(stream)]);
    char *result = fgets(s, n, stream);
    if (result == NULL)
	return result;

    /* MJB: I believe that if NULL is returned, nothing it changed */
    memprof_write_token(&fd_tokens[fileno(stream)]);
    memprof_write_region(result, strlen(result) + 1);
    return result;
}

/* char * memprof_gets(char *s) { */
/*     memprof_load_token(&fd_tokens[fileno(stdin)]); */
/*     char *result = gets(s); */
/*     if (result == NULL) */
/* 	return result; */

/*     /\* MJB: I believe that if NULL is returned, nothing it changed *\/ */
/*     memprof_write_token(&fd_tokens[fileno(stdin)]); */
/*     memprof_write_region(result, strlen(result) + 1); */
/*     return result; */
/* } */

int     memprof_fputs(const char *s, FILE *stream) {
    return memprof_fprintf(stream, "%s", s);
}

int     memprof_puts(const char *s) {
    return memprof_fprintf(stdout, "%s\n", s);
}

int     memprof_ungetc(int c, FILE *stream) {
    memprof_load_token(&fd_tokens[fileno(stream)]);
    int result = ungetc(c, stream);
    memprof_write_token(&fd_tokens[fileno(stream)]);
    return result;
}

int     memprof_putchar(int c) {
    return memprof_fputc(c, stdout);
}

int     memprof_getchar() {
    return memprof_fgetc(stdin);
}

char *  memprof_ttyname(int filedes) {
    char *result;
    memprof_load_token(&fd_tokens[filedes]);
    result = ttyname(filedes);
    memprof_write_region(result, strlen(result) + 1);
    return result;
}

/* MJB: I'm not sure that this function is correctly annotated */
char * memprof_tmpnam(char *s) {
    char *result;
    memprof_load_string(s);
    result = tmpnam(s);
    memprof_write_region(result, strlen(result) + 1);
    return result;
}

FILE* memprof_tmpfile() {
    FILE *fp = tmpfile();
    memprof_load_token(&fd_tokens[fileno(fp)]);
    return fp;
}

int     memprof_fileno(FILE *stream) {
    int result = fileno(stream);
    memprof_load_token(&fd_tokens[result]);
    return result;
}

int     memprof_select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    UNIMPLEMENTED("select");
}

int     memprof_remove(const char *path) {
    memprof_load_string(path);
    return remove(path);
}

void    memprof_setbuf(FILE * stream, char * buf) {
    memprof_write_token(&fd_tokens[fileno(stream)]);
    setbuf(stream, buf);
}

int memprof_truncate(const char *path, off_t length) {
    UNIMPLEMENTED("truncate");
}

int memprof_ftruncate(int fildes, off_t length) {
    UNIMPLEMENTED("ftruncate");
}

int memprof_dup(int oldfd) {
    UNIMPLEMENTED("dup");
}

int memprof_dup2(int oldfd, int newfd) {
    UNIMPLEMENTED("dup2");
}

int memprof_pipe(int filedes[2]) {
    UNIMPLEMENTED("pipe");
}

int memprof_chmod(const char *path, mode_t mode) {
    memprof_load_string(path);
    return chmod(path, mode);
}

int memprof_fchmod(int fildes, mode_t mode){
    memprof_write_token(&fd_tokens[fildes]);
    return fchmod(fildes, mode);
}

int memprof_fchown(int fd, uid_t owner, gid_t group){
    memprof_write_token(&fd_tokens[fd]);
    return fchown(fd, owner, group);
}

int memprof_access(const char *pathname, int mode){
    memprof_load_string(pathname);
    return access(pathname, mode);
}

long memprof_pathconf(char *path, int name){
    memprof_load_string(path);
    return pathconf(path, name);

}
int memprof_mkdir(const char *pathname, mode_t mode) {
    memprof_load_string(pathname);
    return mkdir(pathname, mode);
}

int memprof_rmdir(const char *pathname) {
    memprof_load_string(pathname);
    return rmdir(pathname);
}

mode_t memprof_umask(mode_t mask) {
    return umask(mask);
}

/* This is a very dangerous function - pray it doesn't do things like dup file descriptors */
int memprof_fcntl(int fd, int cmd, struct flock *lock) {
    int result;
    memprof_load_token(&fd_tokens[fd]);
    memprof_write_token(&fd_tokens[fd]);
    result = fcntl(fd, cmd, lock);
    return result;
}

/* Printf family of functions */
#define IS_STRING(byte)   ((byte == 's'))
#define IS_DOUBLE(byte)   ((byte == 'f') || (byte == 'F') || (byte == 'e') || (byte == 'E') || (byte == 'g') || (byte == 'G'))
#define IS_INT(byte)      ((byte == 'd') || (byte == 'i') || (byte == 'X') || (byte == 'x') || (byte == 'o') || (byte == 'u') || (byte == 'c'))
#define IS_LONG_INT(byte) ((byte == 'D') || (byte == 'O') || (byte == 'U'))
#define IS_VOID_PTR(byte) ((byte == 'p'))

static uint8_t is_format_char(char byte) {
    return (IS_STRING(byte) || IS_DOUBLE(byte) || IS_INT(byte) || IS_LONG_INT(byte) || IS_VOID_PTR(byte));
}

static uint8_t is_half(const char *ptr) {
    return ptr[-1] == 'h';
}

static uint8_t is_halfhalf(const char *ptr) {
    return is_half(ptr) && ptr[-2] == 'h';
}

static uint8_t is_long(const char *ptr) {
    return ptr[-1] == 'l';
}

/* MJB: We need to touch vp, if that is legal */
static void touch_printf_args(const char *format, va_list vp) {
    char byte;
    while ((byte = *format++) != '\0') {
	if (byte != '%') {
#ifdef LIBC_FUNC_DEBUG
	    fprintf(stderr, "touch_printf_arg: Ignoring \"%c\"\n", byte);
#endif
	    continue;
	}

	/* Go to the next character after the % */
	byte = *format++;
	if (byte == '\0') {
	    goto ERROR;
	}

	if (byte == '%') {
#ifdef LIBC_FUNC_DEBUG
	    fprintf(stderr, "touch_printf_args: Ignoring \"%c\"\n", byte);
#endif
	    continue;
	}

	while (!is_format_char(byte)) {
#ifdef LIBC_FUNC_DEBUG
	    fprintf(stderr, "touch_printf_args: Adding Format Character \"%c\"\n", byte);
#endif

	    byte = *format++;
	    if (byte == '\0') {
		goto ERROR;
	    }
	}
	/* Go back one character since the loop backedge will take us forward one character again */
	format--;

#ifdef LIBC_FUNC_DEBUG
	fprintf(stderr, "touch_printf_args: Format Spec \"%c\"\n", byte);
#endif


	// MJB: We're only interested in advancing the underlying va_arg pointer for most formats
	// thus the va_arg result is unused.
	if (IS_STRING(byte)) {
	    char *str_arg = va_arg(vp, char *);
	    memprof_load_string(str_arg);
	} else if (IS_INT(byte)) {
	    if (is_long(format)) {
		long int_arg __attribute__ ((unused));
		int_arg = va_arg(vp, long);
	    } else {
		int int_arg __attribute__ ((unused));
		int_arg = va_arg(vp, int);
	    }
	} else if (IS_DOUBLE(byte)) {
	    double real_arg  __attribute__ ((unused));
	    real_arg = va_arg(vp, double);
	} else if (IS_LONG_INT(byte)) {
	    long int long_int_arg  __attribute__ ((unused));
	    long_int_arg = va_arg(vp, long int);
	} else if (IS_VOID_PTR(byte)) {
	    void *void_ptr_arg  __attribute__ ((unused));
	    void_ptr_arg = va_arg(vp, void *);
	} else {
	    fprintf(stderr, "Unknown type\n");
	    abort();
	}
    }

    return;

 ERROR:
    {
	fprintf(stderr, "printf_args: Error in format string encountered\n");
	abort();
    }
}

int memprof_printf(const char *format, ...) {
    va_list ap;
    int size;
    va_start(ap, format);
    size = memprof_vprintf(format, ap);
    va_end(ap);
    return size;
}

int memprof_fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    int size;
    va_start(ap, format);
    size = memprof_vfprintf(stream, format, ap);
    va_end(ap);
    return size;
}

int memprof_sprintf(char *str, const char *format, ...) {
    va_list ap;
    int size;
    va_start(ap, format);
    size = memprof_vsprintf(str, format, ap);
    va_end(ap);
    return size;
}

int memprof_snprintf(char *str, size_t size, const char *format, ...){
    va_list ap;
    int result;
    va_start(ap, format);
    result = memprof_vsnprintf(str, size, format, ap);
    va_end(ap);
    return result;
}

int memprof_vprintf(const char *format, va_list ap) {
    int size;
    size = memprof_vfprintf(stdout, format, ap);
    return size;
}

int memprof_vfprintf(FILE *stream, const char *format, va_list ap) {
    int size;
    memprof_load_token(&fd_tokens[fileno(stream)]);
    memprof_load_string(format);
    touch_printf_args(format, ap);
    size = vfprintf(stream, format, ap);
    memprof_write_token(&fd_tokens[fileno(stream)]);
    return size;
}

int memprof_vsprintf(char *str, const char *format, va_list ap) {
    int size;
    memprof_load_string(format);
    touch_printf_args(format, ap);
    size = vsprintf(str, format, ap);
    memprof_write_region(str, size);
    return size;
}

int memprof_vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    int result;
    memprof_load_string(format);
    touch_printf_args(format, ap);
    result = vsnprintf(str, size, format, ap);
    memprof_write_region(str, result);
    return result;
}

/* Scanf family of functions */

/* MJB: We need to touch vp, if that is legal */
static void touch_scanf_args(const char *format, va_list vp) {
    char byte;
    uint8_t ignore_store;

    while ((byte = *format++) != '\0') {
	ignore_store = 0;

	if (byte != '%') {
#ifdef LIBC_FUNC_DEBUG
	    fprintf(stderr, "touch_scanf_args: Adding \"%c\"\n", byte);
#endif
	    continue;
	}

	/* Go to the next character after the % */
	byte = *format++;
	if (byte == '\0') {
	    goto ERROR;
	}

	if (byte == '%') {
#ifdef LIBC_FUNC_DEBUG
	    fprintf(stderr, "touch_scanf_args: Ignoring \"%c\"\n", byte);
#endif
	    continue;
	}

	if (byte == '*') {
	    ignore_store = 1;
	    byte = *format++;
	}

	while (!is_format_char(byte)) {
#ifdef LIBC_FUNC_DEBUG
	    fprintf(stderr, "touch_scanf_args: Adding format \"%c\"\n", byte);
#endif

	    byte = *format++;
	    if (byte == '\0') {
		goto ERROR;
	    }
	}
	/* Go back one character since the loop backedge will take us forward one character again */
	format--;

	if (ignore_store)
	    continue;

#ifdef LIBC_FUNC_DEBUG
	fprintf(stderr, "touch_scanf_args: Format Spec \"%c\"\n", byte);
#endif

	/* Note that va_arg is needed for each case to advance the underlying pointer */
	if (IS_STRING(byte)) {
	    char *str_arg = va_arg(vp, char *);
	    memprof_load_string(str_arg);
	} else if (IS_INT(byte)) {
	    if (is_halfhalf(format)) {
		char *arg = va_arg(vp, char *);
		memprof_write_region(arg, sizeof(*arg));
	    } else if (is_half(format)) {
		short *arg = va_arg(vp, short *);
		memprof_write_region(arg, sizeof(*arg));
	    } else if (is_long(format)) {
		long *arg = va_arg(vp, long *);
		memprof_write_region(arg, sizeof(*arg));
	    } else {
		int *arg = va_arg(vp, int *);
		memprof_write_region(arg, sizeof(*arg));
	    }
	} else if (IS_DOUBLE(byte)) {
	    double *arg = va_arg(vp, double *);
	    memprof_write_region(arg, sizeof(*arg));
	} else if (IS_LONG_INT(byte)) {
	    long int *arg = va_arg(vp, long int *);
	    memprof_write_region(arg, sizeof(*arg));
	} else if (IS_VOID_PTR(byte)) {
	    void **arg = va_arg(vp, void **);
	    memprof_write_region(arg, sizeof(*arg));
	} else {
	    fprintf(stderr, "Unknown type\n");
	    abort();
	}
    }

    return;

 ERROR:
    {
	fprintf(stderr, "touch_scanf_args: Error in format string encountered\n");
	abort();
    }
}

int memprof_fscanf(FILE *stream, const char *format, ... ) {
    va_list ap;
    int result;
    va_start(ap, format);
    result = memprof_vfscanf(stream, format, ap);
    va_end(ap);
    return result;
}

int memprof_scanf(const char *format, ... ) {
    va_list ap;
    int result;
    va_start(ap, format);
    result = memprof_vscanf(format, ap);
    va_end(ap);
    return result;
}

int memprof_sscanf(const char *s, const char *format, ... ) {
    va_list ap;
    int result;
    va_start(ap, format);
    result = memprof_vsscanf(s, format, ap);
    va_end(ap);
    return result;
}

int memprof_vscanf(const char *format, va_list arg) {
    int result;
    result = memprof_vfscanf(stdin, format, arg);
    return result;
}

int memprof_vfscanf(FILE *stream, const char *format, va_list arg) {
    int result;
    memprof_load_token(&fd_tokens[fileno(stream)]);
    memprof_load_string(format);
    result = vfscanf(stream, format, arg);
    touch_scanf_args(format, arg);
    memprof_write_token(&fd_tokens[fileno(stream)]);
    return result;
}

int memprof_vsscanf(const char *s, const char *format, va_list arg) {
    int result;
    memprof_load_string(format);
    memprof_load_string(s);
    result = vsscanf(s, format, arg);
    touch_scanf_args(format, arg);
    return result;
}

/* Time functions */

/* Note here that we don't catch the return structure because we have no idea
 * where in memory that will actually go, if it goes in memory at all.
 */
time_t memprof_time(time_t *t) {
    time_t result = time(t);
    if (t != NULL) {
	memprof_write_region(t, sizeof(*t));
    }
    return result;
}

char * memprof_ctime(const time_t *clock) {
    char * result;
    memprof_load_region(clock, sizeof(*clock));
    result = ctime(clock);
    memprof_write_region(result, strlen(result) + 1);
    return result;
}

char * memprof_ctime_r(const time_t *clock, char *buf) {
    char *result;
    memprof_load_region(clock, sizeof(*clock));
    result = ctime_r(clock, buf);
    memprof_write_region(result, strlen(result) + 1);
    return result;
}

int memprof_ftime(struct timeb *tp) {
    int result;
    result = ftime(tp);
    memprof_write_region(tp, sizeof(*tp));
    return result;
}

struct tm *memprof_localtime(const time_t *timer) {
    struct tm *result;
    memprof_load_region(timer, sizeof(*timer));
    result = localtime(timer);
    memprof_write_region(result, sizeof(*result));
    return result;
}
int memprof_gettimeofday(struct timeval *tv, struct timezone *tz){
    int result = gettimeofday(tv, tz);
    memprof_write_region(tv, sizeof(*tv));
    memprof_write_region(tz, sizeof(struct timezone));
    return result;
}
size_t memprof_strftime(char *s, size_t maxsize,
			const char *format, const struct tm *timeptr) {
    size_t result;
    memprof_load_string(format);
    memprof_load_region(timeptr, sizeof(*timeptr));
    result = strftime(s, maxsize, format, timeptr);
    memprof_write_region(s, result);
    return result;
}

double memprof_difftime(time_t time1, time_t time0) {
    return difftime(time1, time0);
}

clock_t memprof_clock(void) {
    return clock();
}

clock_t memprof_times(struct tms *buf) {
    clock_t result;
    result = times(buf);
    memprof_write_region(buf, sizeof(*buf));
    return result;
}

int memprof_utime(const char *filename, const struct utimbuf *buf) {
    int result;
    memprof_load_string(filename);
    memprof_load_region(buf, sizeof(*buf));
    result = utime(filename, buf);
    return result;
}

int memprof_utimes(char *filename, struct timeval *tvp){
    int result;
    memprof_load_string(filename);
    memprof_load_region(tvp, sizeof(*tvp));
    result = utimes(filename, tvp);
    return result;
}

struct tm *memprof_gmtime(const time_t *timer) {
    struct tm *result;
    memprof_load_region(timer, sizeof(*timer));
    result = gmtime(timer);
    memprof_write_region(result, sizeof(*result));
    return result;
}

/* Convert network byte ordering */
uint32_t memprof_htonl(uint32_t hostlong) {
    return htonl(hostlong);
}

uint16_t memprof_htons(uint16_t hostshort) {
    return htons(hostshort);
}

uint32_t memprof_ntohl(uint32_t netlong) {
    return ntohl(netlong);
}

uint16_t memprof_ntohs(uint16_t netshort) {
    return ntohs(netshort);
}

/* Random number generation */

static uint8_t rand_token;
static uint8_t rand48_token;

int memprof_rand(void) {
    memprof_load_token(&rand_token);
    memprof_write_token(&rand_token);
    return rand();
}

void memprof_srand(unsigned seed) {
    memprof_write_token(&rand_token);
    srand(seed);
}
long int memprof_lrand48(void){
    memprof_load_token(&rand48_token);
    memprof_write_token(&rand48_token);
    return lrand48();
}
double memprof_drand48(void){
    memprof_load_token(&rand48_token);
    memprof_write_token(&rand48_token);
    return drand48();
}

/* MISC */
int  memprof_rename(const char *oldpath, const char *newpath) {
    int result;
    memprof_load_string(oldpath);
    memprof_load_string(newpath);
    result = rename(oldpath, newpath);
    return result;
}

int memprof_getrusage(int who, struct rusage *rusage) {
    int result = getrusage(who, rusage);
    memprof_write_region(rusage, sizeof(*rusage));
    return result;
}

int memprof_getrlimit(int resource, struct rlimit *rlp) {
    int result;
    result = getrlimit(resource, rlp);
    memprof_write_region(rlp, sizeof(*rlp));
    return result;
}

int memprof_setrlimit(int resource, const struct rlimit *rlp) {
    int result;
    memprof_load_region(rlp, sizeof(*rlp));
    result = setrlimit(resource, rlp);
    return result;
}

int memprof_unlink(const char *pathname) {
    int result;
    memprof_load_string(pathname);
    result = unlink(pathname);
    return result;
}

int memprof_tolower(int c) {
    return tolower(c);
}

int memprof_toupper(int c) {
    return toupper(c);
}

void memprof_abort(void) {
    abort();
}

void memprof_exit(int status) {
    exit(status);
}

void memprof__exit(int status) {
    _exit(status);
}

int memprof_atexit(void (*func)(void)) {
    return atexit(func);
}

int memprof_atoi(const char *str) {
    memprof_load_string(str);
    return atoi(str);
}

double memprof_strtod(const char *nptr, char **endptr){
    memprof_load_string(nptr);
    double result = strtod(nptr, endptr);
    memprof_write_region(endptr, sizeof(char *));
    return result;
}

long int memprof_strtol(const char *nptr, char **endptr, int base){
    memprof_load_string(nptr);
    long int result = strtol(nptr, endptr, base);
    memprof_write_region(endptr, sizeof(char *));
    return result;
}

long memprof_atol(const char *str) {
    memprof_load_string(str);
    return atol(str);
}

double memprof_atof(const char *str) {
    memprof_load_string(str);
    return atof(str);
}

int  memprof_isatty(int desc) {
    return isatty(desc);
}

int memprof_tcgetattr(int fd, struct termios *termios_p){
    int result = tcgetattr(fd, termios_p);
    memprof_write_region(termios_p, sizeof(struct termios));
    return result;
}

int memprof_tcsetattr(int fd, int optional_actions, struct termios *termios_p){
    memprof_load_region(termios_p,  sizeof(struct termios));
    return tcsetattr(fd, optional_actions, termios_p);
}

/* Msth Functions */

double memprof_ldexp(double x, int exp) {
    return ldexp(x, exp);
}

float  memprof_ldexpf(float x, int exp) {
    return ldexpf(x, exp);
}

long double memprof_ldexpl(long double x, int exp) {
    return ldexpl(x, exp);
}

double memprof_exp(double x) {
    return exp(x);
}

float memprof_expf(float x) {
    return expf(x);
}

long double memprof_expl(long double x) {
    return expl(x);
}

double memprof_log10(double x) {
    return log10(x);
}

float  memprof_log10f(float x) {
    return log10f(x);
}

long double memprof_log10l(long double x) {
    return log10l(x);
}

double memprof_log(double x) {
    return log(x);
}

float memprof_logf(float x) {
    return logf(x);
}

long double memprof_logl(long double x) {
    return logl(x);
}

double memprof_pow(double x, double y) {
    return pow(x,y);
}

float memprof_powf(float x, float y) {
    return  powf(x, y);
}

long double memprof_powl(long double x, long double y) {
    return powl(x,y);
}

double memprof_cos(double x) {
    return cos(x);
}

float memprof_cosf(float x) {
    return cosf(x);
}

long double memprof_cosl(long double x) {
    return cosl(x);
}

double memprof_sin(double x) {
    return sin(x);
}

double memprof_tan(double x) {
    return tan(x);
}
float memprof_sinf(float x) {
    return sinf(x);
}

long double memprof_sinl(long double x) {
    return sinl(x);
}

double memprof_atan(double x) {
    return atan(x);
}

float memprof_atanf(float x) {
    return atanf(x);
}

long double memprof_atanl(long double x) {
    return atanl(x);
}

double memprof_atan2(double y, double x) {
    return atan2(y,x);
}

float memprof_atan2f(float y, float x) {
    return atan2f(y,x);
}

long double memprof_atan2l(long double y, long double x) {
    return atan2l(y,x);
}

double memprof_modf(double x, double *iptr) {
    double result;
    result = modf(x, iptr);
    memprof_write_region(iptr, sizeof(*iptr));
    return result;
}

float memprof_modff(float x, float *iptr) {
    float result;
    result = modff(x, iptr);
    memprof_write_region(iptr, sizeof(*iptr));
    return result;
}

long double memprof_modfl(long double x, long double *iptr) {
    long double result;
    result = modfl(x, iptr);
    memprof_write_region(iptr, sizeof(*iptr));
    return result;
}

double memprof_frexp(double num, int *exp) {
    double result;
    result = frexp(num, exp);
    memprof_write_region(exp, sizeof(*exp));
    return result;
}

float memprof_frexpf(float num, int *exp) {
    float result;
    result = frexpf(num, exp);
    memprof_write_region(exp, sizeof(*exp));
    return result;
}

long double memprof_frexpl(long double num, int *exp) {
    long double result;
    result = frexpl(num, exp);
    memprof_write_region(exp, sizeof(*exp));
    return result;
}

double memprof_floor(double x) {
    return floor(x);
}

float memprof_floorf(float x) {
    return floorf(x);
}

long double memprof_floorl(long double x) {
    return floorl(x);
}

double memprof_ceil(double x) {
    return ceil(x);
}

float memprof_ceilf(float x) {
    return ceilf(x);
}

long double memprof_ceill(long double x) {
    return ceill(x);
}

double memprof_sqrt(double x) {
    return sqrt(x);
}

float memprof_sqrtf(float x) {
    return sqrtf(x);
}

long double memprof_sqrtl(long double x) {
    return sqrtl(x);
}

double memprof_fabs(double x) {
    return fabs(x);
}

float memprof_fabsf(float x) {
    return fabsf(x);
}

long double memprof_fabsl(long double x) {
    return fabsl(x);
}

char* memprof_strerror(int errnum) {
    char * result = strerror(errnum);
    if (result != NULL) {
        memprof_write_region(result, strlen(result) + 1);
    }
    return result;
}

char *memprof_getenv(const char *name) {
    char *result;
    memprof_load_string(name);
    result = getenv(name);
    if (result != NULL) {
	memprof_write_region(result, strlen(result) + 1);
    }
    return result;
}

char *memprof_getcwd(char *buf, size_t size) {
    char *result = getcwd(buf, size);
    if (result != NULL) {
	memprof_write_region(buf, size);
    }
    return result;
}

int memprof_execvp(const char *file, char *const argv[]) {
    UNIMPLEMENTED("execvp");
}

int memprof_execv(const char *file, char *const argv[]) {
    UNIMPLEMENTED("execv");
}

int memprof_execl(const char *path, const char *arg0, ... /*, (char *)0 */) {
    UNIMPLEMENTED("execl");
}

/* Compiler/Glibc internals */
void memprof___assert_fail(const char * assertion, const char * file, unsigned int line, const char * function) {
#ifndef NDEBUG
    __assert_fail(assertion, file, line, function);
#endif
}

const unsigned short int **memprof___ctype_b_loc(void) {
    return __ctype_b_loc();
}

int memprof__IO_getc(_IO_FILE * __fp) {
    memprof_load_token(&fd_tokens[fileno(__fp)]);
    memprof_write_token(&fd_tokens[fileno(__fp)]);
    return _IO_getc(__fp);
}

int memprof__IO_putc(int __c, _IO_FILE *__fp) {
    memprof_load_token(&fd_tokens[fileno(__fp)]);
    memprof_write_token(&fd_tokens[fileno(__fp)]);
    return _IO_putc(__c, __fp);
}

/* MJB: Handle this */
int * memprof___errno_location (void) {
    return __errno_location();
}

int memprof___xstat (int __ver, __const char *__filename,
		     struct stat *__stat_buf) {
    int result = __xstat(__ver, __filename, __stat_buf);
    memprof_load_string(__filename);
    memprof_write_region(__stat_buf, sizeof(*__stat_buf));
    return result;
}

int memprof___fxstat (int __ver, int __fildes, struct stat *__stat_buf) {
    memprof_load_token(&fd_tokens[__fildes]);
    int result = __fxstat(__ver, __fildes, __stat_buf);
    memprof_write_region(__stat_buf, sizeof(*__stat_buf));
    return result;
}

/* MJB: You can't actually wrap setjmp, as returning from the function that
 * calls setjmp invalidates the struct
*/
/*int memprof__setjmp (struct __jmp_buf_tag __env[1]) {
    return _setjmp(__env);
    }*/

/* MJB: Not handled correctly */
void memprof_longjmp(jmp_buf env, int val) {
    longjmp(env, val);
}

void memprof_perror(const char *s) {
    memprof_load_token(&fd_tokens[fileno(stderr)]);
    memprof_write_token(&fd_tokens[fileno(stderr)]);
    memprof_load_string(s);
    perror(s);
}

int memprof_system(const char *command) {
    UNIMPLEMENTED("system");
}

FILE *memprof_popen(const char *command, const char *mode) {
    UNIMPLEMENTED("popen");
}

int memprof_pclose(FILE *stream) {
    UNIMPLEMENTED("pclose");
}

/* MJB: Not handled correctly */
sighandler_t memprof_signal(int signum, sighandler_t handler) {
    sighandler_t result = signal(signum, handler);
    return result;
}

int memprof_ioctl(int d, int request, ...) {
    return ioctl(d, request);
}

long memprof_sysconf(int name) {
    return sysconf(name);
}

int memprof_setuid(uid_t uid) {
    return setuid(uid);
}

uid_t memprof_getuid(void) {
    return getuid();
}

uid_t memprof_geteuid(void) {
    return geteuid();
}

int memprof_setgid(gid_t gid) {
    return setgid(gid);
}

gid_t memprof_getgid(void) {
    return getgid();
}

gid_t memprof_getegid(void) {
    return getegid();
}

pid_t memprof_getpid(void) {
    return getpid();
}

pid_t memprof_getppid(void) {
    return getppid();
}

int memprof_chdir(const char *path) {
    int result;
    memprof_load_string(path);
    result = chdir(path);
    return result;
}

int memprof_kill(pid_t pid, int sig) {
    UNIMPLEMENTED("kill");
}

pid_t memprof_fork(void) {
    UNIMPLEMENTED("fork");
}

pid_t memprof_wait(int *status) {
    UNIMPLEMENTED("wait");
}

unsigned int memprof_sleep(unsigned int seconds) {
    return sleep(seconds);
}

//extern long __sysconf(int name);

long memprof___sysconf(int name) {
    return sysconf(name);
}

int memprof_getopt(int argc, char * const argv[], const char *optstring) {
    int i = 0;

    memprof_load_string(optstring);
    while (argv[i] != NULL) {
	    memprof_load_string(argv[i]);
        i++;
    }

    return getopt(argc, argv, optstring);
}


#undef UNIMPLEMENTED

#ifdef __cplusplus
}
#endif

#endif
