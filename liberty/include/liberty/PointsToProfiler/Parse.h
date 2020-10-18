#ifndef LLVM_LIBERTY_MALLOC_PROFILER_PARSE_H
#define LLVM_LIBERTY_MALLOC_PROFILER_PARSE_H

#include "llvm/ADT/FoldingSet.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "liberty/PointsToProfiler/Pieces.h"

#include <map>
#include <list>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

struct SemanticAction
{
  SemanticAction() : valid(false) {}
  virtual ~SemanticAction() {}

  virtual bool sem_escape_object(AU *au, Ctx *ctx, unsigned cnt) = 0;
  virtual bool sem_local_object(AU *au, Ctx *ctx, unsigned cnt) = 0;
  virtual bool sem_int_predict(Value *value, Ctx *ctx, Ints &ints) = 0;
  virtual bool sem_ptr_predict(Value *value, Ctx *ctx, Ptrs &ptrs) = 0;
  virtual bool sem_obj_predict(Value *value, Ctx *ctx, Ptrs &ptrs) = 0;
  virtual bool sem_pointer_residual(Value *value, Ctx *ctx, unsigned short bitvector) = 0;

  virtual void sem_set_valid(bool isValid) { valid = isValid; }

  virtual AU *fold(AU *) const = 0;
  virtual Ctx *fold(Ctx *) const = 0;

  bool resultsValid() const { return valid; }

private:
  bool valid;

};

/// This class is responsible for parsing the profile,
/// and generating a series of semantic action callbacks.
/// Clients should inherit from this, and overload the
/// sem_*() methods.
struct Parse
{
  Parse(Module &mod);
  virtual ~Parse() {}

  void parse(const char *filename, SemanticAction *sema);

private:
  Module &module;

  SemanticAction *sema;

  // Ad hoc recursive descent parser.
  typedef std::list<std::string> TokList;
  TokList prev_tokens, next_tokens;
  unsigned lineno;
  bool parse_line(char *line);

  // Is the profile information complete w.r.t.
  //  - Allocation coverage?
  bool isAllocationCoverageComplete;

  // If the next token is keyword, consume it and return true
  // otherwise, return false and do not consume
  bool test(const char *keyword);

  // Consume the next token.  Return true iff that token==keyword
  bool expect(const char *keyword);

  // Consume an arbitrary string; report error if end of tokens.
  bool consume(const char *desc, std::string &out);

  bool parse_complete();
  bool parse_incomplete();

  bool parse_escape_object();
  bool parse_local_object();
  bool parse_predict_int();
  bool parse_predict_ptr();
  bool parse_underlying_obj();
  bool parse_pointer_residues();

  bool parse_au(AU **au);
  bool parse_ctx(Ctx **ctx);
  bool parse_int(unsigned *u);

  bool parse_ctxs(Ctx **ctx);
  bool parse_ptrs(Ptrs &ptrs);
  bool parse_ptr(Ptrs &ptrs);

  bool parse_prediction(Value **vout, Ctx **cout, unsigned *nsout, unsigned *nvout);
  bool parse_ptr_predictions(Value **vout, Ctx **cout, Ptrs &ptrsout);

  bool parse_int_sample(Ints &intsout);
  bool parse_int_samples(Ints &intsout);
  bool parse_int_predictions(Value **valueout, Ctx **ctxout, Ints &intsout);

  bool parse_gv(GlobalVariable **gvout);
  bool parse_fcn(Function **fcnout);
  bool parse_bb(BasicBlock **bbout);
  bool parse_inst(Instruction **instout);
  bool parse_value(Value **valueout);

  raw_ostream &error();
};



}
}

#endif

