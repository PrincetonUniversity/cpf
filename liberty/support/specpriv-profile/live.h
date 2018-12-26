#ifndef SPECPRIV_LIVE_H
#define SPECPRIV_LIVE_H

#include <cstdlib>
#include <cassert>
#include <map>
#include <stdint.h>
#include <ostream>

#include "holder.h"
#include "context.h"


// Represents the range [low,hi)
struct right_open_interval
{
  uint64_t low, high;

  right_open_interval(void *base, uint64_t size=1) : low( (uint64_t)base ), high(low + size) {}

  bool includes(void *ptr) const
  {
    uint64_t iptr = (uint64_t)ptr;
    return low <= iptr && iptr < high;
  }

  // We use == to signify overlap
  bool operator==(const right_open_interval &other) const;

  // We use < to order intervals.
  bool operator<(const right_open_interval &other) const { return high <= other.low; }

  // Containment
  bool is_wholly_within(const right_open_interval &other) const
  {
    return other.low <= low && high <= other.high;
  }

  uint64_t size() const { return high - low; }

  void print(std::ostream &fout) const;
};

// Info collected per allocation unit.
enum AUType { AU_Null=0, AU_Unknown, AU_Constant, AU_Global, AU_Stack, AU_Heap };
struct AUAttributes
{
  AUAttributes() : realloc_shrink_excess(false) {}

  bool                realloc_shrink_excess:1;
};
struct AllocationUnit : public RefCount
{
  AUType              type;
  AUAttributes        attrs;
  right_open_interval extents;
  const char        * name;
  CtxHolder           creation;
  CtxHolder           deletion;

  AllocationUnit(AUType t=AU_Unknown, const right_open_interval &e=(void*)0, const char *n=0, const CtxHolder &c=0)
    : RefCount(), type(t), attrs(), extents(e), name(n), creation(c) {}

  static AllocationUnit *Null() { return new AllocationUnit(AU_Null); }
  static AllocationUnit *Unknown() { return new AllocationUnit(AU_Unknown); }

  bool operator==(const AllocationUnit &other) const
  {
    return type == other.type
    &&     name == other.name
//    &&     extents == other.extents
    &&     creation == other.creation;
  }

  bool operator<(const AllocationUnit &other) const;

  void print(std::ostream &fout) const;
};

typedef Holder<AllocationUnit> AUHolder;

std::ostream &operator<<(std::ostream &fout, const right_open_interval &roi);
std::ostream &operator<<(std::ostream &fout, const AUHolder &au);


// Determine offset of pointer within allocation unit.
uint64_t operator-(void *, const AUHolder &);

struct AllocationUnitTable
{
  AllocationUnitTable()
    : permanents(), temporaries(), peak_permanents(0), peak_temporaries(0), num_split_constants(0) {}

  // A binary tree of NON-OVERLAPPING intervals.
  typedef std::map< right_open_interval, AUHolder > AllocationUnitMap;


  bool count(const right_open_interval &) const;
  AUHolder lookupPointer(void *);

  AUHolder add_temporary(AUType,const right_open_interval &, const char *, const CtxHolder &);
  AUHolder add_temporary(const AUHolder &);
  AUHolder add_permanent(AUType,const right_open_interval &, const char *, const CtxHolder &);

  void remove(const AUHolder &);

  void print(std::ostream &) const;

private:
  AllocationUnitMap permanents, temporaries;
  AUHolder  mostRecentlyUsed;

  // statistics
  unsigned peak_permanents, peak_temporaries;
  unsigned num_split_constants;
};

std::ostream &operator<<(std::ostream &fout, const AllocationUnitTable &aut);

#endif

