#include "profiler.h"
#include "timer.h"
#include "trailing_assert.h"

#include <cstdio>

void Profiler::begin()
{
#ifdef DEBUG_SPEED
  begin_time = rdtsc();
#endif
}

void Profiler::write_results() const
{
  std::ofstream log("result.specpriv.profile.txt.tmp");
  print(log);
  log.close();

  // Rename is an atomic operation
  rename("result.specpriv.profile.txt.tmp", "result.specpriv.profile.txt");
}

void Profiler::timing_stats(std::ostream &log) const
{
#if TIMER
  log << "# ---- Timing\n"
      << "# live.lookupPointer: " << total_time_lookup_pointer         << " / " << num_pointer_lookups << '\n'
      << "#   pred.predict_int: " << total_time_predict_int            << " / " << num_predict_int << '\n'
      << "#   pred.predict_ptr: " << total_time_predict_ptr            << " / " << num_predict_ptr << '\n'
      << "#   pred.ptr_residue: " << total_time_pointer_residue        << " / " << num_pointer_residue << '\n'
      << "#       pred.find_uo: " << total_time_find_underlying_object << " / " << num_find_underlying_object << '\n';
#endif
}

#ifdef DEBUG_SPEED
void Profiler::test_print(std::ostream &log) const
{
  const uint64_t total_time = rdtsc() - begin_time;
  log << "# Event histogram:\n"
      << "# ---- AU registration\n"
      << "#  malloc " << evt_malloc << '\n'
      << "# realloc " << evt_realloc << '\n'
      << "#    free " << evt_free << '\n'
      << "#   const " << evt_constant << '\n'
      << "#  global " << evt_global << '\n'
      << "#   stack " << evt_stack << '\n'
      << "# ---- Context manipulation\n"
      << "#    +fcn " << evt_begin_fcn << '\n'
      << "#    -fcn " << evt_end_fcn << '\n'
      << "#   +iter " << evt_begin_iter << '\n'
      << "#   -iter " << evt_end_iter << '\n'
      << "# aus_cnt " << total_aus_size << '\n'
      << "# ---- Timing\n"
      << "#total_time " << total_time << '\n'
      << "#begin_fcn  " << total_time_begin_function << '\n'
      << "#end_fcn    " << total_time_end_function << '\n'
      << "# free st   " << total_time_freestack<< '\n'
      << "#report st  " << total_time_report_stack<< '\n'
      << "#free       " << total_time_free << '\n'
      // << "# ---- Instrumentation\n"
      // << "#     fuo " << evt_fuo << '\n'
      // << "#   p int " << evt_pred_int << '\n'
      // << "#   p ptr " << evt_pred_ptr << '\n'
      // << "# residue " << evt_ptr_residue << '\n'
      << "#\n";
}
#endif

void Profiler::print(std::ostream &log) const
{
  log << "BEGIN SPEC PRIV PROFILE\n";
  log << "# Event histogram:\n"
      << "# ---- AU registration\n"
      << "#  malloc " << evt_malloc << '\n'
      << "# realloc " << evt_realloc << '\n'
      << "#    free " << evt_free << '\n'
      << "#   const " << evt_constant << '\n'
      << "#  global " << evt_global << '\n'
      << "#   stack " << evt_stack << '\n'
      << "# ---- Context manipulation\n"
      << "#    +fcn " << evt_begin_fcn << '\n'
      << "#    -fcn " << evt_end_fcn << '\n'
      << "#   +iter " << evt_begin_iter << '\n'
      << "#   -iter " << evt_end_iter << '\n'
      << "# ---- Instrumentation\n"
      << "#     fuo " << evt_fuo << '\n'
      << "#   p int " << evt_pred_int << '\n'
      << "#   p ptr " << evt_pred_ptr << '\n'
      << "# residue " << evt_ptr_residue << '\n'
      << "#\n";

  // Can we guarantee that the profile was COMPLETE
  // w.r.t the allocation/deallocation of pointers?
  if( possibleAllocationLeaks.empty() )
    log << "COMPLETE ALLOCATION INFO ;\n";
  for(FcnNameList::iterator i=possibleAllocationLeaks.begin(), e=possibleAllocationLeaks.end(); i!=e; ++i)
    log << "INCOMPLETE ALLOCATION INFO " << *i << " ;\n";


  log << liveObjects;
  log << escapes;
  log << predictions;

  log << "END SPEC PRIV PROFILE\n";
}

void Profiler::malloc(const char *name, void *ptr, uint64_t size)
{
  ++evt_malloc;
  add_temporary_au(AU_Heap,name,ptr,size);
}

void Profiler::realloc(const char *name, void *old_ptr, void *new_ptr, uint64_t size)
{
  ++evt_realloc;

  if( old_ptr != 0 )
  {
#if TIMER
    const uint64_t start = rdtsc();
#endif
    AUHolder au = liveObjects.lookupPointer(old_ptr);
#if TIMER
    total_time_lookup_pointer += rdtsc() - start;
    ++num_pointer_lookups;
#endif

    const char *old_name = au->name;
    CtxHolder old_creation = au->creation;
    const uint64_t old_size = au->extents.size();
    free(au);

    // Some benchmarks hold reference to and/or perform address arithmetic
    // yielding addresses that fall in the excess that realloc cuts off of the
    // end of an object.  e.g. 429.mcf does this a lot.  This behavior is
    // undefined, and fools the pointer-to-object resolution logic in the
    // profiler.
    //
    // To work around this, Thom Jablin suggested a realloc-doesn't-shrink
    // heuristic, which allows the profiler to work around 429's undefined
    // behavior, but it tends to break other benchmarks like 175.vpr.
    //
    // This is a hybrid solution: when realloc shrinks an object, we create a
    // dummy object representing the lost space.  We track that, so we can
    // resolve pointers to that region (per 429.mcf), but we treat them as
    // already freed and subordinate to real objects.
    if( old_ptr == new_ptr )
      if( size > 0 && old_size > size )
      {
        // Create a new AU that occupies the trailing bytes which were
        // freed.
        uint64_t base = size + (uint64_t)old_ptr;
        right_open_interval extents((void*)base, old_size-size);
        AUHolder excess = new AllocationUnit(AU_Heap, extents, old_name, old_creation);
        excess->attrs.realloc_shrink_excess = true;
        excess->deletion = currentContext;

        liveObjects.add_temporary(excess);
      }
  }

  if( size != 0 )
    malloc(name,new_ptr,size);
}

void Profiler::free(const char *name, void *ptr, bool isAlloca)
{
  ++evt_free;
#if TIMER
  const uint64_t start = rdtsc();
#endif
  AUHolder au = liveObjects.lookupPointer(ptr);
#if TIMER
  total_time_lookup_pointer += rdtsc() - start;
  ++num_pointer_lookups;
#endif

  free(au, isAlloca);
}

static bool warnFreeHeapAsStackOnce = false,
            warnFreeStackAsHeapOnce = false;

void Profiler::free(AUHolder &au, bool isAlloca)
{
#ifdef DEBUG_SPEED
  const uint64_t start = rdtsc();
#endif
  if( ! isAlloca ) // heap
  {
    if( au->type != AU_Heap )
    {
      if( ! warnFreeStackAsHeapOnce )
      {
        fprintf(stderr, "Warning: expected heap object, but free()d object %s not on heap.\n",
          au->name);
        warnFreeStackAsHeapOnce = true;
      }
      return;
    }
  }
  else if( isAlloca ) // Stack
  {
    if( au->type != AU_Stack )
    {
      if( ! warnFreeHeapAsStackOnce )
      {
        fprintf(stderr, "Warning: expected stack object, but free()d object %s is not on stack.\n",
          au->name);
        warnFreeHeapAsStackOnce = true;
      }
      return;
    }
  }

  au->deletion = currentContext;

  CtxHolder local = au->creation->findCommon( au->deletion );

  escapes.report_local(au, local);

  liveObjects.remove( au );

  remove_from_context( au );

#ifdef DEBUG_SPEED 
  total_time_free += rdtsc() - start;
#endif
}

void Profiler::report_constant(const char *name, void *base, uint64_t size)
{
  ++evt_constant;
  add_permanent_au(AU_Constant,name,base,size);
}

void Profiler::report_global(const char *name, void *base, uint64_t size)
{
  ++evt_global;
  add_permanent_au(AU_Global,name,base,size);
}

void Profiler::report_stack(const char *name, void *base, uint64_t array_size, uint64_t elt_size)
{
#ifdef DEBUG_SPEED
  const uint64_t start = rdtsc();
#endif
  ++evt_stack;
  if( 0 == array_size )
    array_size = 1;
  const uint64_t size = array_size * elt_size;

  add_temporary_au(AU_Stack,name,base,size);
#ifdef DEBUG_SPEED 
  total_time_report_stack += rdtsc() - start;
#endif
}

void Profiler::begin_function(const char *name)
{
#ifdef DEBUG_SPEED
  const uint64_t start = rdtsc();
#endif
  
  ++evt_begin_fcn;
  enter_ctx(Function,name);

#ifdef DEBUG_SPEED 
  total_time_begin_function += rdtsc() - start;
#endif
}

void Profiler::end_function(const char *name)
{
#ifdef DEBUG_SPEED
  const uint64_t start = rdtsc();
#endif

  ++evt_end_fcn;
  free_stacks();

#ifdef DEBUG_SPEED 
  total_time_freestack += rdtsc() - start;
#endif

  if( !name ) name = currentContext->name;
  exit_ctx(Function,name);

#ifdef DEBUG_SPEED 
  total_time_end_function += rdtsc() - start;
#endif
}

void Profiler::begin_iter(const char *name)
{
  ++evt_begin_iter;
  enter_ctx(Loop,name);
}

void Profiler::end_iter(const char *name)
{
  ++evt_end_iter;
  if( !name ) name = currentContext->name;
  exit_ctx(Loop,name);
}

static const char *name_of_last_unknown_object = 0;

void Profiler::find_underlying_object(const char *name, void *ptr)
{
  ++evt_fuo;
#if TIMER
  const uint64_t start = rdtsc();
#endif
  AUHolder au = liveObjects.lookupPointer(ptr);
#if TIMER
  total_time_lookup_pointer += rdtsc() - start;
  ++num_pointer_lookups;
#endif


  if( DEBUG )
  {
    if( au->type == AU_Unknown )
    {
      if( name != name_of_last_unknown_object )
      {
        fprintf(stderr, "Ptr '%s' to %p is unknown\n", name, ptr);

        if( STOP_ON_FIRST_UNKNOWN )
          abort();
      }

      name_of_last_unknown_object = name;
    }
  }

#if TIMER
  const uint64_t middle = rdtsc();
#endif
  PtrSample object(au);
  predictions.find_underlying_object(currentContext,name,object);
#if TIMER
  total_time_find_underlying_object += rdtsc() - middle;
  ++num_find_underlying_object;
#endif
}

void Profiler::predict_int(const char *name, uint64_t value)
{
  ++evt_pred_int;

#if TIMER
  const uint64_t start = rdtsc();
#endif
  IntSample sample(value);
  predictions.predict_int(currentContext,name,sample);
#if TIMER
  total_time_predict_int += rdtsc() - start;
  ++num_predict_int;
#endif
}

void Profiler::predict_ptr(const char *name, void *ptr)
{
  ++evt_pred_ptr;

#if TIMER
  const uint64_t start = rdtsc();
#endif
  AUHolder au = liveObjects.lookupPointer(ptr);
#if TIMER
  const uint64_t middle1 = rdtsc();
  total_time_lookup_pointer += middle1 - start;
  ++num_pointer_lookups;
#endif


  uint64_t offset = ptr - au;
  PtrSample sample = PtrSample(au,offset);
  predictions.predict_ptr(currentContext,name,sample);

#if TIMER
  const uint64_t middle2 = rdtsc();
  total_time_predict_ptr += middle2 - middle1;
  ++num_predict_ptr;
#endif

  // Also predict pointer residues
  predictions.pointer_residue(currentContext,name,ptr);

#if TIMER
  total_time_pointer_residue += rdtsc() - middle2;
  ++num_pointer_residue;
#endif
}

void Profiler::pointer_residue(const char *name, void *ptr)
{
  ++evt_ptr_residue;
#if TIMER
  const uint64_t start = rdtsc();
#endif
  predictions.pointer_residue(currentContext,name,ptr);
#if TIMER
  total_time_pointer_residue += rdtsc() - start;
  ++num_pointer_residue;
#endif
}

AUHolder Profiler::add_temporary_au(AUType type, const char *name, void *base, uint64_t size)
{
  right_open_interval key(base,size);

  // Moved from AllocationUnitTable::add_temporary:
  //
  // If more than one stack object from this context is allocated
  // to the same address, remove the old one.
  //
  // (this can happen, for instance, because two allocas have non-overlapping
  //  lifetimes (as reported by llvm.lifetime.start, llvm.lifetime.end), and
  //  the backend merges them into a single alloca)
  for(CtxHolder ctx=currentContext; ! ctx.is_null(); ctx = ctx->parent)
  {
    for(unsigned i=0; i<ctx->aus.size(); ++i)
    {
      AUHolder old = (AllocationUnit*) * ctx->aus[i];

      if( old->type == AU_Stack
      &&  old->extents.includes(base) )
      {
        free_one_stack(ctx, i);
        --i;
      }
    }

    if( ctx->type != Loop )
      break;
  }

  // Add the new object, or find it if it was a repeat...
  AUHolder au = liveObjects.add_temporary(type,key,name,currentContext);

  // Add it to the context unless it was a repeat.
  for(unsigned i=0, N=currentContext->aus.size(); i<N; ++i)
    if( au == (AUHolder&) currentContext->aus[i] )
      return (AUHolder&) currentContext->aus[i];
  currentContext->aus.push_back(*au);

  return au;
}

void Profiler::add_permanent_au(AUType type, const char *name, void *base, uint64_t size)
{
  right_open_interval key(base,size);
  liveObjects.add_permanent(type,key,name,currentContext);
}

// An AU is in the live-aus list of exactly one
// live context.  Find and delete it.
void Profiler::remove_from_context(const AUHolder &au)
{
  // Remove this au from the live objects list.
  for(CtxHolder ctx = currentContext; !ctx.is_null(); ctx = ctx->parent)
  {
    for(unsigned i=0; i<ctx->aus.size(); ++i)
    {
      AUHolder match = (AllocationUnit *) * ctx->aus[i];
      if( *match == *au ) // pointer compare!
      {
        ctx->aus[i] = ctx->aus.back();
        ctx->aus.pop_back();
        return;
      }
    }
  }
}

void Profiler::enter_ctx(CtxType type, const char *name)
{
  Context *f = new Context(type);
  f->name = name;
  f->parent = currentContext;
  currentContext = f;
}

void Profiler::exit_ctx(CtxType type, const char *name)
{
  trailing_assert( currentContext->type == type
  &&               currentContext->name == name );

  // All live aus in this context escape.
  for(unsigned i=0, N=currentContext->aus.size(); i<N; ++i)
  {
    AUHolder au = (AllocationUnit*) *currentContext->aus[i];
    escapes.report_escape( au, currentContext );
  }

  // They are copied to the parent context.
  CtxHolder parent = currentContext->parent;
  parent->aus.insert( parent->aus.end(),
    currentContext->aus.begin(), currentContext->aus.end() );
  currentContext->aus.clear();

  // Accumulate value predictions.
  predictions.exit_ctx(currentContext);

  // pop context stack
  currentContext = parent;
}

void Profiler::free_stacks()
{
  Context::AUs &aus = currentContext->aus;
#ifdef DEBUG_SPEED
  total_aus_size += aus.size();
#endif
  for(int i=aus.size()-1; i>=0; --i)
  {
    AUHolder au = (AllocationUnit*) *aus[i];

    // Is this an alloca?
    if( au->type == AU_Stack
    &&  au->creation->innermostFunction() == currentContext )
    {
      free_one_stack(currentContext, i);
    }
  }
}

void Profiler::free_one_stack(CtxHolder ctx, unsigned idx)
{
  Context::AUs &aus = ctx->aus;
  AUHolder au = (AllocationUnit*) *aus[idx];

  // Implicitly free()ed when function returns
  au->deletion = currentContext;
  liveObjects.remove( au );

  escapes.report_local(au, currentContext->findCommon(ctx) );

  // Remove it from the AU list.
  // Swap with last, then pop-back
  aus[idx] = aus.back();
  aus.pop_back();
}

void Profiler::end()
{
  // We model a call to exit() as repeatedly returning
  // from all active functions.
  while( currentContext->type != Top )
  {
    if( currentContext->type == Function )
      end_function( currentContext->name );
    else if( currentContext->type == Loop )
      end_iter( currentContext->name );
  }

  while( !currentContext->aus.empty() )
  {
    AUHolder au = (AllocationUnit*) * currentContext->aus.back();
    currentContext->aus.pop_back();

    // Globals, constants always escape, we don't care about those.
    if( au->type != AU_Global && au->type != AU_Constant )
      escapes.report_escape(au, currentContext);
  }

  write_results();
}

const char *name_of_last_assert_fail = 0;

// If the profiler ran in sanity check mode,
// then it emits calls to this.  These check at profile collection
// time that pointer arithmetic does not escape AU bounds.
void Profiler::assert_in_bounds(const char *name, void *base, void *derived)
{
  if( DEBUG )
  {
#if TIMER
    const uint64_t start = rdtsc();
#endif
    AUHolder base_au = liveObjects.lookupPointer(base);
#if TIMER
    const uint64_t middle = rdtsc();
    total_time_lookup_pointer += middle - start;
    ++num_pointer_lookups;
#endif

    if( base_au->type == AU_Unknown )
      return;

    AUHolder derived_au = liveObjects.lookupPointer(base);
#if TIMER
    total_time_lookup_pointer += rdtsc() - middle;
    ++num_pointer_lookups;
#endif

    if( *base_au != *derived_au )
    {
      if( name != name_of_last_assert_fail )
        fprintf(stderr, "Assert in-bounds: failed on %s\n", name);
      name_of_last_assert_fail = name;
    }
  }
}

void Profiler::possible_allocation_leak(const char *name)
{
  if( DEBUG )
    if( !possibleAllocationLeaks.count(name) )
      fprintf(stderr, "Possible allocation leak: %s\n", name);

  possibleAllocationLeaks.insert(name);
}

Profiler *Profiler::theInstance = 0;



