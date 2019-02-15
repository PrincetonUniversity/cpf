#define DEBUG_TYPE "specpriv-parse"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#include "liberty/PointsToProfiler/Parse.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/FindUnderlyingObjects.h"
#include "liberty/Utilities/GetMemOper.h"

#include <stdio.h>
#include <sstream>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numLines, "Lines read from SpecPriv profile");

raw_ostream &Parse::error()
{
  raw_ostream &er = errs();
  er << lineno << ": ";

  for(TokList::const_iterator i=prev_tokens.begin(), e=prev_tokens.end(); i!=e; ++i)
    er << *i << ' ';

  er << " ### ";

  for(TokList::const_iterator i=next_tokens.begin(), e=next_tokens.end(); i!=e; ++i)
    er << *i << ' ';

  er << "\nParse error: ";
  return er;
}

bool Parse::test(const char *keyword)
{
  if( next_tokens.empty() )
    return false;

  if( next_tokens.front() != keyword )
    return false;

  prev_tokens.push_back( next_tokens.front() );
  next_tokens.pop_front();
  return true;
}

bool Parse::expect(const char *keyword)
{
  if( next_tokens.empty() )
  {
    error() << "expected " << keyword << '\n';
    return false;
  }

  if( next_tokens.front() != keyword )
  {
    error() << "expected " << keyword << '\n';
    return false;
  }

  prev_tokens.push_back( next_tokens.front() );
  next_tokens.pop_front();
  return true;
}

bool Parse::consume(const char *desc, std::string &out)
{
  if( next_tokens.empty() )
  {
    error() << "expected " << desc << '\n';
    return false;
  }

  out = next_tokens.front();
  next_tokens.pop_front();
  prev_tokens.push_back(out);
  return true;
}

#define EXPECT(s)     do { if( !expect(s) ) return false; } while(0)
#define CONSUME(d,s)  std::string s; if( !consume(d,s) ) return false;

bool Parse::parse_int(unsigned *u)
{
  CONSUME("integer", sint);

  if( 1 != sscanf(sint.c_str(), " %u", u) )
  {
    error() << "Failed to parse integer from " << sint << '\n';
    return false;
  }

  return true;
}

bool Parse::parse_gv(GlobalVariable **gvout)
{
  EXPECT("global");
  CONSUME("global variable name", name);
  GlobalVariable *gv = module.getGlobalVariable(name,true);
  if( !gv )
  {
    error() << "Global variable " << name << " not found\n";
    return false;
  }

  *gvout = gv;
  return true;
}

bool Parse::parse_fcn(Function **fcnout)
{
  CONSUME("function name", fname);
  Function *fcn = module.getFunction(fname);
  if( !fcn )
  {
    error() << "Function does not exist " << fname << '\n';
    return false;
  }

  *fcnout = fcn;
  return true;
}

bool Parse::parse_bb(BasicBlock **bbout)
{
  Function *fcn = 0;
  if( !parse_fcn(&fcn) )
    return false;

  CONSUME("basic block name", bbname);
  BasicBlock *bb = 0;
  for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
  {
    if( i->getName() == bbname )
    {
      bb = &*i;
      break;
    }
  }
  if( !bb )
  {
    error() << "Function has no block named " << bbname << '\n';
    return false;
  }

  *bbout = bb;
  return true;
}

bool Parse::parse_inst(Instruction **instout)
{
  BasicBlock *bb = 0;
  if( !parse_bb(&bb) )
    return false;

  CONSUME("instruction name or position", iname);

  if( iname[0] == '$' )
  {
    unsigned n = 0;
    if( 1 != sscanf(iname.c_str(), "$%u", &n) )
    {
      error() << "Cannot parse position from " << iname << '\n';
      return false;
    }
    unsigned pos=0;
    for(BasicBlock::iterator i=bb->begin(), e=bb->end(); i!=e; ++i, ++pos)
      if( pos == n )
      {
        *instout = &*i;
        return true;
      }

    error() << "Block " << bb->getName() << " has no instruction at position " << n << '\n';
    return false;
  }
  else
  {
    for(BasicBlock::iterator i=bb->begin(), e=bb->end(); i!=e; ++i)
      if( i->getName() == iname )
      {
        *instout = &*i;
        return true;
      }

    error() << "Block " << bb->getName() << " has no instruction named " << iname << '\n';
    return false;
  }
}

bool Parse::parse_value(Value **valueout)
{
  if( test("argument") )
  {
    Function *fcn = 0;
    if( !parse_fcn(&fcn) )
      return false;

    CONSUME("argument number", argstr);
    if( argstr[0] != '%' )
    {
      error() << "Expected argument number\n";
      return false;
    }

    unsigned u = 0;
    if( 1 != sscanf(argstr.c_str(), "%%%u", &u) )
    {
      error() << "Cannot parse argument number from " << argstr << '\n';
      return false;
    }

    // Return the u-th arg of fcn.
    Argument *arg = 0;
    unsigned pos=0;
    for(Function::arg_iterator i=fcn->arg_begin(), e=fcn->arg_end(); i!=e; ++i, ++pos)
      if( pos == u )
      {
        arg = &*i;
        break;
      }

    if( !arg )
    {
      error() << "Function " << fcn->getName() << " has no " << u << "-th arg!\n";
      return false;
    }

    *valueout = arg;
    return true;
  }

  Instruction *inst = 0;
  if( !parse_inst(&inst) )
    return false;

  *valueout = inst;
  return true;
}

bool Parse::parse_ctx(Ctx **ctxout)
{
  //  <ctx> ::= CONTEXT { <ctxs> }
  //  <ctxs> ::= <ctx0>
  //         ::= <ctx0> WITHIN <ctxs>
  //  <ctx0> ::= FUNCTION <fcn-name>
  //         ::= LOOP <fcn-name> <header-name> <depth>
  //         ::= TOP

  EXPECT("CONTEXT");
  EXPECT("{");

  Ctx *ctx = 0;
  if( !parse_ctxs(&ctx) )
    return false;

  EXPECT("}");

  *ctxout = ctx;
  return true;
}

bool Parse::parse_ctxs(Ctx **ctxout)
{
  if( test("TOP") )
  {
    *ctxout = sema->fold( new Ctx(Ctx_Top) );
    return true;
  }

  if( test("FUNCTION") )
  {
    Function *fcn = 0;
    if( !parse_fcn(&fcn) )
      return false;

    EXPECT("WITHIN");

    Ctx *parent = 0;
    if( !parse_ctxs(&parent) )
      return false;

    Ctx *ctx = new Ctx(Ctx_Fcn, parent);
    ctx->fcn = fcn;

    *ctxout = sema->fold(ctx);
    return true;
  }

  else if( test("LOOP") )
  {
    BasicBlock *bb = 0;
    if( !parse_bb(&bb) )
      return false;

    unsigned depth = 0;
    if( !parse_int(&depth) )
      return false;

    EXPECT("WITHIN");

    Ctx *parent = 0;
    if( !parse_ctxs(&parent) )
      return false;

    Ctx *ctx = new Ctx(Ctx_Loop, parent);
    ctx->fcn = bb->getParent();
    ctx->header = bb;
    ctx->depth = depth;

    *ctxout = sema->fold(ctx);
    return true;
  }

  error() << "Expected one of TOP, FUNCTION or LOOP\n";
  return false;
}

bool Parse::parse_au(AU **auout)
{
  //  <au> ::= AU UNKNOWN
  //       ::= AU NULL
  //       ::= AU CONSTANT <global-name>
  //       ::= AU CONSTANT UNMANAGED <xxx>
  //       ::= AU GLOBAL <global-name>
  //       ::= AU STACK <val-spec> FROM <ctx>
  //       ::= AU HEAP <val-spec> FROM <ctx>
  //       ::= AU HEAP UNMANAGED <xxx> FROM <ctx>

  EXPECT("AU");

  if( test("UNKNOWN") )
  {
    AU *au = 0;

    if( isAllocationCoverageComplete )
      au = new AU(AU_Undefined);
    else
      au = new AU(AU_Unknown);

    *auout = sema->fold(au);
    return true;
  }

  else if( test("NULL") )
  {
    AU *au = new AU(AU_Null);

    *auout = sema->fold(au);
    return true;
  }

  else if( test("CONSTANT") )
  {
    if( test("UNMANAGED") )
    {
      CONSUME("name of unmanaged memory", unmanaged);

      AU *au = new AU(AU_Unknown);
      *auout = sema->fold(au);
      return true;
    }

    GlobalVariable *gv = 0;
    if( !parse_gv(&gv) )
      return false;

    if( !gv->isConstant() )
    {
      error() << "Global variable " << gv->getName() << " is not a constant\n";
      return false;
    }

    AU *au = new AU(AU_Constant);
    au->value = gv;


    *auout = sema->fold(au);
    return true;
  }

  else if( test("GLOBAL") )
  {
    GlobalVariable *gv = 0;
    if( !parse_gv(&gv) )
      return false;

    if( gv->isConstant() )
    {
      error() << "Global variable " << gv->getName() << " is a constant\n";
      return false;
    }

    AU *au = new AU(AU_Global);
    au->value = gv;

    *auout = sema->fold(au);
    return true;
  }

  else if( test("STACK") )
  {
    Instruction *inst = 0;
    if( !parse_inst(&inst) )
      return false;

    if( !isa<AllocaInst>(inst) )
    {
      error() << "Instruction is not an alloca.\n";
      return false;
    }

    EXPECT("FROM");

    Ctx *ctx = 0;
    if( !parse_ctx(&ctx) )
      return false;

    AU *au = new AU(AU_Stack);
    au->value = inst;
    au->ctx = ctx;

    *auout = sema->fold(au);
    return true;
  }

  else if( test("HEAP") )
  {
    if( test("UNMANAGED") )
    {
      CONSUME("name of unmanaged memory", unmanaged);
      EXPECT("FROM");
      Ctx *ctx = 0;
      if( !parse_ctx(&ctx) )
        return false;

      AU *au = new AU(AU_Unknown);
      *auout = sema->fold(au);
      return true;
    }

    Instruction *inst = 0;
    if( !parse_inst(&inst) )
      return false;

    EXPECT("FROM");

    Ctx *ctx = 0;
    if( !parse_ctx(&ctx) )
      return false;

    AU *au = new AU(AU_Heap);
    au->value = inst;
    au->ctx = ctx;

    *auout = sema->fold(au);
    return true;
  }

  error() << "Expected one of UNKNOWN, NULL, CONSTANT, GLOBAL, STACK or HEAP\n";
  return false;
}

bool Parse::parse_complete()
{
  EXPECT("ALLOCATION");
  EXPECT("INFO");
  EXPECT(";");

  isAllocationCoverageComplete = true;
  return true;
}

bool Parse::parse_incomplete()
{
  EXPECT("ALLOCATION");
  EXPECT("INFO");

  Instruction *inst = 0;
  if( !parse_inst(&inst) )
    return false;

  EXPECT(";");

  errs() << "Profile has incomplete allocation coverage because " << *inst << '\n';

  isAllocationCoverageComplete = false;
  return true;
}

bool Parse::parse_escape_object()
{
  //  ESCAPE __ OBJECT <au> ESCAPES <ctx> COUNT <n> ;
  EXPECT("OBJECT");

  AU *au = 0;
  if( !parse_au(&au) )
    return false;


  EXPECT("ESCAPES");

  Ctx *ctx = 0;
  if( !parse_ctx(&ctx) )
    return false;


  EXPECT("COUNT");

  unsigned cnt = 0;
  if( !parse_int(&cnt) )
    return false;

  EXPECT(";");

  return sema->sem_escape_object(au,ctx,cnt);
}

bool Parse::parse_local_object()
{
  //  LOCAL __ OBJECT <au> LOCAL TO <ctx> COUNT <n> ;
  EXPECT("OBJECT");

  AU *au = 0;
  if( !parse_au(&au) )
    return false;

  EXPECT("IS");
  EXPECT("LOCAL");
  EXPECT("TO");

  Ctx *ctx = 0;
  if( !parse_ctx(&ctx) )
    return false;

  EXPECT("COUNT");

  unsigned cnt = 0;
  if( !parse_int(&cnt) )
    return false;

  EXPECT(";");

  return sema->sem_local_object(au,ctx,cnt);
}

bool Parse::parse_pointer_residues()
{
  EXPECT("RESIDUES");

  // PTR RESIDUES __ <name> AT <ctx> AS RESTRICTED <n> SAMPLES OVER <n> MEMBERS { <r-values> } ;
  Value *value = 0;
  if( !parse_value(&value) )
    return false;

  EXPECT("AT");

  Ctx *ctx = 0;
  if( !parse_ctx(&ctx) )
    return false;

  EXPECT("AS");
  EXPECT("RESTRICTED");

  // PTR RESIDUES <name> AT <ctx> AS RESTRICTED __ <n> SAMPLES OVER <n> MEMBERS { <r-values> } ;

  unsigned nsamples = 0;
  if( !parse_int(&nsamples) )
    return false;

  EXPECT("SAMPLES");
  EXPECT("OVER");
  unsigned pop_count = 0;
  if( !parse_int(&pop_count) )
    return false;

  EXPECT("MEMBERS");
  EXPECT("{");

  // PTR RESIDUES <name> AT <ctx> AS RESTRICTED <n> SAMPLES OVER <n> MEMBERS { __ <r-values> } ;
  unsigned char bit_vector = 0;
  for(;;)
  {
    unsigned member = 0;
    if( !parse_int(&member) )
      return false;

    if( member >= 16 )
    {
      error() << "Members of a residue set must be in range [0,16)!\n";
      return false;
    }

    unsigned char mask = 1u << member;
    bit_vector |= mask;

    if( test(",") )
      continue;
    else
      break;
  }

  EXPECT("}");
  EXPECT(";");

  return sema->sem_pointer_residual(value,ctx,bit_vector);
}

bool Parse::parse_predict_int()
{
  //  PRED INT __ <name> AT <ctx> AS PREDICTABLE <n> SAMPLES OVER <m> VALUES { <i-values> } ;
  Value *value = 0;
  Ctx *ctx = 0;
  Ints ints;
  if( !parse_int_predictions(&value,&ctx,ints) )
    return false;

  return sema->sem_int_predict(value,ctx,ints);
}

bool Parse::parse_predict_ptr()
{
  //  PRED PTR __ <name> AT <ctx> AS PREDICTABLE <n> SAMPLES OVER <m> VALUES { <p-values> } ;
  Value *value = 0;
  Ctx *ctx = 0;
  Ptrs ptrs;
  if( !parse_ptr_predictions(&value,&ctx,ptrs) )
    return false;

  return sema->sem_ptr_predict(value,ctx,ptrs);
}

bool Parse::parse_underlying_obj()
{
  //  PRED OBJ __ <name> AT <ctx> AS PREDICTABLE <n> SAMPLES OVER <m> VALUES { <p-values> } ;

  Value *value = 0;
  Ctx *ctx = 0;
  Ptrs ptrs;
  if( !parse_ptr_predictions(&value,&ctx,ptrs) )
    return false;

  return sema->sem_obj_predict(value,ctx,ptrs);
}

bool Parse::parse_ptr(Ptrs &ptrsout)
{
  //  <p-value> ::= ( OFFSET <n> BASE <au> COUNT <m> )

  EXPECT("(");
  EXPECT("OFFSET");

  unsigned offset;
  if( !parse_int(&offset) )
    return false;

  EXPECT("BASE");

  AU *au = 0;
  if( !parse_au(&au) )
    return false;

  EXPECT("COUNT");

  unsigned freq = 0;
  if( !parse_int(&freq) )
    return false;

  EXPECT(")");

  ptrsout.push_back( Ptr(au,offset,freq) );

  return true;
}


bool Parse::parse_ptrs(Ptrs &ptrsout)
{
  if( !parse_ptr(ptrsout) )
    return false;

  if( test(",") )
    return parse_ptrs(ptrsout);

  return true;
}


bool Parse::parse_prediction(Value **valueout, Ctx **ctxout, unsigned *nsamplesout, unsigned *nvaluesout)
{
  Value *value = 0;
  if( !parse_value(&value) )
    return false;

  EXPECT("AT");

  Ctx *ctx = 0;
  if( !parse_ctx(&ctx) )
    return false;

  EXPECT("AS");
  EXPECT("PREDICTABLE");

  unsigned nsamples = 0;
  if( !parse_int(&nsamples) )
    return false;

  EXPECT("SAMPLES");
  EXPECT("OVER");

  unsigned nvalues = 0;
  if( !parse_int(&nvalues) )
    return false;

  EXPECT("VALUES");

  *valueout = value;
  *ctxout = ctx;
  *nsamplesout = nsamples;
  *nvaluesout = nvalues;
  return true;
}

bool Parse::parse_int_sample(Ints &intsout)
{
  //  <i-value> ::= ( INT <n> COUNT <m> )
  EXPECT("(");

  EXPECT("INT");
  unsigned u = 0;
  if( !parse_int(&u) )
    return false;

  EXPECT("COUNT");
  unsigned freq = 0;
  if( !parse_int(&freq) )
    return false;

  EXPECT(")");

  intsout.push_back( Int(u,freq) );

  return true;
}


bool Parse::parse_int_samples(Ints &intsout)
{
  if( !parse_int_sample(intsout) )
    return false;

  if( test(",") )
    return parse_int_samples(intsout);

  return true;
}

bool Parse::parse_int_predictions(Value **valueout, Ctx **ctxout, Ints &intsout)
{
  unsigned nsamples=0, nvalues=0;
  if( !parse_prediction(valueout,ctxout,&nsamples,&nvalues) )
    return false;

  EXPECT("{");

  if( !parse_int_samples(intsout) )
    return false;
  if( intsout.size() != nvalues )
  {
    error() << "Wrong number of distinct sampled vallues!\n";
    return false;
  }

  EXPECT("}");
  EXPECT(";");

  return true;
}

bool Parse::parse_ptr_predictions(Value **valueout, Ctx **ctxout, Ptrs &ptrsout)
{
  unsigned nsamples=0, nvalues=0;
  if( !parse_prediction(valueout,ctxout,&nsamples,&nvalues) )
    return false;

  EXPECT("{");

  if( !parse_ptrs(ptrsout) )
    return false;
  if( ptrsout.size() != nvalues )
  {
    error() << "Wrong number of distinct sampled values!\n";
    return false;
  }

  EXPECT("}");
  EXPECT(";");

  return true;
}

bool Parse::parse_line(char *buffer)
{
  // Types of message:
  //  COMPLETE ALLOCATION INFO ;
  //  INCOMPLETE ALLOCATION INFO name ;
  //  ESCAPE OBJECT <au> ESCAPES <ctx> COUNT <n> ;
  //  LOCAL OBJECT <au> LOCAL TO <ctx> COUNT <n> ;
  //  PRED INT <name> AT <ctx> AS PREDICTABLE <n> SAMPLES OVER <m> VALUES { <i-values> } ;
  //  PRED PTR <name> AT <ctx> AS PREDICTABLE <n> SAMPLES OVER <m> VALUES { <p-values> } ;
  //  PRED OBJ <name> AT <ctx> AS PREDICTABLE <n> SAMPLES OVER <m> VALUES { <p-values> } ;
  //  PTR RESIDUES <name> AT <ctx> AS RESTRICTED <n> SAMPLES OVER <n> MEMBERS { <r-values> } ;

  //
  // where,
  //  <au> ::= AU UNKNOWN
  //       ::= AU NULL
  //       ::= AU CONSTANT <global-name>
  //       ::= AU GLOBAL <global-name>
  //       ::= AU STACK <val-spec> FROM <ctx>
  //       ::= AU HEAP <val-spec> FROM <ctx>
  // <global-name> ::= global <name>
  // <val-spec> ::= <fcn-name> <bb-name> <inst-name>
  //             ::= <fcn-name> <bb-name> $<offset>
  //             ::= argument <fcn-name> %<offset>
  //  <ctx> ::= CONTEXT { <ctxs> }
  //  <ctxs> ::= <ctx0>
  //         ::= <ctx0> WITHIN <ctxs>
  //  <ctx0> ::= FUNCTION <fcn-name>
  //         ::= LOOP <fcn-name> <header-name> <depth>
  //         ::= TOP
  //  <i-values> ::= <i-value>
  //             ::= <i-value> , <i-values>
  //  <p-values> ::= <p-value>
  //             ::= <p-value> , <p-values>
  //  <r-values> ::= <r-value>
  //             ::= <r-value> , <r-values>
  //  <i-value> ::= ( INT <n> COUNT <m> )
  //  <p-value> ::= ( OFFSET <n> BASE <au> COUNT <m> )
  //  <r-value> ::= 0 | 1 | 2 | ... | 15

  // Tokenize the string
  prev_tokens.clear();
  next_tokens.clear();
  char *state=0;
  if( const char *tok0 = strtok_r(buffer," \t\r\n",&state) )
  {
    next_tokens.push_back(tok0);
    while( const char *toki = strtok_r(0," \t\r\n",&state) )
      next_tokens.push_back(toki);
  }

  // empty lines are ok
  if( next_tokens.empty() )
    return true;

  if( test("COMPLETE") )
    return parse_complete();

  else if( test("INCOMPLETE") )
    return parse_incomplete();

  else if( test("ESCAPE" ) )
    return parse_escape_object();

  else if( test("LOCAL") )
    return parse_local_object();

  else if( test("PTR") )
    return parse_pointer_residues();

  else if( test("PRED") )
  {
    if( test("INT") )
      return parse_predict_int();

    else if( test("PTR") )
      return parse_predict_ptr();

    else if( test("OBJ") )
      return parse_underlying_obj();

    error() << "expected one of INT, PTR, OBJ\n";
    return false;
  }

  error() << "expected one of ESCAPE, LOCAL, PRED, PTR\n";
  return false;
}

Parse::Parse(Module &mod) : module(mod), sema(0)
{
}

void Parse::parse(const char *filename, SemanticAction *s)
{
  sema = s;

  bool goodHeader = false, goodTailer = false;
  unsigned numBadLines = 0;
  unsigned numGoodLines = 0;

  FILE *fin = fopen(filename, "r");
  if( fin )
  {
//    char buffer[1024];
//    buffer[0]=0;
//    if( fgets(buffer,1024,fin) )
    char *buffer = 0;
    size_t n;
    getline(&buffer,&n,fin);
    if( buffer )
    {
      if( strcmp(buffer, "BEGIN SPEC PRIV PROFILE\n") == 0 )
      {
        ++numLines;
        goodHeader = true;

        lineno = 1;
        while( !feof(fin) )
        {
          //buffer[0]=0;
          char *line = 0;
//          if( 0 == fgets(buffer,1024,fin) )
          if( getline(&line,&n,fin) < 1 || line == 0)
            break; //eof


          ++lineno;

          // replace comment character with end-of-line
          if( char *cmt = index(line, '#') )
            *cmt = '\0';

          if( strcmp(line, "END SPEC PRIV PROFILE\n") == 0 )
          {
            // end of profile
            ++numLines;
            goodTailer = true;
            free(line);
            break;
          }

          if( parse_line(line) )
            ++numGoodLines;
          else
            ++numBadLines;

          ++numLines;
          free(line);
        }
      }
      else
      {
        fprintf(stderr, "Bad header: %s\n", buffer);
      }

      free(buffer);
    }

    fclose(fin);
  }

  fprintf(stderr, "SpecPrivProfiler loader: Structure %s/%s, Read %d good lines, %d bad lines\n",
    (goodHeader ? "good" : "bad"), (goodTailer ? "good" : "bad"), numGoodLines, numBadLines);

  sema->sem_set_valid( goodHeader && goodTailer && numBadLines == 0 );

  sema = 0;
}



}
}
