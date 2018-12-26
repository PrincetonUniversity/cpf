#include "sys/stat.h"

#include "commfd.h"

int commutative(int fd1, int fd2)
{
  struct stat m1,m2;

  fstat(fd1,&m1);
  if( !S_ISREG(m1.st_mode) )
    return 0;

  fstat(fd2,&m2);
  if( !S_ISREG(m2.st_mode) )
    return 0;

  // both are regular files.
  // are they /different/ regular files?

  if( m1.st_ino == m2.st_ino
  &&  m1.st_dev == m2.st_dev )
    return 0;

  // they are both regular files.
  // they are /different/ regular files.

  // Are we accessing them exclusively?
  // TODO
  //  * use either advisorly locks or leases...
  //  it's unclear at this point which would
  //  be better.
  //  * actually, it's unclear if this is necessary...

  return 1;
}


