#ifndef SPECPRIV_PROFILER_H
#define SPECPRIV_PROFILER_H

#include <cstdlib>
#include <cassert>
#include <map>
#include <set>
#include <fstream>

#include <stdint.h>

#include "config.h"
#include "context.h"
#include "live.h"
#include "prediction.h"
#include "escape.h"

struct Profiler
{
  void begin();
  void end();

  void write_results() const;
  void print(std::ostream &log) const;
  void malloc(const char *name, void *ptr, uint64_t size);
  void realloc(const char *name, void *old_ptr, void *new_ptr, uint64_t size);
  void free(const char *name, void *ptr, bool isAlloca = false);
  void free(AUHolder &au, bool isAlloca = false);
  void report_constant(const char *name, void *base, uint64_t size);
  void report_global(const char *name, void *base, uint64_t size);
  void report_stack(const char *name, void *base, uint64_t array_size, uint64_t elt_size);
  void begin_function(const char *name);
  void end_function(const char *name);
  void begin_iter(const char *name);
  void end_iter(const char *name);
  void predict_int(const char *name, uint64_t value);
  void find_underlying_object(const char *name, void *ptr);

  // Note: predict_ptr implies pointer_residue
  void predict_ptr(const char *name, void *ptr);
  void pointer_residue(const char *name, void *ptr);

  void assert_in_bounds(const char *name, void *base, void *derived);
  void possible_allocation_leak(const char *name);

  // This is a  singleton class.
  static Profiler &getInstance()
  {
    if( !theInstance )
      theInstance = new Profiler;
    return *theInstance;
  }

  void timing_stats(std::ostream &log) const;

private:
  // SINGLETON
  static Profiler *theInstance;
  Profiler()
  {
    currentContext = new Context(Top);

    // statistics
    evt_malloc = 0;
    evt_free = 0;
    evt_constant = 0;
    evt_global = 0;
    evt_stack = 0;
    evt_begin_fcn = 0;
    evt_end_fcn = 0;
    evt_begin_iter = 0;
    evt_end_iter = 0;
    evt_fuo = 0;
    evt_pred_int = 0;
    evt_pred_ptr = 0;
    evt_realloc = 0;
    evt_ptr_residue = 0;

#if TIMER
    total_time_lookup_pointer = 0;
    num_pointer_lookups = 0;
    total_time_predict_int = 0;
    num_predict_int = 0;
    total_time_predict_ptr = 0;
    num_predict_ptr = 0;
    total_time_pointer_residue = 0;
    num_pointer_residue = 0;
    total_time_find_underlying_object = 0;
    num_find_underlying_object = 0;
#endif
  }

  AUHolder add_temporary_au(AUType type, const char *name, void *base, uint64_t size);
  void add_permanent_au(AUType type, const char *name, void *base, uint64_t size);
  void remove_from_context(const AUHolder &au);
  void enter_ctx(CtxType type, const char *name);
  void exit_ctx(CtxType type, const char *name);
  void free_stacks();
  void free_one_stack(CtxHolder ctx, unsigned idx);

  // Runtime information
  AllocationUnitTable liveObjects;
  CtxHolder currentContext;

  // Tabulated results which will be saved
  PredictionTable predictions;
  EscapeTable escapes;

  // Possible allocation leaks
  typedef std::set<const char *> FcnNameList;
  FcnNameList possibleAllocationLeaks;


  // Count of dynamic events
  unsigned evt_malloc, evt_free, evt_constant, evt_global,
           evt_stack, evt_begin_fcn, evt_end_fcn, evt_begin_iter,
           evt_end_iter, evt_fuo, evt_pred_int, evt_pred_ptr,
           evt_realloc, evt_ptr_residue;

#if TIMER
  // Time for various events.
  uint64_t total_time_lookup_pointer;
  unsigned num_pointer_lookups;

  uint64_t total_time_predict_int;
  unsigned num_predict_int;

  uint64_t total_time_predict_ptr;
  unsigned num_predict_ptr;

  uint64_t total_time_pointer_residue;
  unsigned num_pointer_residue;

  uint64_t total_time_find_underlying_object;
  unsigned num_find_underlying_object;
#endif
};

#endif

