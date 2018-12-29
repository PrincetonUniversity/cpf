#define DEBUG_TYPE "unique-access-paths-aa"

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/FindSource.h"
#include "liberty/Analysis/GetCallers.h"
#include "liberty/Analysis/Introspection.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/QueryCacheing.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/CaptureUtil.h"
#include "liberty/Utilities/FindUnderlyingObjects.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Operator.h"

#include "AnalysisTimeout.h"

#include <set>
#include <ctime>

using namespace llvm;
using namespace liberty;

STATISTIC(traceSteps,    "Num steps performed by trace()");
STATISTIC(numQueries,    "Num queries");
STATISTIC(numHits,       "Num cache hits");
STATISTIC(numEligible,   "Num eligible queries");
STATISTIC(numSubQueries, "Num sub-queries issued");
STATISTIC(numSkipReflexive, "Num reflexive-uapAlias queries");
STATISTIC(longestDefList, "Longest list of definitions");

struct AccessPath : FoldingSetNode
{
  enum PathType
  {
    Global=0,
    Local,
    StructField,
    ArrayElement
  }            type;
  AccessPath  *base;
  const Value *value;
  uint64_t     offset;

  AccessPath(PathType t, const Value *root) : type(t), base(0), value(root), offset(0) {}
  AccessPath(AccessPath *b, PathType t, uint64_t o = 0) : type(t), base(b), value(0), offset(o) {}

  virtual void Profile(FoldingSetNodeID &id) const
  {
    id.AddInteger(type);
    if( type == Global || type == Local )
      id.AddPointer(value);
    else
    {
      id.AddPointer(base);
      if( type == StructField )
        id.AddInteger(offset);
    }
  }

  void print(raw_ostream &out) const
  {
    if( type == Global )
      out << value->getName();
    else if( type == Local)
    {
      out << '{';
      if( const Argument *arg = dyn_cast< Argument >(value) )
        out << arg->getParent()->getName() << ':';
      else if( const Instruction *inst = dyn_cast< Instruction >(value) )
        out << inst->getParent()->getParent()->getName() << ':';
      out << value->getName() << '}';
    }
    else if( type == StructField )
    {
      base->print(out);
      out << ".fld" << offset;
    }
    else if( type == ArrayElement )
    {
      base->print(out);
      out << "[]";
    }
  }
};

raw_ostream &operator<<(raw_ostream &out, const AccessPath &ap)
{
  ap.print(out);
  return out;
}

struct AccessPathAttrs
{
  typedef std::vector<const Value*> Defs;

  AccessPathAttrs() : isCaptured(false), defs() {}

  bool                        isCaptured;
  Defs                        defs;
};

class UniquePathsAA : public ModulePass, public liberty::ClassicLoopAA
{

private:

  typedef FoldingSet< AccessPath > AccessPathSet;
  typedef std::map< const AccessPath *, AccessPathAttrs > AccessPath2Attrs;
  typedef Module::const_global_iterator GlobalIt;
  typedef Value::const_user_iterator UseIt;
  typedef DenseSet<const Value *> ValueSet;
  typedef generic_gep_type_iterator<User::const_op_iterator> GepTyIt;
  typedef DenseMap<PtrPtrKey, AliasResult> Cache;

  // Which functions have we already analyzed?
  ValueSet alreadyAnalyzed;

  // Maintain canonical names for access paths.
  AccessPathSet accessPaths;

  // Every access path is either captured or not.
  AccessPath2Attrs accessPath2Attrs;

  // Speed up expensive queries, and avoids infinite recursions.
  Cache cache;

  void uponStackChange()
  {
    cache.clear();
  }

  AccessPath *Unique(AccessPath *ap)
  {
    AccessPath *unique = accessPaths.GetOrInsertNode(ap);
    if( ap != unique )
      delete ap;
    return unique;
  }

  AccessPath *getGlobalRoot(const Value *v)
  {
    assert( isa< GlobalValue >(v) );
    return Unique( new AccessPath(AccessPath::Global,v) );
  }

  AccessPath *getLocalRoot(const Value *v)
  {
    return Unique( new AccessPath(AccessPath::Local,v) );
  }

  AccessPath *getStructField(AccessPath *b, uint64_t fieldno)
  {
    return Unique( new AccessPath( b, AccessPath::StructField, fieldno ) );
  }

  AccessPath *getArrayElement(AccessPath *b)
  {
    return Unique( new AccessPath( b, AccessPath::ArrayElement ) );
  }

  bool isCaptured(const AccessPath *ap) const
  {
    if( !ap )
      return false;
    AccessPath2Attrs::const_iterator i = accessPath2Attrs.find(ap);
    if( i == accessPath2Attrs.end() )
      return false;
    return i->second.isCaptured;
  }

  bool isTransCaptured(const AccessPath *ap) const
  {
    if( !ap )
      return false;
    return isCaptured(ap) || isTransCaptured(ap->base);
  }

  AccessPath *getGep(AccessPath *ap, const GEPOperator *gep)
  {
    typedef User::const_op_iterator IdxIt;

    IdxIt i = gep->idx_begin(), e = gep->idx_end();
    GepTyIt j = gep_type_begin(gep), z = gep_type_end(gep);
    for(; i!=e && j!=z; ++i, ++j)
    {
      const Value *index = *i;
      //sot: changed due to removal of operator*() from gep_type_iterator
      //Type *type = *j;
      //Type *type = j.getIndexedType();

      //check if index of gep is of struct type
      if (j.isStruct())
      //if( type->isStructTy() )
      {
        const ConstantInt *ci = dyn_cast< ConstantInt >(index);

        if( !ci )
        {
          errs() << *gep << '\n';
          assert(false && "Accessing the i-th field of a structure?");
        }

        ap = getStructField(ap, ci->getLimitedValue() );
      }
      //check if index of gep is sequential
      else if (j.isSequential())
      //else if( type->isArrayTy() || type->isPointerTy() )
      {
        ap = getArrayElement(ap);
      }
      else
      {
        DEBUG( errs() << "Gep is: " << *gep << '\n' );
        assert(false && "Malformed gep?");
      }
    }

    return ap;
  }

  void tracePath(const Value *v, AccessPath *ap, ValueSet &visited)
  {
    if( visited.count(v) )
      return;
    visited.insert(v);
    ++traceSteps;

    AccessPathAttrs &attrs = accessPath2Attrs[ap];
    if( findAllCaptures(v) )
    {
      attrs.isCaptured = true;
      return;
    }

    for(UseIt j=v->user_begin(), z=v->user_end(); j!=z; ++j)
    {
      if( attrs.isCaptured )
        break;

      const Value *use = *j;

      CallSite cs = getCallSite(use);

      if( cs.getInstruction() )
      {
        Function *fcn = cs.getCalledFunction();

        if( !fcn )
        {
          DEBUG(errs() << "Passed to indirect call: " << *use << '\n');
          attrs.isCaptured = true;
          break;
        }

        else if( !fcn->isDeclaration() )
        {
          Function::arg_iterator j=fcn->arg_begin(), z=fcn->arg_end();

          for(CallSite::arg_iterator i=cs.arg_begin(), e=cs.arg_end(); i!=e; ++i, ++j)
          {
            if( j == z )
            {
              // In the case of variadic functions, there may be
              // more callsite args than function args.
              DEBUG(errs() << "Passed as variadic argument: " << *use << '\n');
              attrs.isCaptured = true;
              break;
            }

            if( v == *i )
              tracePath(&*j, ap, visited);
          }
        }

        else
        {
          DEBUG(errs() << "Passed to non-capturing declaration: " << *use << '\n');
        }
      }

      else if( const StoreInst *store = dyn_cast< StoreInst >(use) )
      {
        const Value *def = store->getValueOperand();
        if( std::find( attrs.defs.begin(), attrs.defs.end(), def ) == attrs.defs.end() )
        {
          attrs.defs.push_back( def );

          const unsigned len = attrs.defs.size();
          if( len > longestDefList )
            longestDefList = len;
        }
      }

      else if( const GEPOperator *gep = dyn_cast< GEPOperator >(use) )
      {
        AccessPath *gepap = getGep(ap, gep);
        tracePath(gep, gepap, visited);
      }

      else if( isa< LoadInst >(use) )
      {
        // safe to ignore
      }

      else
      {
        DEBUG(errs() << "Unknown use: " << *use << '\n');
      }
    }
  }

  void analyzeFunction(const Function *f)
  {
    if( alreadyAnalyzed.count(f) )
      return;
    alreadyAnalyzed.insert(f);

    DEBUG(errs() << "Analyzing " << f->getName() << "\n");

    ValueSet visited;
    for(const_inst_iterator i=inst_begin(f), e=inst_end(f); i!=e; ++i)
    {
      const Instruction *inst = &*i;

      if( isNoAliasCall(inst) || isa<AllocaInst>(inst) )
      {
        AccessPath *ap = getLocalRoot(inst);
        visited.clear();
        tracePath(inst,ap,visited);
      }
    }

    DEBUG(
      for(const_inst_iterator i=inst_begin(f), e=inst_end(f); i!=e; ++i)
      {
        const Instruction *inst = &*i;

        if( isNoAliasCall(inst) || isa<AllocaInst>(inst) )
        {
          AccessPath *ap = getLocalRoot(inst);

          errs() << *ap << ": ";

          if( isTransCaptured(ap) )
            errs() << "Captured\n";
          else
          {
            AccessPathAttrs &attrs = accessPath2Attrs[ap];
            errs() << "Not captured";
            if( attrs.defs.empty() )
              errs() << '\n';
            else
            {
              errs() << "; defs are:\n";
              for(AccessPathAttrs::Defs::iterator j=attrs.defs.begin(), z=attrs.defs.end(); j!=z; ++j)
                errs() << "\to " << **j << '\n';
            }
          }
        }
      }
    );

  }

  void analyzeParent(const Value *v)
  {
    if( const Instruction *inst = dyn_cast< Instruction >(v) )
      analyzeFunction( inst->getParent()->getParent() );
    else if( const Argument *arg = dyn_cast< Argument >(v) )
      analyzeFunction( arg->getParent() );
  }


  std::set<const Value *> valList;
  // This is like tracePath(), only backwards from uses
  // instead of forward from roots.
  AccessPath *findPath(const Value *v)
  {
    if( const GEPOperator *gep = dyn_cast< GEPOperator >(v) )
    {
      AccessPath *b = findPath( gep->getPointerOperand() );
      if( !b )
        return 0;
      return getGep(b, gep);
    }
    else if( isa< GlobalValue >(v) )
      return getGlobalRoot(v);
    else if( isNoAliasCall(v) || isa< AllocaInst >(v) )
      return getLocalRoot(v);
    else if( const Argument *arg = dyn_cast< Argument >(v) )
    {
      // Try to find sources of this argument.
      const Function *callee = arg->getParent();
      CallSiteList callers;
      if( !getCallers(callee, callers) )
        return 0; // partial list

      AccessPath *uniqueActualPath = 0;
      for(CallSiteList::const_iterator i=callers.begin(), e=callers.end(); i!=e; ++i)
      {
        const CallSite &callsite = *i;
        const Function *caller = callsite.getInstruction()->getParent()->getParent();

        analyzeFunction(caller);
        const Value *actual = callsite.getArgument( arg->getArgNo() );
        if( actual == arg )
          continue;


        AccessPath *actualPath;
        if(valList.count(actual) == 0)
        {
          valList.insert(actual);
          actualPath = findPath( actual );
          valList.erase(actual);
        }
        else
        {
          return 0;
        }

        if( !actualPath )
          return 0; // no access path found for actual parameter.
        else if( !uniqueActualPath )
          uniqueActualPath = actualPath;
        else if( uniqueActualPath != actualPath )
          return 0; // not unique
      }

      return uniqueActualPath;
    }
    return 0;
  }


  AccessPath *findPathForLoad(const Value *v)
  {
    if( const LoadInst *load = dyn_cast< LoadInst >(v) )
      return findPath( load->getPointerOperand() );
    else
      return 0;
  }

  bool eligible(const AccessPath *ap) const
  {
    if( !ap )
      return false;
    if( isTransCaptured(ap) )
      return false;

    AccessPath2Attrs::const_iterator i = accessPath2Attrs.find(ap);
    if( i == accessPath2Attrs.end() )
      return false;

    if( i->second.defs.empty() )
      return false;

    return true;
  }

  static AliasResult join(AliasResult a, AliasResult b)
  {
    if( a == NoAlias )
      return b;
    else if( b == NoAlias )
      return a;
    else if( a == b )
      return a;
    else
      return MayAlias;
  }

public:
  static char ID;

  Module* Mod;

  UniquePathsAA() : ModulePass(ID), ClassicLoopAA() {}

  ~UniquePathsAA()
  {
    // Delete all of the folded paths
    std::vector<AccessPath*> aps;
    for(AccessPathSet::iterator i=accessPaths.begin(), e=accessPaths.end(); i!=e; ++i)
      aps.push_back(&*i);
    accessPaths.clear();
    for(unsigned i=0; i<aps.size(); ++i)
      delete aps[i];
  }

  virtual bool runOnModule(Module &M)
  {
    const DataLayout &DL = M.getDataLayout();
    Mod = &M;
    InitializeLoopAA(this, DL);

    ValueSet visited;
    for(GlobalIt i=M.global_begin(), e=M.global_end(); i!=e; ++i)
    {
      const GlobalVariable *gv = &*i;
      if( gv->isConstant() )
        continue;

      AccessPath *ap = getGlobalRoot( gv );

      if( FULL_UNIVERSAL || gv->hasLocalLinkage() )
        tracePath(gv, ap, visited);
      else
        accessPath2Attrs[ap].isCaptured = true;
    }

    DEBUG(
    for(AccessPathSet::iterator i=accessPaths.begin(), e=accessPaths.end(); i!=e; ++i)
    {
      const AccessPath *ap = &*i;

      errs() << *ap << ": ";

      if( isTransCaptured(ap) )
        errs() << "Captured\n";
      else
      {
        AccessPathAttrs &attrs = accessPath2Attrs[ap];
        errs() << "Not captured";
        if( attrs.defs.empty() )
          errs() << '\n';
        else
        {
          errs() << "; defs are:\n";
          for(AccessPathAttrs::Defs::iterator j=attrs.defs.begin(), z=attrs.defs.end(); j!=z; ++j)
            errs() << "\to " << **j << '\n';
        }
      }
    }
    );

    return false;
  }

  // Move-to-front.
  // Moves elements [0,i) to [1,i+1)
  // and moves the element at i to position 0.
  void mtf(AccessPathAttrs::Defs &set, AccessPathAttrs::Defs::iterator i) const
  {
    // On 470.lbm, the MTF optimization reduces the number of subqueries from
    // 7363 to 6362 on a run of nospec-pipeline with 1 late-inline round.
    // On other benchmarks, like 433.milc, no improvement.
    AccessPathAttrs::Defs::iterator b = set.begin();
    if( i != b )
    {
      const Value *tmp = *i;
      AccessPathAttrs::Defs::iterator out = i+1;
      std::copy_backward( b, i, out );
      *b = tmp;
    }
  }

  AliasResult uapAlias(const Pointer &P1, const Value *obj1, TemporalRelation Rel, const Pointer &P2, const Value *obj2, const Loop *L, time_t queryStart, unsigned Timeout)
  {
    LoopAA *top = getTopAA();

    AccessPath *ap1 = findPathForLoad(obj1);
    AccessPath *ap2 = findPathForLoad(obj2);

    if( eligible(ap1) && eligible(ap2) )
    {
      ++numEligible;

      // Very common case: two loads from the same unique-access-path
      // (in 470.lbm, this is 72% of eligible queries...)
      if( ap1 == ap2 )
      {
        DEBUG(errs() << "UAP:\n"
                     << " Pointers " << *P1.ptr << '\n'
                     << "      and " << *P2.ptr << '\n'
                     << " share the same access path " << *ap1 << '\n'
                     << " ==> MayAlias.\n");
        ++numSkipReflexive;

        if( accessPath2Attrs[ap1].defs.size() == 1 )
          return MustAlias;
        else
          return MayAlias;
      }

      AliasResult result = NoAlias;

      // we have a finite set of defs for P1 and P2
      AccessPathAttrs::Defs &defs1 = accessPath2Attrs[ap1].defs,
                            &defs2 = accessPath2Attrs[ap2].defs;

      // Ensure that defs1 and defs2 are refer to physically-distinct collections.
      assert( &defs1 != &defs2
      && "Two distinct access paths physically share a collection of defs.");

      DEBUG(errs() << *ap1 << " has " << defs1.size() << '\n');
      for(AccessPathAttrs::Defs::iterator i=defs1.begin(), e=defs1.end(); i!=e; ++i)
      {
        const Value *def1 = *i;
        DEBUG(errs() << '\t' << *def1 << '\n');

        if( isa< ConstantPointerNull >(def1)
        ||  isa< ConstantInt >(def1) )
        {
          DEBUG(errs() << "\t\tSkip.\n");
          continue;
        }

        DEBUG(errs() << "\t\t" << *ap2 << " has " << defs2.size() << '\n');
        const unsigned size1 = ~0U; // TODO
        for(AccessPathAttrs::Defs::iterator j=defs2.begin(), z=defs2.end(); j!=z; ++j)
        {
          const Value *def2 = *j;
          DEBUG(errs() << "\t\t\t" << *def2 << '\n');

          if( isa< ConstantPointerNull >(def2)
          ||  isa< ConstantInt >(def2) )
          {
            DEBUG(errs() << "\t\tSkip.\n");
            continue;
          }

          const unsigned size2 = ~0U; // TODO

          if( def1 == P1.ptr )
          {
            mtf(defs1, i);
            return MayAlias;
          }

          if( def2 == P2.ptr )
          {
            mtf(defs2, j);
            return MayAlias;
          }

          ++numSubQueries;
          const AliasResult myresult = top->alias(def1, size1, Rel, def2, size2, L);

          if( myresult != NoAlias )
          {
            // Move-to-front, so that subsequent calls to uapAlias will
            // try these definitions earlier.  Ideally, this will lead
            // to faster may/must alias results.
            //
            // Note that mtf() does not invalidate any iterators.
            //
            // It's safe to rearrange the collection: the loop invariant
            // is that every element at or before i, j, has been tested.
            // This is easily shown so long as defs1, defs2 are distinct.
            mtf(defs1, i);
            mtf(defs2, j);
          }

          result = join(result, myresult);

          if( result == MayAlias )
          {
            DEBUG(errs() << "\t\t\t\thit bottom\n");
            return MayAlias;
          }

          if(Timeout > 0 && queryStart > 0)
          {
            time_t now;
            time(&now);
            if( (now - queryStart) > Timeout )
            {
              errs() << "UniquePaths Timeout\n";
              return MayAlias;
            }
          }
        }
      }

      DEBUG(errs() << "\n\tResult: " << result << '\n');
      return result;
    }

    else if( eligible(ap1) )
    {
      ++numEligible;
      AliasResult result = NoAlias;

      // we have a finite set of defs for P1
      AccessPathAttrs::Defs &defs1 = accessPath2Attrs[ap1].defs;
      DEBUG(errs() << *ap1 << " has " << defs1.size() <<  '\n');
      for(AccessPathAttrs::Defs::iterator i=defs1.begin(), e=defs1.end(); i!=e; ++i)
      {
        const Value *def1 = *i;
        DEBUG(errs() << '\t' << *def1 << '\n');
        const unsigned size1 = ~0U; // TODO

        DEBUG(errs() << "\t\tvs " << *P2.ptr << '\n');

        if( def1 == P1.ptr )
        {
          mtf(defs1, i);
          return MayAlias;
        }

        ++numSubQueries;
        const AliasResult myresult = top->alias(def1,size1,Rel,P2.ptr,P2.size,L);

        if( myresult != NoAlias )
        {
          // Move-to-front for faster failures on subsequent queries.
          // Does not invalidate iterator i.
          mtf(defs1,i);
        }

        result = join(result, myresult);

        if( result == MayAlias )
        {
          DEBUG(errs() << "\t\t\thit bottom\n");
          return MayAlias;
        }

        if(Timeout > 0 && queryStart > 0)
        {
          time_t now;
          time(&now);
          if( (now - queryStart) > Timeout )
          {
            errs() << "UniquePaths Timeout\n";
            return MayAlias;
          }
        }
      }

      DEBUG(errs() << "\n\tResult: " << result << '\n');
      return result;
    }

    else if( eligible(ap2) )
    {
      ++numEligible;
      AliasResult result = NoAlias;

      // we have a finite set of defs for P2
      AccessPathAttrs::Defs &defs2 = accessPath2Attrs[ap2].defs;
      DEBUG(errs() << *ap2 << " has " << defs2.size() << '\n');
      for(AccessPathAttrs::Defs::iterator i=defs2.begin(), e=defs2.end(); i!=e; ++i)
      {
        const Value *def2 = *i;
        DEBUG(errs() << '\t' << *def2 << '\n');
        const unsigned size2 = ~0U; // TODO

        DEBUG(errs() << "\t\tvs " << *P1.ptr << '\n');

        if( def2 == P2.ptr )
        {
          mtf(defs2, i);
          return MayAlias;
        }

        ++numSubQueries;
        const AliasResult myresult = top->alias(P1.ptr,P1.size,Rel,def2,size2,L);

        if( myresult != NoAlias )
        {
          // Move-to-front.  Does not invalidate iterator i.
          mtf(defs2,i);
        }

        result = join(result, myresult);

        if( result == MayAlias )
        {
          DEBUG(errs() << "\t\t\thit bottom\n");
          return MayAlias;
        }

        if(Timeout > 0 && queryStart > 0)
        {
          time_t now;
          time(&now);
          if( (now - queryStart) > Timeout )
          {
            errs() << "UniquePaths Timeout\n";
            return MayAlias;
          }
        }
      }

      DEBUG(errs() << "\n\tResult: " << result << '\n');
      return result;
    }

    return MayAlias;
  }

  virtual AliasResult aliasCheck(const Pointer &P1,
                                 TemporalRelation Rel,
                                 const Pointer &P2,
                                 const Loop *L)
  {
    INTROSPECT(ENTER(P1,Rel,P2,L));
    ++numQueries;

    PtrPtrKey key(P1,Rel,P2,L);
    if( Rel == After )
      key = PtrPtrKey(P2,Before,P1,L);

    if( cache.count(key) )
    {
      ++numHits;
      AliasResult result = cache[key];
      INTROSPECT(EXIT(P1,Rel,P2,L, result));
      return result;
    }

    time_t queryStart=0;
    if( AnalysisTimeout > 0 )
      time(&queryStart);

    // Temporarily pessimize this query
    // to avoid infinite recursion.
    // this will be fixed before we return.
    cache[key] = MayAlias;

    analyzeParent( P1.ptr );
    analyzeParent( P2.ptr );

    AliasResult result = NoAlias;

    //sot
    const DataLayout &DL = Mod->getDataLayout();

    UO underlying1, underlying2;
    GetUnderlyingObjects(P1.ptr, underlying1, DL);
    GetUnderlyingObjects(P2.ptr, underlying2, DL);

    for(UO::const_iterator i=underlying1.begin(), e=underlying1.end(); i!=e; ++i)
    {
      if( result == MayAlias )
        break;

      const Value *obj1 = *i;

      for(UO::const_iterator j=underlying2.begin(), z=underlying2.end(); j!=z; ++j)
      {
        if( result == MayAlias )
          break;

        const Value *obj2 = *j;

        result = join(result, uapAlias(P1,obj1,Rel,P2,obj2,L,queryStart,AnalysisTimeout));

        if(AnalysisTimeout > 0 && queryStart > 0)
        {
          time_t now;
          time(&now);
          if( (now - queryStart) > AnalysisTimeout )
          {
            errs() << "UniquePaths Timeout\n";
            result = MayAlias;
            break;
          }
        }

      }
    }

    // Since we map pointers to their underlying objects,
    // we cannot report MUST alias unless the underlying
    // objects are precisely the pointers themselves.
    if( result == MustAlias )
    {
      if( underlying1.size() != 1 )
        result = MayAlias;
      else if( *underlying1.begin() != P1.ptr )
        result = MayAlias;
      else if( underlying2.size() != 1 )
        result = MayAlias;
      else if( *underlying2.begin() != P2.ptr )
        result = MayAlias;
    }

    INTROSPECT(EXIT(P1,Rel,P2,L, result));
    cache[key] = result; // fix the cache.
    queryStart = 0;
    return result;
  }

  StringRef getLoopAAName() const
  {
    return "unique-access-paths-aa";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const
  {
    LoopAA::getAnalysisUsage(AU);
    AU.setPreservesAll();        // Does not transform code
  }

  /// getAdjustedAnalysisPointer - This method is used when a pass implements
  /// an analysis interface through multiple inheritance.  If needed, it
  /// should override this to adjust the this pointer as needed for the
  /// specified pass info.
  virtual void *getAdjustedAnalysisPointer(AnalysisID PI)
  {
    if (PI == &LoopAA::ID)
      return (LoopAA*)this;
    return this;
  }
};

char UniquePathsAA::ID = 0;

static RegisterPass<UniquePathsAA>
X("unique-access-paths-aa", "Alias analysis for memory objects with unique access paths", false, true);
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

