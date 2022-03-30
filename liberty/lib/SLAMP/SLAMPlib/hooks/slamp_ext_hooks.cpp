#include <bits/stdint-uintn.h>
#include <cassert>
#include <cerrno>
#include <clocale>
#include <cstdint>
#include <tuple>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include "malloc.h"

#include "slamp_timestamp.h"
#include "slamp_logger.h"
#include "slamp_hooks.h"
#include "slamp_shadow_mem.h"
#include "slamp_bound_malloc.h"
#include "slamp_debug.h"

#include "slamp_timer.h"


#include <set>
#include <map>
#include <utility>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

// debugging tools
#if DEBUG
extern uint8_t __slamp_begin_trace = 0;
#endif

extern std::map<void*, size_t>* alloc_in_the_loop;

extern slamp::MemoryMap* smmap;
static const char* percent_c = "%c";
static const char* percent_s = "%s";

/*
 * LLVM mem intrinsics
 *
 * Intrinsics are not removed, these functions are call along the side in addition
 */

void SLAMP_llvm_memcpy_p0i8_p0i8_i32(const uint8_t* dst_addr, const uint8_t* src_addr, const uint32_t len)
{
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(src_addr), 0, static_cast<uint64_t>(len) );
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(dst_addr), 0, static_cast<uint64_t>(len) );
}

void SLAMP_llvm_memcpy_p0i8_p0i8_i64(const uint8_t* dst_addr, const uint8_t* src_addr, const uint64_t len)
{
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(src_addr), 0, len);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(dst_addr), 0, len);
}

void SLAMP_llvm_memmove_p0i8_p0i8_i32(const uint8_t* dst_addr, const uint8_t* src_addr, const uint32_t len)
{
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(src_addr), 0, static_cast<uint64_t>(len) );
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(dst_addr), 0, static_cast<uint64_t>(len) );
}

void SLAMP_llvm_memmove_p0i8_p0i8_i64(const uint8_t* dst_addr, const uint8_t* src_addr, const uint64_t len)
{
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(src_addr), 0, len);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(dst_addr), 0, len);
}

void SLAMP_llvm_memset_p0i8_i32(const uint8_t* dst_addr, const uint32_t len)
{
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(dst_addr), 0, static_cast<uint64_t>(len) );
}

void SLAMP_llvm_memset_p0i8_i64(const uint8_t* dst_addr, const uint64_t len)
{
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(dst_addr), 0, len);
}


void SLAMP_llvm_lifetime_start_p0i8(uint64_t size, uint8_t* ptr)
{
  // do nothing for now

  /*
   * void* shadow = smmap->allocate((void*)ptr, size);
   * if (shadow)
   * {
   *   if (context)
   *   {
   *     (*alloc_in_the_loop)[(void*)ptr] = size;
   *   }
   * }
   * else
   * {
   *   fprintf(stderr, "SLAMP_llvm_lifetime_start_p0i8, error in shadow malloc\n");
   *   assert(false);
   * }
   */
}


void SLAMP_llvm_lifetime_end_p0i8(uint64_t size, uint8_t* ptr)
{
  // do nothing for now
}


/*
 * External library wrappers
 */


void* SLAMP_calloc(size_t nelem, size_t elsize)
{
  void* ptr = SLAMP_malloc(nelem*elsize);
  memset(ptr, '\0', nelem*elsize);
  return ptr;
}

void* SLAMP_realloc(void* ptr, size_t size)
{
  if (ptr == NULL)
  {
    /*
       In case that ptr is a null pointer, the function behaves like malloc, assigning
       a new block of size bytes and returning a pointer to its beginning.
     */
    return SLAMP_malloc(size);
  }

  if (size == 0)
  {
    /*
       If size is zero, the return value depends on the particular library
       implementation (it may or may not be a null pointer), but the returned
       pointer shall not be used to dereference an object in any case.
     */
    SLAMP_free(ptr);
    return NULL;
  }

  void* result = SLAMP_malloc(size);
  if (result == NULL)
  {
    /*
       If the function failed to allocate the requested block of memory, a null
       pointer is returned, and the memory block pointed to by argument ptr is not
       deallocated (it is still valid, and with its contents unchanged).
     */
    return NULL;
  }

  // copy data from old to new
  size_t orig_size = slamp::get_object_size(ptr);
  size_t copy_size = (orig_size < size) ? orig_size : size;
  SLAMP_memcpy(result, ptr, copy_size);
  //memcpy(result, ptr, copy_size);
  smmap->copy(result, ptr, copy_size);

  // free original allocation
  SLAMP_free(ptr);

  return result;
}

// TODO: describe the difference (the constructor and the exception handling)
void* SLAMP__Znam(size_t size) {
  return SLAMP_malloc(size);
}

void* SLAMP__Znwm(size_t size) {
  return SLAMP_malloc(size);
}

char* SLAMP_strdup(const char *s1)
{
  size_t slen = strlen(s1) + 1;
  char* result = (char*)SLAMP_malloc(slen);
  if (!result)
    return NULL;
  strncpy(result, s1, slen);
  return result;
}

char* SLAMP___strdup(const char *s1)
{
  return SLAMP_strdup(s1);
}

void  SLAMP_cfree(void* ptr)
{
  SLAMP_free(ptr);
}

// TODO: describe the difference (the constructor and the exception handling)
void SLAMP__ZdlPv(void *ptr) {
  SLAMP_free(ptr);
}

void SLAMP__ZdaPv(void *ptr) {
  SLAMP_free(ptr);
}

int   SLAMP_brk(void *end_data_segment)
{
  return brk(end_data_segment);
}

void* SLAMP_sbrk(intptr_t increment)
{
  return sbrk(increment);
}

/* The call to exit is to make the compiler not complain about failure to return a value */
#define UNIMPLEMENTED(name) \
    fprintf(stderr, "\"%s\" not implemented!\n", name); \
    abort(); \
    exit(-1);

/* String functions */

static void load_string(const char *ptr)
{
  if (ptr == NULL)
    return;
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(ptr), 0, strlen(ptr)+1);
}

size_t SLAMP_strlen(const char *str)
{
  size_t result = strlen(str);

  // TODO: Guarantee str has shadow at this point
  //
  // FIXME: bypass strlen
  // smmap->allocate((void*)str, result);

  // load_string(str);
  return result;
}

char* SLAMP_strchr(char *s, int c)
{
  char* result = strchr(s, c);
  load_string(s);
  return result;
}

char* SLAMP_strrchr(char *s, int c)
{
  char* result = strrchr(s, c);

  // TODO: Guarantee str has shadow at this point
  smmap->allocate((void*)s, strlen(s));

  load_string(s);
  return result;
}

int SLAMP_strcmp(const char *s1, const char *s2)
{
  int result = strcmp(s1, s2);
  load_string(s1);
  load_string(s2);
  return result;
}

int SLAMP_strncmp(const char *s1, const char *s2, size_t n)
{
  size_t s1_len = strlen(s1);
  size_t s2_len = strlen(s2);
  int result = strncmp(s1, s2, n);

  if (s1_len < n || s2_len < n)
  {
    //printf("WARNING: strncmp, n is larger than the length of the string s1 or s2\n");
    load_string(s1);
    load_string(s2);
  }
  else
  {
    SLAMP_loadn_ext(reinterpret_cast<uint64_t>(s1), 0, n);
    SLAMP_loadn_ext(reinterpret_cast<uint64_t>(s2), 0, n);
  }

  return result;
}

char* SLAMP_strcpy(char *dest, const char *src)
{
  char* result = strcpy(dest, src);
  size_t n = strlen(src);
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(src), 0, n + 1);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(dest), 0, n + 1);
  return result;
}

char* SLAMP_strncpy(char *dest, const char *src, size_t n)
{
  char* result = strncpy(dest, src, n);
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(src), 0, n);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(dest), 0, n);
  return result;
}

char* SLAMP_strcat(char *s1, const char *s2)
{
  char* result;
  size_t s1_len = strlen(s1);
  size_t s2_len = strlen(s2);
  load_string(s1);
  load_string(s2);
  result = strcat(s1, s2);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(s1) + s1_len, 0, s2_len + 1);
  return result;
}

char* SLAMP_strncat(char *s1, const char *s2, size_t n)
{
  char* result;
  load_string(s1);
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(s2), 0, n);
  result = strncat(s1, s2, n);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(s1) + strlen(s1), 0, n + 1);
  return result;
}

char* SLAMP_strstr(char *s1, char *s2)
{
  load_string(s1);
  load_string(s2);
  return strstr(s1, s2);
}

size_t SLAMP_strspn(const char *s1, const char *s2)
{
  load_string(s1);
  load_string(s2);
  return strspn(s1, s2);
}

size_t SLAMP_strcspn(const char *s1, const char *s2)
{
  load_string(s1);
  load_string(s2);
  return strcspn(s1, s2);
}

char* SLAMP_strtok(char *s, const char *delim)
{
  char *result;
  //fprintf(stderr, "String s in the beginning of SLAMP_strtok is now\"%s\"\n", s);
  load_string(delim);
  result = strtok(s, delim);
  if (result == NULL) {
    //fprintf(stderr, "Result of SLAMP_strtok is null\n");
    return NULL;
  }
  //fprintf(stderr, "Result of SLAMP_strtok is \"%s\"\n", result);
  load_string(result);

  //fprintf(stderr, "About to call SLAMP_storen_ext in SLAMP_strtok\n");
  //fprintf(stderr, "String s is now\"%s\"\n", s);
  // assign additional loop carried dependence to itself
  if (s)
    SLAMP_storen_ext(reinterpret_cast<uint64_t>(s), 0, strlen(s));

  return result;
}

double SLAMP_strtod(const char *nptr, char **endptr)
{
  load_string(nptr);
  // TODO: is it a correct implementation?
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(endptr), 0, sizeof(char*));
  return strtod(nptr, endptr);
}

long int SLAMP_strtol(const char *nptr, char **endptr, int base)
{
  load_string(nptr);
  // TODO: is it a correct implementation?
  if (endptr)
    SLAMP_storen_ext(reinterpret_cast<uint64_t>(endptr), 0, sizeof(char*));
  return strtol(nptr, endptr, base);
}

char* SLAMP_strpbrk(char *s1, char *s2)
{
  char* result;
  load_string(s1);
  load_string(s2);
  result = strpbrk(s1, s2);
  return result;
}

/* Mem* and b* functions */

void* SLAMP_memset(void* dest, int c, size_t n)
{
  void *result = memset(dest, c, n);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(dest), 0, n);
  return result;
}

void* SLAMP_memcpy(void* dest, const void* src, size_t n)
{
  void *result = memcpy(dest, src, n);
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(src), 0, n);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(dest), 0, n);
  return result;
}

void* SLAMP___builtin_memcpy (void* dest, const void* src, size_t n)
{
  return SLAMP_memcpy(dest, src, n);
}

void* SLAMP_memmove(void* dest, const void* src, size_t n)
{
  void* result = memmove(dest, src, n);
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(src), 0, n);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(dest), 0, n);
  return result;
}

int SLAMP_memcmp(const void *s1, const void *s2, size_t n)
{
  int result = memcmp(s1, s2, n);
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(s1), 0, n);
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(s2), 0, n);
  return result;
}

void* SLAMP_memchr(void* ptr, int value, size_t num)
{
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(ptr), 0, num);
  return memchr(ptr, value, num);
}

void* SLAMP___rawmemchr(void* ptr, int value)
{
  load_string(reinterpret_cast<const char*>(ptr));
  return rawmemchr(ptr, value);
}

void  SLAMP_bzero(void *s, size_t n)
{
  bzero(s, n);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(s), 0, n);
}

void  SLAMP_bcopy(const void *s1, void *s2, size_t n)
{
  bcopy(s1, s2, n);
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(s1), 0, n);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(s2), 0, n);
}

/* IO */

/* TODO: dependency across file descriptors? */

ssize_t SLAMP_read(int fd, void *buf, size_t count)
{
  ssize_t result;
  assert( count <= SSIZE_MAX );
  result = read(fd, buf, count);
  if (result == 0 || result == -1)
    return result;
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(buf), 0, result);
  return result;
}

int SLAMP_open(const char *pathname, int flags, mode_t mode)
{
  int result;
  load_string(pathname);
  result = open(pathname, flags, mode);
  return result;
}

int SLAMP_close(int fd)
{
  return close(fd);
}

ssize_t SLAMP_write(int fd, const void *buf, size_t count)
{
  ssize_t result;
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(buf), 0, count);
  result = write(fd, buf, count);
  return result;
}

off_t SLAMP_lseek(int fildes, off_t offset, int whence)
{
  return lseek(fildes, offset, whence);
}

FILE* SLAMP_fopen(const char *path, const char *mode)
{
  load_string(path);
  load_string(mode);
  FILE* fp = fopen(path, mode);
  smmap->allocate((void*)fp, sizeof(FILE));
  return fp;
}

FILE* SLAMP_fopen64(const char *path, const char *mode)
{
  load_string(path);
  load_string(mode);
  FILE* fp = fopen64(path, mode);
  smmap->allocate((void*)fp, sizeof(FILE));

  // how about this nasty hack?
  if (fp) {
    char* ptr = (char*)fp;
    char* base = *( (char**)(ptr+8) );
    char* end = *( (char**)(ptr+16) );
    if (end-base) {
      smmap->allocate((void*)base, end-base);
    }
  }
  return fp;
}

FILE* SLAMP_freopen(const char *path, const char *mode, FILE* stream)
{
  load_string(path);
  load_string(mode);
  FILE* fp = freopen(path, mode, stream);
  smmap->allocate((void*)fp, sizeof(FILE));
  return fp;
}

int SLAMP_fflush(FILE *stream)
{
  return fflush(stream);
}

int SLAMP_fclose(FILE *stream)
{
  return fclose(stream);
}

int SLAMP_ferror(FILE *stream)
{
  return ferror(stream);
}

int SLAMP_feof(FILE *stream)
{
  return feof(stream);
}

long SLAMP_ftell(FILE *stream)
{
  return ftell(stream);
}

size_t SLAMP_fread(void* ptr, size_t size, size_t nitems, FILE *stream)
{
  size_t result = fread(ptr, size, nitems, stream);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(ptr), 0, result * size);

  // how about this nasty hack?
  if (stream) {
    char* p = (char*)stream;
    char* base = *( (char**)(p+8) );
    char* end = *( (char**)(p+16) );
    if (end-base) {
      smmap->allocate((void*)base, end-base);
    }
  }

  return result;
}

size_t SLAMP_fwrite(const void *ptr, size_t size, size_t nitems, FILE *stream)
{
  size_t result = fwrite(ptr, size, nitems, stream);
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(ptr), 0, size*result);
  return result;
}

int SLAMP_fseek(FILE *stream, long offset, int whence)
{
  return fseek(stream, offset, whence);
}

void SLAMP_rewind(FILE *stream)
{
  return rewind(stream);
}

int SLAMP_fgetc(FILE *stream)
{
  return fgetc(stream);
}

int SLAMP_fputc(int c, FILE *stream)
{
  //int result = SLAMP_fprintf(stream, percent_c, c);
  int result = fprintf(stream, percent_c, c);
  if (result == 0) {
    return EOF;
  }
  return (unsigned char) c;
}

char* SLAMP_fgets(char *s, int n, FILE *stream)
{
  char *result = fgets(s, n, stream);
  if (result == NULL)
    return result;

  /* MJB: I believe that if NULL is returned, nothing it changed */
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(result), 0, strlen(result) + 1);
  return result;
}

int SLAMP_fputs(const char *s, FILE *stream)
{
  return SLAMP_fprintf(stream, percent_s, s);
}

int SLAMP_ungetc(int c, FILE *stream)
{
  return ungetc(c, stream);
}

int SLAMP_putchar(int c)
{
  return putchar(c);
}

int SLAMP_getchar(void)
{
  return getchar();
}

int SLAMP_fileno(FILE *stream)
{
  return fileno(stream);
}

char* SLAMP_gets(char *s)
{
  fprintf(stderr, "The most recent revision of the C standard (2011) has definitively removed gets from its specification.\n");
  abort();
  exit(-1);
}

int SLAMP_puts(const char *s)
{
  load_string(s);
  return puts(s);
}

int SLAMP_select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
  UNIMPLEMENTED("select");
}

int SLAMP_remove(const char *path)
{
  load_string(path);
  return remove(path);
}

void SLAMP_setbuf(FILE * stream, char * buf)
{
  setbuf(stream, buf);
}

void SLAMP_setvbuf(FILE * stream, char * buf, int mode, size_t size)
{
  setvbuf(stream, buf, mode, size);
}

char* SLAMP_tmpnam(char *s)
{
  char* result = tmpnam(s);
  if (result)
    SLAMP_storen_ext(reinterpret_cast<uint64_t>(result), 0, strlen(result) + 1);
  return result;
}

FILE* SLAMP_tmpfile(void)
{
  return tmpfile();
}

char* SLAMP_ttyname(int fildes)
{
  char* result = ttyname(fildes);
  if (result)
    SLAMP_storen_ext(reinterpret_cast<uint64_t>(result), 0, strlen(result) + 1);
  return result;
}

FILE* SLAMP_fdopen(int fildes, const char *mode)
{
  load_string(mode);
  return fdopen(fildes, mode);
}

void SLAMP_clearerr(FILE *stream)
{
  clearerr(stream);
}

int SLAMP_truncate(const char *path, off_t length)
{
  UNIMPLEMENTED("truncate");
}

int SLAMP_ftruncate(int fildes, off_t length)
{
  UNIMPLEMENTED("ftruncate");
}

int SLAMP_dup(int oldfd)
{
  UNIMPLEMENTED("dup");
}

int SLAMP_dup2(int oldfd, int newfd)
{
  UNIMPLEMENTED("dup2");
}

int SLAMP_pipe(int filedes[2])
{
  UNIMPLEMENTED("pipe");
}

int SLAMP_chmod(const char *path, mode_t mode)
{
  load_string(path);
  return chmod(path, mode);
}

int SLAMP_fchmod(int fildes, mode_t mode)
{
  return fchmod(fildes, mode);
}

int SLAMP_fchown(int fd, uid_t owner, gid_t group)
{
  return fchown(fd, owner, group);
}

int SLAMP_access(const char *pathname, int mode)
{
  load_string(pathname);
  return access(pathname, mode);
}

long SLAMP_pathconf(char *path, int name)
{
  load_string(path);
  return pathconf(path, name);
}

int SLAMP_mkdir(const char *pathname, mode_t mode)
{
  load_string(pathname);
  return mkdir(pathname, mode);
}

int SLAMP_rmdir(const char *pathname)
{
  load_string(pathname);
  return rmdir(pathname);
}

mode_t SLAMP_umask(mode_t mask)
{
  return umask(mask);
}

int SLAMP_fcntl(int fd, int cmd, struct flock *lock)
{
  //UNIMPLEMENTED("fcntl");
  return fcntl(fd, cmd, lock);
}

DIR* SLAMP_opendir(const char* name)
{
  load_string(name);
  return opendir(name);
}

struct dirent* SLAMP_readdir(DIR *dirp)
{
  dirent* ret = readdir(dirp);
  smmap->allocate((void*)ret, sizeof(dirent));
  if (ret)
    SLAMP_storen_ext(reinterpret_cast<uint64_t>(ret), 0, sizeof(dirent));
  return ret;
}

struct dirent64* SLAMP_readdir64(DIR *dirp)
{
  dirent64* ret = readdir64(dirp);
  smmap->allocate((void*)ret, sizeof(dirent64));
  if (ret)
    SLAMP_storen_ext(reinterpret_cast<uint64_t>(ret), 0, sizeof(dirent64));
  return ret;
}

int SLAMP_closedir(DIR* dirp)
{
  return closedir(dirp);
}

/* Printf family of functions */
#define IS_STRING(byte)   ((byte == 's'))
#define IS_DOUBLE(byte)   ((byte == 'f') || (byte == 'F') || (byte == 'e') || (byte == 'E') || (byte == 'g') || (byte == 'G'))
#define IS_INT(byte)      ((byte == 'd') || (byte == 'i') || (byte == 'X') || (byte == 'x') || (byte == 'o') || (byte == 'u') || (byte == 'c'))
#define IS_LONG_INT(byte) ((byte == 'D') || (byte == 'O') || (byte == 'U'))
#define IS_VOID_PTR(byte) ((byte == 'p'))
#define IS_LEN(byte)      ((byte == 'n'))

static uint8_t is_format_char(char byte) {
    return (IS_STRING(byte) || IS_DOUBLE(byte) || IS_INT(byte) || IS_LONG_INT(byte) || IS_VOID_PTR(byte) || IS_LEN(byte));
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
  va_list vp_save;

#ifdef __va_copy
  /* This macro will be available soon in gcc's <stdarg.h>.  We need it
     since on some systems `va_list' is not an integral type.  */
  __va_copy (vp_save, vp);
#else
  vp_save = vp;
#endif

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
      char *str_arg = va_arg(vp_save, char *);
      load_string(str_arg);
    } else if (IS_INT(byte)) {
      if (is_long(format)) {
        long int_arg __attribute__ ((unused));
        int_arg = va_arg(vp_save, long);
      } else {
        int int_arg __attribute__ ((unused));
        int_arg = va_arg(vp_save, int);
      }
    } else if (IS_DOUBLE(byte)) {
      double real_arg  __attribute__ ((unused));
      real_arg = va_arg(vp_save, double);
    } else if (IS_LONG_INT(byte)) {
      long int long_int_arg  __attribute__ ((unused));
      long_int_arg = va_arg(vp_save, long int);
    } else if (IS_VOID_PTR(byte)) {
      void *void_ptr_arg  __attribute__ ((unused));
      void_ptr_arg = va_arg(vp_save, void *);
    } else if (IS_LEN(byte)){
        // fprintf(stderr, "IS LEN%%n\n");
        // %n return the len of string
        // DON'T NEED TO DO ANYTHING
    }
    else {
      fprintf(stderr, "Unknown type: %c$ \n", byte);
      abort();
    }
  }

#ifdef __va_copy
  va_end(vp_save);
#endif

  return;

ERROR:
  {
    fprintf(stderr, "printf_args: Error in format string encountered\n");
    abort();
  }
}

int SLAMP_printf(const char *format, ...)
{
  va_list ap;
  int size;
  va_start(ap, format);
  size = SLAMP_vprintf(format, ap);
  va_end(ap);
  return size;
}

int SLAMP_fprintf(FILE *stream, const char *format, ...)
{
  va_list ap;
  int size;
  va_start(ap, format);
  size = SLAMP_vfprintf(stream, format, ap);
  va_end(ap);
  return size;
}

int SLAMP_sprintf(char *str, const char *format, ...)
{
  va_list ap;
  int size;
  va_start(ap, format);
  size = SLAMP_vsprintf(str, format, ap);
  va_end(ap);
  return size;
}

int SLAMP_snprintf(char *str, size_t size, const char *format, ...)
{
  va_list ap;
  int result;
  va_start(ap, format);
  result = SLAMP_vsnprintf(str, size, format, ap);
  va_end(ap);
  return result;
}

int SLAMP_vprintf(const char *format, va_list ap)
{
  int size;
  size = SLAMP_vfprintf(stdout, format, ap);
  return size;
}

int SLAMP_vfprintf(FILE *stream, const char *format, va_list ap)
{
  int size;
  load_string(format);
  touch_printf_args(format, ap);
  size = vfprintf(stream, format, ap);
  return size;
}

int SLAMP_vsprintf(char *str, const char *format, va_list ap)
{
  int size;
  load_string(format);
  touch_printf_args(format, ap);
  size = vsprintf(str, format, ap);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(str), 0, size);
  return size;
}

int SLAMP_vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
  int result;
  load_string(format);
  touch_printf_args(format, ap);
  result = vsnprintf(str, size, format, ap);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(str), 0, result);
  return result;
}

/* Scanf family of functions */

/* MJB: We need to touch vp, if that is legal */
static void touch_scanf_args(const char *format, va_list vp) {
  char byte;
  va_list vp_save;
  uint8_t ignore_store;

#ifdef __va_copy
  /* This macro will be available soon in gcc's <stdarg.h>.  We need it
     since on some systems `va_list' is not an integral type.  */
  __va_copy (vp_save, vp);
#else
  vp_save = vp;
#endif

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
      char *str_arg = va_arg(vp_save, char *);
      load_string(str_arg);
    } else if (IS_INT(byte)) {
      if (is_halfhalf(format)) {
        char *arg = va_arg(vp_save, char *);
        SLAMP_storen_ext(reinterpret_cast<uint64_t>(arg), 0, sizeof(*arg));
      } else if (is_half(format)) {
        short *arg = va_arg(vp_save, short *);
        SLAMP_storen_ext(reinterpret_cast<uint64_t>(arg), 0, sizeof(*arg));
      } else if (is_long(format)) {
        long *arg = va_arg(vp_save, long *);
        SLAMP_storen_ext(reinterpret_cast<uint64_t>(arg), 0, sizeof(*arg));
      } else {
        int *arg = va_arg(vp_save, int *);
        SLAMP_storen_ext(reinterpret_cast<uint64_t>(arg), 0, sizeof(*arg));
      }
    } else if (IS_DOUBLE(byte)) {
      double *arg = va_arg(vp_save, double *);
      SLAMP_storen_ext(reinterpret_cast<uint64_t>(arg), 0, sizeof(*arg));
    } else if (IS_LONG_INT(byte)) {
      long int *arg = va_arg(vp_save, long int *);
      SLAMP_storen_ext(reinterpret_cast<uint64_t>(arg), 0, sizeof(*arg));
    } else if (IS_VOID_PTR(byte)) {
      void **arg = va_arg(vp_save, void **);
      SLAMP_storen_ext(reinterpret_cast<uint64_t>(arg), 0, sizeof(*arg));
    } else if (IS_LEN(byte)){
      //fprintf(stderr, "IS LEN %%n\n");   
    }else {
      fprintf(stderr, "Unknown type\n");
      abort();
    }
  }

#ifdef __va_copy
  va_end(vp_save);
#endif

  return;

ERROR:
  {
    fprintf(stderr, "touch_scanf_args: Error in format string encountered\n");
    abort();
  }
}

int SLAMP_fscanf(FILE *stream, const char *format, ... )
{
  va_list ap;
  int result;
  va_start(ap, format);
  result = SLAMP_vfscanf(stream, format, ap);
  va_end(ap);
  return result;
}

int SLAMP_scanf(const char *format, ... )
{
  va_list ap;
  int result;
  va_start(ap, format);
  result = SLAMP_vscanf(format, ap);
  va_end(ap);
  return result;
}

int SLAMP_sscanf(const char *s, const char *format, ... )
{
  va_list ap;
  int result;
  va_start(ap, format);
  result = SLAMP_vsscanf(s, format, ap);
  va_end(ap);
  return result;
}

int SLAMP___isoc99_sscanf(const char *s, const char *format, ... )
{
  va_list ap;
  int result;
  va_start(ap, format);
  result = SLAMP_vsscanf(s, format, ap);
  va_end(ap);
  return result;
}

int SLAMP_vfscanf(FILE *stream, const char *format, va_list ap)
{
  int result;
  load_string(format);
  result = vfscanf(stream, format, ap);
  touch_scanf_args(format, ap);
  return result;
}

int SLAMP_vscanf(const char *format, va_list ap)
{
  int result;
  result = SLAMP_vfscanf(stdin, format, ap);
  return result;
}

int SLAMP_vsscanf(const char *s, const char *format, va_list ap)
{
  int result;
  load_string(format);
  load_string(s);
  result = vsscanf(s, format, ap);
  touch_scanf_args(format, ap);
  return result;
}

/* Time */
time_t SLAMP_time(time_t *t)
{
  time_t result = time(t);
  if (t != NULL)
    SLAMP_storen_ext(reinterpret_cast<uint64_t>(t), 0, sizeof(time_t));
  return result;
}

struct tm *SLAMP_localtime(const time_t *timer)
{
  struct tm *result;
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(timer), 0, sizeof(*timer));
  result = localtime(timer);

  if ( !smmap->is_allocated(reinterpret_cast<void*>(result)) )
    smmap->allocate(result, sizeof(*result));

  SLAMP_storen_ext(reinterpret_cast<uint64_t>(result), 0, sizeof(*result));
  return result;
}

struct lconv* SLAMP_localeconv() {
  struct lconv* lc = localeconv();

  if ( !smmap->is_allocated(reinterpret_cast<void*>(lc)) )
    smmap->allocate(lc, sizeof(*lc));

  if ( !smmap->is_allocated(reinterpret_cast<void*>(lc->decimal_point)) )
    smmap->allocate(lc->decimal_point, sizeof(*(lc->decimal_point)));

  SLAMP_storen_ext(reinterpret_cast<uint64_t>(lc), 0, sizeof(*lc));
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(lc->decimal_point), 0, sizeof(*(lc->decimal_point)));
  return lc;
}

struct tm *SLAMP_gmtime(const time_t *timer)
{
  struct tm *result;
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(timer), 0, sizeof(*timer));
  result = gmtime(timer);

  if ( !smmap->is_allocated(reinterpret_cast<void*>(result)) )
    smmap->allocate(result, sizeof(*result));

  SLAMP_storen_ext(reinterpret_cast<uint64_t>(result), 0, sizeof(*result));
  return result;
}

int SLAMP_gettimeofday(struct timeval *tv, struct timezone *tz)
{
  int result = gettimeofday(tv, tz);
  if (tv)
    SLAMP_storen_ext(reinterpret_cast<uint64_t>(tv), 0, sizeof(*tv));
  if (tz)
    SLAMP_storen_ext(reinterpret_cast<uint64_t>(tz), 0, sizeof(struct timezone));
  return result;
}

/* Math Functions */

double SLAMP_ldexp(double x, int exp)
{
  return ldexp(x, exp);
}

float  SLAMP_ldexpf(float x, int exp)
{
  return ldexpf(x, exp);
}

long double SLAMP_ldexpl(long double x, int exp)
{
  return ldexpl(x, exp);
}

double SLAMP_exp(double x)
{
  return exp(x);
}

float SLAMP_expf(float x)
{
  return expf(x);
}

long double SLAMP_expl(long double x)
{
  return expl(x);
}

double SLAMP_log10(double x)
{
  return log10(x);
}

float  SLAMP_log10f(float x)
{
  return log10f(x);
}

long double SLAMP_log10l(long double x)
{
  return log10l(x);
}

double SLAMP_log(double x)
{
  return log(x);
}

float SLAMP_logf(float x)
{
  return logf(x);
}

long double SLAMP_logl(long double x)
{
  return logl(x);
}

double SLAMP_pow(double x, double y)
{
  return pow(x,y);
}

float SLAMP_powf(float x, float y)
{
  return  powf(x, y);
}

long double SLAMP_powl(long double x, long double y)
{
  return powl(x,y);
}

double SLAMP_cos(double x)
{
  return cos(x);
}

float SLAMP_cosf(float x)
{
  return cosf(x);
}

long double SLAMP_cosl(long double x)
{
  return cosl(x);
}

double SLAMP_sin(double x)
{
  return sin(x);
}

double SLAMP_tan(double x)
{
  return tan(x);
}
float SLAMP_sinf(float x)
{
  return sinf(x);
}

long double SLAMP_sinl(long double x)
{
  return sinl(x);
}

double SLAMP_atan(double x)
{
  return atan(x);
}

float SLAMP_atanf(float x)
{
  return atanf(x);
}

long double SLAMP_atanl(long double x)
{
  return atanl(x);
}

double SLAMP_atan2(double y, double x)
{
  return atan2(y,x);
}

float SLAMP_atan2f(float y, float x)
{
  return atan2f(y,x);
}

long double SLAMP_atan2l(long double y, long double x)
{
  return atan2l(y,x);
}

double SLAMP_modf(double x, double *iptr)
{
  double result;
  result = modf(x, iptr);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(iptr), 0, sizeof(*iptr));
  return result;
}

float SLAMP_modff(float x, float *iptr)
{
  float result;
  result = modff(x, iptr);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(iptr), 0, sizeof(*iptr));
  return result;
}

long double SLAMP_modfl(long double x, long double *iptr)
{
  long double result;
  result = modfl(x, iptr);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(iptr), 0, sizeof(*iptr));
  return result;
}

double SLAMP_fmod(double x, double y)
{
  return fmod(x, y);
}

double SLAMP_frexp(double num, int *exp)
{
  double result;
  result = frexp(num, exp);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(exp), 0, sizeof(*exp));
  return result;
}

float SLAMP_frexpf(float num, int *exp)
{
  float result;
  result = frexpf(num, exp);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(exp), 0, sizeof(*exp));
  return result;
}

long double SLAMP_frexpl(long double num, int *exp)
{
  long double result;
  result = frexpl(num, exp);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(exp), 0, sizeof(*exp));
  return result;
}

int SLAMP_isnan()
{
  UNIMPLEMENTED("isnan");
}

double SLAMP_floor(double x)
{
  return floor(x);
}

float SLAMP_floorf(float x)
{
  return floorf(x);
}

long double SLAMP_floorl(long double x)
{
  return floorl(x);
}

double SLAMP_ceil(double x)
{
  return ceil(x);
}

float SLAMP_ceilf(float x)
{
  return ceilf(x);
}

long double SLAMP_ceill(long double x)
{
  return ceill(x);
}

double SLAMP_sqrt(double x)
{
  return sqrt(x);
}

float SLAMP_sqrtf(float x)
{
  return sqrtf(x);
}

long double SLAMP_sqrtl(long double x)
{
  return sqrtl(x);
}

double SLAMP_fabs(double x)
{
  return fabs(x);
}

float SLAMP_fabsf(float x)
{
  return fabsf(x);
}

long double SLAMP_fabsl(long double x)
{
  return fabsl(x);
}

/* MISC */
char* SLAMP_getenv(const char* name)
{
  char *result;
  load_string(name);
  result = getenv(name);
  if (result != NULL) {
    if ( !smmap->is_allocated(reinterpret_cast<void*>(result)) )
      smmap->allocate(result, strlen(result)+1);
    SLAMP_storen_ext(reinterpret_cast<uint64_t>(result), 0, strlen(result) + 1);
  }
  return result;
}

int SLAMP_putenv(char* string)
{
  load_string(string);
  return putenv(string);
}

char *SLAMP_getcwd(char *buf, size_t size)
{
  char *result = getcwd(buf, size);
  if (result != NULL) {
    SLAMP_storen_ext(reinterpret_cast<uint64_t>(buf), 0, size);
  }
  return result;
}

char* SLAMP_strerror(int errnum)
{
  return strerror(errnum);
}

void SLAMP_exit(int status)
{
  exit(status);
}

void SLAMP__exit(int status)
{
  _exit(status);
}

int SLAMP_link(const char *oldpath, const char *newpath)
{
  int result;
  load_string(oldpath);
  load_string(newpath);
  result = link(oldpath, newpath);
  return result;
}

int SLAMP_unlink(const char *pathname)
{
  int result;
  load_string(pathname);
  result = unlink(pathname);
  return result;
}

int SLAMP_isatty(int desc)
{
  return isatty(desc);
}

int SLAMP_setuid(uid_t uid)
{
  return setuid(uid);
}

uid_t SLAMP_getuid(void)
{
  return getuid();
}

uid_t SLAMP_geteuid(void)
{
  return geteuid();
}

int SLAMP_setgid(gid_t gid)
{
  return setgid(gid);
}

gid_t SLAMP_getgid(void)
{
  return getgid();
}

gid_t SLAMP_getegid(void)
{
  return getegid();
}

pid_t SLAMP_getpid(void)
{
  return getpid();
}

int SLAMP_chdir(const char *path)
{
  int result;
  load_string(path);
  result = chdir(path);
  return result;
}

int SLAMP_execl(const char* path, const char* arg0, ...)
{
  UNIMPLEMENTED("execl");
}

int SLAMP_execv(const char* path, char* const argv[])
{
  UNIMPLEMENTED("execvp");
}

int SLAMP_execvp(const char* file, char* const argv[])
{
  UNIMPLEMENTED("execvp");
}

int SLAMP_kill(pid_t pid, int sig)
{
  UNIMPLEMENTED("kill");
}

pid_t SLAMP_fork(void)
{
  UNIMPLEMENTED("fork");
}

sighandler_t SLAMP___sysv_signal(int signum, sighandler_t handler)
{
  UNIMPLEMENTED("__sysv_signal");
}

pid_t SLAMP_waitpid(pid_t pid, int* status, int options)
{
  if (status)
    SLAMP_loadn_ext(reinterpret_cast<uint64_t>(status), 0, sizeof(int));
  return waitpid(pid, status, options);
}

void SLAMP_qsort(void* base, size_t nmemb, size_t size, int(*compare)(const void *, const void *))
{
  SLAMP_loadn_ext(reinterpret_cast<uint64_t>(base), 0, nmemb * size);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(base), 0, nmemb * size);
  qsort(base, nmemb, size, compare);
}

int SLAMP_ioctl(int d, int request, ...)
{
  UNIMPLEMENTED("ioctl");
}

unsigned int SLAMP_sleep(unsigned int seconds)
{
  return sleep(seconds);
}

char* SLAMP_gcvt(double number, size_t ndigit, char* buf)
{
  char* ret = gcvt(number, ndigit, buf);
  size_t len = strlen(buf);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(buf), 0, len+1);
  return ret;
}

char* SLAMP_nl_langinfo(nl_item item)
{
  char* ret = nl_langinfo(item);
  smmap->allocate(ret, strlen(ret));
  return ret;
}

/* Compiler/Glibc Internals */

void SLAMP___assert_fail(const char * assertion, const char * file, unsigned int line, const char * function)
{
#ifndef NDEBUG
  __assert_fail(assertion, file, line, function);
#endif
}

const unsigned short int **SLAMP___ctype_b_loc(void)
{
  auto ret = __ctype_b_loc();
  // *ret it's an array of 384 short, each represent the trait of a char
  // https://stackoverflow.com/questions/37702434/ctype-b-loc-what-is-its-purpose
  smmap->allocate((void*)*ret, 384 * sizeof(**ret));
  return ret;
}

/*
 *int SLAMP__IO_getc(_IO_FILE* __fp)
 *{
 *  return _IO_getc(__fp);
 *}
 *
 *int SLAMP__IO_putc(int __c, _IO_FILE* __fp)
 *{
 *  return _IO_putc(__c, __fp);
 *}
 */

/*
 * Compiler inserts the implementation of this function,
 * because __errno_location cannot be called explicitly.
 */
#if 0
int * SLAMP___errno_location (void) {
  return __errno_location();
}
#endif

int SLAMP___fxstat (int __ver, int __fildes, struct stat *__stat_buf)
{
  int result = __fxstat(__ver, __fildes, __stat_buf);
  SLAMP_storen_ext(reinterpret_cast<uint64_t>(__stat_buf), 0, sizeof(*__stat_buf));
  return result;
}

int SLAMP___xstat (int __ver, __const char *__filename, struct stat *__stat_buf)
{
  int result = __xstat(__ver, __filename, __stat_buf);
  return result;
}
