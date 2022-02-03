#ifndef SLAMPLIB_HOOKS_SLAMP_HOOKS_H
#define SLAMPLIB_HOOKS_SLAMP_HOOKS_H

#include <dirent.h>
#include <stdint.h>
#include <sys/types.h>
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
#include <limits.h>
#include <wait.h>
#include <langinfo.h>

#ifdef __cplusplus
extern "C" {
#endif

void SLAMP_dbggv(int id);
void SLAMP_dbggvstr(char* str);

void SLAMP_init(uint32_t fn_id, uint32_t loop_id);
void SLAMP_fini(const char* filename);
void SLAMP_allocated(uint64_t addr);
void SLAMP_init_global_vars(uint64_t addr, size_t size);
void SLAMP_main_entry(uint32_t argc, char** argv, char** env);

void SLAMP_loop_invocation();
void SLAMP_loop_iteration();
void SLAMP_loop_exit();

void SLAMP_push(const uint32_t instr);
void SLAMP_pop();

void SLAMP_load1(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value);
void SLAMP_load2(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value);
void SLAMP_load4(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value);
void SLAMP_load8(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value);
void SLAMP_loadn(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, size_t n);

void SLAMP_load1_ext(const uint64_t addr, const uint32_t bare_instr, uint64_t value);
void SLAMP_load2_ext(const uint64_t addr, const uint32_t bare_instr, uint64_t value);
void SLAMP_load4_ext(const uint64_t addr, const uint32_t bare_instr, uint64_t value);
void SLAMP_load8_ext(const uint64_t addr, const uint32_t bare_instr, uint64_t value);
void SLAMP_loadn_ext(const uint64_t addr, const uint32_t bare_instr, size_t n);

void SLAMP_store1(uint32_t instr, const uint64_t addr);
void SLAMP_store2(uint32_t instr, const uint64_t addr);
void SLAMP_store4(uint32_t instr, const uint64_t addr);
void SLAMP_store8(uint32_t instr, const uint64_t addr);
void SLAMP_storen(uint32_t instr, const uint64_t addr, size_t n);

void SLAMP_store1_ext(const uint64_t addr, const uint64_t bare_inst);
void SLAMP_store2_ext(const uint64_t addr, const uint64_t bare_inst);
void SLAMP_store4_ext(const uint64_t addr, const uint64_t bare_inst);
void SLAMP_store8_ext(const uint64_t addr, const uint64_t bare_inst);
void SLAMP_storen_ext(const uint64_t addr, const uint64_t bare_inst, size_t n);

/* wrappers */

void* SLAMP_malloc(size_t size);
void* SLAMP_calloc(size_t nelem, size_t elsize);
void* SLAMP_realloc(void* ptr, size_t size);
void* SLAMP__Znam(size_t size);
void* SLAMP__Znwm(size_t size);

char* SLAMP_strdup(const char *s1);
char* SLAMP___strdup(const char *s1);
void  SLAMP_free(void* ptr);
void  SLAMP_cfree(void* ptr);
void  SLAMP__ZdlPv(void* ptr);
void  SLAMP__ZdaPv(void* ptr);
int   SLAMP_brk(void *end_data_segment);
void* SLAMP_sbrk(intptr_t increment);

/* llvm memory intrinsics */
void SLAMP_llvm_memcpy_p0i8_p0i8_i32(const uint8_t* dstAddr, const uint8_t* srcAddr, const uint32_t sizeBytes);
void SLAMP_llvm_memcpy_p0i8_p0i8_i64(const uint8_t* dstAddr, const uint8_t* srcAddr, const uint64_t sizeBytes);
void SLAMP_llvm_memmove_p0i8_p0i8_i32(const uint8_t* dstAddr, const uint8_t* srcAddr, const uint32_t sizeBytes);
void SLAMP_llvm_memmove_p0i8_p0i8_i64(const uint8_t* dstAddr, const uint8_t* srcAddr, const uint64_t sizeBytes);
void SLAMP_llvm_memset_p0i8_i32(const uint8_t* dstAddr, const uint32_t len);
void SLAMP_llvm_memset_p0i8_i64(const uint8_t* dstAddr, const uint64_t len);

void SLAMP_llvm_lifetime_start_p0i8(uint64_t size, uint8_t* ptr);
void SLAMP_llvm_lifetime_end_p0i8(uint64_t size, uint8_t* ptr);

/* String functions */
size_t SLAMP_strlen(const char *str);
char* SLAMP_strchr(char *s, int c);
char* SLAMP_strrchr(char *s, int c);
int SLAMP_strcmp(const char *s1, const char *s2);
int SLAMP_strncmp(const char *s1, const char *s2, size_t n);
char* SLAMP_strcpy(char *dest, const char *src);
char* SLAMP_strncpy(char *dest, const char *src, size_t n);
char* SLAMP_strcat(char *s1, const char *s2);
char* SLAMP_strncat(char *s1, const char *s2, size_t n);
char* SLAMP_strstr(char *s1, char *s2);
size_t SLAMP_strspn(const char *s1, const char *s2);
size_t SLAMP_strcspn(const char *s1, const char *s2);
char* SLAMP_strtok(char *s, const char *delim);
double SLAMP_strtod(const char *nptr, char **endptr);
long int SLAMP_strtol(const char *nptr, char **endptr, int base);
char* SLAMP_strpbrk(char *s1, char *s2);

/* Mem* and b* functions */
void *SLAMP_memset (void *dest, int c, size_t n);
void *SLAMP_memcpy (void *dest, const void *src, size_t n);
void *SLAMP___builtin_memcpy (void *dest, const void *src, size_t n);
void *SLAMP_memmove (void *dest, const void *src, size_t n);
int   SLAMP_memcmp(const void *s1, const void *s2, size_t n);
void* SLAMP_memchr(void* ptr, int value, size_t num);
void* SLAMP___rawmemchr(void* ptr, int value);

void  SLAMP_bzero(void *s, size_t n);
void  SLAMP_bcopy(const void *s1, void *s2, size_t n);

/* IO */
ssize_t SLAMP_read(int fd, void *buf, size_t count);
int     SLAMP_open(const char *pathname, int flags, mode_t mode);
int     SLAMP_close(int fd);
ssize_t SLAMP_write(int fd, const void *buf, size_t count);
off_t   SLAMP_lseek(int fildes, off_t offset, int whence);

FILE *  SLAMP_fopen(const char *path, const char *mode);
FILE *  SLAMP_fopen64(const char *path, const char *mode);
FILE *  SLAMP_freopen(const char *path, const char *mode, FILE* stream);
int     SLAMP_fflush(FILE *stream);
int     SLAMP_fclose(FILE *stream);
int     SLAMP_ferror(FILE *stream);
int     SLAMP_feof(FILE *stream);
long    SLAMP_ftell(FILE *stream);
size_t  SLAMP_fread(void * ptr, size_t size, size_t nitems, FILE *stream);
size_t  SLAMP_fwrite(const void *ptr, size_t size, size_t nitems, FILE *stream);
int     SLAMP_fseek(FILE *stream, long offset, int whence);
void    SLAMP_rewind(FILE *stream);

int     SLAMP_fgetc(FILE *stream);
int     SLAMP_fputc(int c, FILE *stream);
char *  SLAMP_fgets(char *s, int n, FILE *stream);
int     SLAMP_fputs(const char *s, FILE *stream);

int     SLAMP_ungetc(int c, FILE *stream);
int     SLAMP_putchar(int c);
int     SLAMP_getchar(void);

int     SLAMP_fileno(FILE *stream);
char *  SLAMP_gets(char *s);
int     SLAMP_puts(const char *s);

int     SLAMP_select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
int     SLAMP_remove(const char *path);

void    SLAMP_setbuf(FILE * stream, char * buf);
void    SLAMP_setvbuf(FILE * stream, char * buf, int mode, size_t size);
char * SLAMP_tmpnam(char *s);
FILE* SLAMP_tmpfile(void);
char *  SLAMP_ttyname(int fildes);

FILE *  SLAMP_fdopen(int fildes, const char *mode);
void    SLAMP_clearerr(FILE *stream);

int SLAMP_truncate(const char *path, off_t length);
int SLAMP_ftruncate(int fildes, off_t length);

int SLAMP_dup(int oldfd);
int SLAMP_dup2(int oldfd, int newfd);
int SLAMP_pipe(int filedes[2]);

int SLAMP_chmod(const char *path, mode_t mode);
int SLAMP_fchmod(int fildes, mode_t mode);
int SLAMP_fchown(int fd, uid_t owner, gid_t group);
int SLAMP_access(const char *pathname, int mode);
long SLAMP_pathconf(char *path, int name);
int SLAMP_mkdir(const char *pathname, mode_t mode);
int SLAMP_rmdir(const char *pathname);
mode_t SLAMP_umask(mode_t mask);
int SLAMP_fcntl(int fd, int cmd, struct flock *lock);

DIR* SLAMP_opendir(const char* name);
struct dirent* SLAMP_readdir(DIR *dirp);
struct dirent64* SLAMP_readdir64(DIR *dirp);
int SLAMP_closedir(DIR* dirp);

/* Printf */
int SLAMP_printf(const char *format, ...);
int SLAMP_fprintf(FILE *stream, const char *format, ...);
int SLAMP_sprintf(char *str, const char *format, ...);
int SLAMP_snprintf(char *str, size_t size, const char *format, ...);

int SLAMP_vprintf(const char *format, va_list ap);
int SLAMP_vfprintf(FILE *stream, const char *format, va_list ap);
int SLAMP_vsprintf(char *str, const char *format, va_list ap);
int SLAMP_vsnprintf(char *str, size_t size, const char *format, va_list ap);

/* Scanf */
int SLAMP_fscanf(FILE *stream, const char *format, ... );
int SLAMP_scanf(const char *format, ... );
int SLAMP_sscanf(const char *s, const char *format, ... );
int SLAMP___isoc99_sscanf(const char *s, const char *format, ... );

int SLAMP_vfscanf(FILE *stream, const char *format, va_list ap);
int SLAMP_vscanf(const char *format, va_list ap);
int SLAMP_vsscanf(const char *s, const char *format, va_list ap);

/* Time */
time_t SLAMP_time(time_t *t);
struct tm *SLAMP_localtime(const time_t *timer);
struct tm *SLAMP_gmtime(const time_t *timer);
int SLAMP_gettimeofday(struct timeval *tv, struct timezone *tz);

/* Math */
double SLAMP_ldexp(double x, int exp);
float  SLAMP_ldexpf(float x, int exp);
long double SLAMP_ldexpl(long double x, int exp);
double SLAMP_log10(double x);
float  SLAMP_log10f(float x);
long double SLAMP_log10l(long double x);
double SLAMP_log(double x);
float SLAMP_logf(float x);
long double SLAMP_logl(long double x);

double SLAMP_exp(double x);
float SLAMP_expf(float x);
long double SLAMP_expl(long double x);

double SLAMP_cos(double x);
float SLAMP_cosf(float x);
long double SLAMP_cosl(long double x);
double SLAMP_sin(double x);
double SLAMP_tan(double x);
float SLAMP_sinf(float x);
long double SLAMP_sinl(long double x);

double SLAMP_atan(double x);
float SLAMP_atanf(float x);
long double SLAMP_atanl(long double x);

double SLAMP_floor(double x);
float SLAMP_floorf(float x);
long double SLAMP_floorl(long double x);
double SLAMP_ceil(double x);
float SLAMP_ceilf(float x);
long double SLAMP_ceill(long double x);

double SLAMP_atan2(double y, double x);
float SLAMP_atan2f(float y, float x);
long double SLAMP_atan2l(long double y, long double x);

double SLAMP_sqrt(double x);
float SLAMP_sqrtf(float x);
long double SLAMP_sqrtl(long double x);

double SLAMP_pow(double x, double y);
float SLAMP_powf(float x, float y);
long double SLAMP_powl(long double x, long double y);

double SLAMP_fabs(double x);
float SLAMP_fabsf(float x);
long double SLAMP_fabsl(long double x);

double SLAMP_modf(double x, double *iptr);
float SLAMP_modff(float x, float *iptr);
long double SLAMP_modfl(long double x, long double *iptr);

double SLAMP_fmod(double x, double y);

double SLAMP_frexp(double num, int *exp);
float SLAMP_frexpf(float num, int *exp);
long double SLAMP_frexpl(long double num, int *exp);

int SLAMP_isnan();

/* MISC */
char *SLAMP_getenv(const char *name);
int SLAMP_putenv(char* string);
char *SLAMP_getcwd(char *buf, size_t size);
char* SLAMP_strerror(int errnum);
void SLAMP_exit(int status);
void SLAMP__exit(int status);
int  SLAMP_link(const char *oldpath, const char *newpath);
int  SLAMP_unlink(const char *pathname);
int  SLAMP_isatty(int desc);
int SLAMP_setuid(uid_t uid);
uid_t SLAMP_getuid(void);
uid_t SLAMP_geteuid(void);
int SLAMP_setgid(gid_t gid);
gid_t SLAMP_getgid(void);
gid_t SLAMP_getegid(void);
pid_t SLAMP_getpid(void);
int SLAMP_chdir(const char *path);
int SLAMP_execl(const char *path, const char *arg0, ... /*, (char *)0 */);
int SLAMP_execv(const char *path, char *const argv[]);
int SLAMP_execvp(const char *file, char *const argv[]);
int SLAMP_kill(pid_t pid, int sig);
pid_t SLAMP_fork(void);
sighandler_t SLAMP___sysv_signal(int signum, sighandler_t handler);
pid_t SLAMP_waitpid(pid_t pid, int* status, int options);
void SLAMP_qsort(void* base, size_t nmemb, size_t size, int(*compar)(const void *, const void *));
int SLAMP_ioctl(int d, int request, ...);
unsigned int SLAMP_sleep(unsigned int seconds);
char* SLAMP_gcvt(double number, size_t ndigit, char* buf);
char* SLAMP_nl_langinfo(nl_item item);

/* Compiler/Glibc Internals */
void SLAMP___assert_fail(const char * assertion, const char * file, unsigned int line, const char * function);
const unsigned short int **SLAMP___ctype_b_loc(void);
int SLAMP__IO_getc(_IO_FILE * __fp);
int SLAMP__IO_putc(int __c, _IO_FILE *__fp);
int* SLAMP___errno_location (void);

int SLAMP___fxstat (int __ver, int __fildes, struct stat *__stat_buf);
int SLAMP___xstat (int __ver, __const char *__filename, struct stat *__stat_buf);

#ifdef __cplusplus
}
#endif

#endif /* SLAMP_HOOKS_H */
