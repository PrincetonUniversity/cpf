#include <cstdio>
#include <iostream>

#include "config.h"
#include "live.h"
#include "trailing_assert.h"

bool right_open_interval::operator==(const right_open_interval &other) const
{
  if( low <= other.low && other.low < high )
    return true;
  else if( other.low <= low && low < other.high )
    return true;
  else
    return false;
}

void right_open_interval::print(std::ostream &fout) const
{
  fout << '[' << low << ", " << high << ')';
}

bool AllocationUnit::operator<(const AllocationUnit &other) const
{
  if( type < other.type )
    return true;
  else if( type > other.type )
    return false;

  else if( name < other.name )
    return true;
  else if( name > other.name )
    return false;

  else
    return creation < other.creation;
}

void AllocationUnit::print(std::ostream &fout) const
{
  const char *types[] = {
    "NULL", "UNKNOWN", "CONSTANT",
    "GLOBAL", "STACK", "HEAP" };

  fout << "AU " << types[type] << ' ';
  if( type != AU_Null && type != AU_Unknown )
  {
    fout << name;
    if( type != AU_Global && type != AU_Constant)
      fout << " FROM " << creation;
  }
}

std::ostream &operator<<(std::ostream &fout, const AUHolder &au)
{
  if( au.is_null() )
    fout << "BAD-AU";
  else
    au->print(fout);
  return fout;
}

uint64_t operator-(void *ptr, const AUHolder &au)
{
  if( au->type == AU_Unknown || au->type == AU_Null )
    return 0;

  uint64_t addr = (uint64_t)ptr;
  const right_open_interval &interval = au->extents;

//  trailing_assert( interval.low <= addr && addr < interval.high );

  return addr - interval.low;
}

bool AllocationUnitTable::count(const right_open_interval &key) const
{
  return temporaries.count(key) || permanents.count(key);
}

AUHolder AllocationUnitTable::lookupPointer(void *ptr)
{
  if( !ptr )
    return AllocationUnit::Null();

  if( ! mostRecentlyUsed.is_null() )
    if( mostRecentlyUsed->extents.includes(ptr) )
      return mostRecentlyUsed;

  right_open_interval key(ptr);

  // First look for proper inclusion in temporary AUs...
  AllocationUnitMap::iterator i = temporaries.find( key );
  if( i != temporaries.end() )
    return mostRecentlyUsed = i->second;

  // ... proper inclusion in permanents...
  i = permanents.find( key );
  if( i != permanents.end() )
    return mostRecentlyUsed = i->second;

  // Some codes do ugly things, such as compute a pointer
  // just beyond an AU, and use that as an iteration
  // bound.  These are called 'disguised pointers'.
  // (the C99 spec allows for pointer arithmetic
  //  that includes one byte beyond the AU)
  // The remainder of this function tries to deal with
  // disguised pointers.

  // ... just beyond a temporary AU?
  i = temporaries.lower_bound( key );
  if( i != temporaries.end() )
  {
    if( i != temporaries.begin() )
    {
      --i;
      uint64_t delta = ((uint64_t) ptr) - i->first.high;
      if( delta < 16 )
        return mostRecentlyUsed = i->second;
    }
  }

  // ... just beyond a permanent AU?
  i = permanents.lower_bound( key );
  if( i != permanents.end() )
    if( i != permanents.begin() )
    {
      --i;
      uint64_t delta = ((uint64_t) ptr) - i->first.high;
      if( delta < 16 )
        return mostRecentlyUsed = i->second;
    }

  // Widen null a bit.
  if( 1024 > (uint64_t)ptr )
    return AllocationUnit::Null();

  return AllocationUnit::Unknown();
}

AUHolder AllocationUnitTable::add_temporary(AUType type, const right_open_interval &extents, const char *name, const CtxHolder &ctx)
{
  AUHolder au = new AllocationUnit(type,extents,name,ctx);
  return add_temporary(au);
}

AUHolder AllocationUnitTable::add_temporary(const AUHolder &au)
{
  /* moved to Profiler::add_temporary_au
  AllocationUnitMap::iterator i = temporaries.find( au->extents  );
  if( temporaries.count( au->extents ) )
    remove(i->second);
   */
  AllocationUnitMap::iterator i = temporaries.find( au->extents );
  if( i != temporaries.end() )
  {
    // This is only okay if the temporary was reported multiple
    // times (which can happen, for instance, if lifetime.start/.end
    // intrinsics are placed in a way that allows you to visit
    // lifetime.start twice before lifetime.end...
    if( au == i->second )
    {
//      std::cerr << "Warning: pesky lifetime.start/.end pattern\n";
      return i->second; // okay
    }

    // Also, it's okay if the collision is against a fake object representing
    // the excess after shrinking an object with realloc().
    else if( i->second->attrs.realloc_shrink_excess )
    {
      temporaries.erase(i);
    }

    else
    {
      // std::cerr << "Address collision:\n"
      //           << " Old temporary at " << i->second->extents << ": "  << i->second << '\n'
      //           << " New temporary at " << au->extents << ": " << au << '\n';
      // Ziyang 9/26: This can happen if the object is deleted by the deleting destructor
      //              and the points-to profiler doesn't see in the deleting destructor;
      //              or any other pattern where a virtual function that is not addressed
      //              by devirtualization delete the object; or both.
      //              FIXME: add new patterns;
      //              (1) recognize D0Ev deleting destructor, and if it's an external function,
      //              register it as if it's a delete
      //              (2) recognize a virtual function call site with the pointer as an argument
      //              and when the collision happens, assume the object is released.
      trailing_assert( false && "repeat address t-t" );
    }
  }

  trailing_assert( !permanents.count( au->extents ) && "repeat address t-p" );
  temporaries[ au->extents ] = au;
  if( DEBUG )
    fprintf(stderr, "+T [%lx, %lx)   %s\n", au->extents.low, au->extents.high, au->name);
  if( temporaries.size() > peak_temporaries )
    peak_temporaries = temporaries.size();
  return au;
}

AUHolder AllocationUnitTable::add_permanent(AUType type, const right_open_interval &extents, const char *name, const CtxHolder &ctx)
{
  AUHolder au = new AllocationUnit(type,extents,name,ctx);
  trailing_assert( !temporaries.count( au->extents ) && "repeat address p-t" );

  // Unfortunately, constant variables in llvm
  // may share memory addresses.  I.e. repeated
  // string literal constants are emitted only once.
  // We must map constants() specially with this
  // knowledge.
  if( type == AU_Constant )
  {
    // Our approach is one of address-range coverage, not
    // of completeness.  If one is contained wholly within
    // another, we opt for the larger AU.  If they otherwise
    // overlap, we chop them so that each named AU contains
    // its offset 0 address.  e.g.
    //
    //      A------B    becomes A------B
    //        C---D
    //
    //      A---B      becomes A-CC----D
    //         C----D
    // etc.

    // repeatedly split as necessary, since an
    // allocation unit may collide with many...
    for(;;)
    {
      AllocationUnitMap::iterator i = permanents.find( au->extents );
      if( i == permanents.end() )
        break; // no more collisions :)

      AUHolder collision = i->second;
      trailing_assert( collision->type == AU_Constant && "Collisions only acceptable among constants");

      ++num_split_constants;

      if( au->extents.is_wholly_within( collision->extents ) )
      {
        // easiest case: do not insert the new constant.
        return collision;
      }
      else if( collision->extents.is_wholly_within( au->extents ) )
      {
        // slightly harder; replace old constant with new one.
        permanents.erase( i );
      }
      else if( au->extents.low < collision->extents.low )
      {
        // Update the new AU's bounds so it doesn't collide.
        au->extents.high = collision->extents.low;
      }
      else
      {
        // Update the old AU's extents so it doesn't collide.
        permanents.erase( i );
        collision->extents.high = au->extents.low;
        permanents[ collision->extents ] = collision;
      }
    }
  }

  trailing_assert( !permanents.count( au->extents ) && "repeat address p-p" );
  permanents[ au->extents ] = au;
  if( DEBUG )
    fprintf(stderr, "+P [%lx, %lx)   %s\n", au->extents.low, au->extents.high, au->name);
  if( permanents.size() > peak_permanents )
    peak_permanents = permanents.size();
  return au;
}

void AllocationUnitTable::remove(const AUHolder &au)
{
  AllocationUnitMap::iterator i = temporaries.find( au->extents );
  trailing_assert( i != temporaries.end() && "Can't remove");

  if( au == mostRecentlyUsed )
    mostRecentlyUsed = 0;

  temporaries.erase(i);
  if( DEBUG )
    fprintf(stderr, "-T [%lx, %lx)   %s\n", au->extents.low, au->extents.high, au->name);
}

void AllocationUnitTable::print(std::ostream &fout) const
{
  fout << "# Peak temporaries " << peak_temporaries << '\n';
  fout << "# Peak permanents " << peak_permanents << '\n';
  fout << "# Split constants " << num_split_constants << '\n';
}

std::ostream &operator<<(std::ostream &fout, const right_open_interval &roi)
{
  roi.print(fout);
  return fout;
}

std::ostream &operator<<(std::ostream &fout, const AllocationUnitTable &aut)
{
  aut.print(fout);
  return fout;
}
