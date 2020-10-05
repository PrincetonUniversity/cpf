#include "llvm/IR/Constants.h"
#include "AAvsOracle.h"

#include "scaf/Utilities/CallSiteFactory.h"

#include <cstdio>

namespace liberty
{
  using namespace llvm;


  char AAvsOracle_EarlyHelper::ID=0;
  char AAvsOracle::ID=0;
  namespace
  {
    static RegisterPass<AAvsOracle_EarlyHelper> X("aa-vs-oracle-early", "Early helper for AA-vs-Oracle");
    static RegisterPass<AAvsOracle> Y("aa-vs-oracle", "Compare ::Alias-Analysis to Oracle");
  }

  bool AAvsOracle_EarlyHelper::gather(Function *oracle, Truths &collection, Module &mod)
  {
    bool modified = false;


    const DataLayout &td = mod.getDataLayout();

    // we modify the use list, so
    // make a tmp copy.
    typedef std::vector<Value*> Values;
    Values tmp(oracle->user_begin(), oracle->user_end() );
    for(Values::iterator i=tmp.begin(), e=tmp.end(); i!=e; ++i)
    {
      CallSite cs = getCallSite(*i);
      if( !cs.getInstruction() )
        continue;

      const DebugLoc &location = cs.getInstruction()->getDebugLoc();
      std::string desc;
      {
        Value *description = cs.getArgument(0);
        if( User *udesc = dyn_cast< User >(description) )
          if( GlobalVariable *gv = dyn_cast<GlobalVariable>( udesc->getOperand(0) ) )
            if( ConstantDataArray  *carr = dyn_cast<ConstantDataArray>( gv->getInitializer() ) )
              desc = carr->getAsString();
      }

      Value *ptr1 = cs.getArgument(1);
      Value *ptr2 = cs.getArgument(2);

      PointerType *pty1 = dyn_cast< PointerType >(ptr1->getType()),
                        *pty2 = dyn_cast< PointerType >(ptr2->getType());

      if( !pty1 )
      {
        fprintf(stderr, "Line %d \"%s\": arg 2 is not a pointer\n", location.getLine(), desc.c_str());
        continue;
      }
      if( !pty2 )
      {
        fprintf(stderr, "Line %d \"%s\": arg 3 is not a pointer\n", location.getLine(), desc.c_str());
        continue;
      }

      const unsigned s1=td.getTypeSizeInBits(pty1->getElementType())/8,
                     s2=td.getTypeSizeInBits(pty2->getElementType())/8;

      collection.push_back( Truth() );
      collection.back().desc = desc;
      collection.back().line = location.getLine();
      collection.back().ptr1 = ptr1;
      collection.back().s1 = s1;
      collection.back().ptr2 = ptr2;
      collection.back().s2 = s2;

      cs.getInstruction()->eraseFromParent();
      modified = true;
    }

    if( oracle->use_empty() )
    {
      oracle->eraseFromParent();
      modified = true;
    }

    return modified;
  }

  bool AAvsOracle_EarlyHelper::runOnModule(Module &mod)
  {
    bool modified = false;

    Function *no_alias_oracle = mod.getFunction("no_alias");
    if( no_alias_oracle )
      modified |= gather(no_alias_oracle, no, mod);

    Function *may_alias_oracle = mod.getFunction("may_alias");
    if( may_alias_oracle )
      modified |= gather(may_alias_oracle, may, mod);

    Function *must_alias_oracle = mod.getFunction("must_alias");
    if( must_alias_oracle )
      modified |= gather(must_alias_oracle, must, mod);

    return modified;
  }


  void AAvsOracle::test(unsigned truth, ATI begin, ATI end, unsigned *stat_row)
  {
    AAResultsWrapperPass &aliasWrap = getAnalysis<AAResultsWrapperPass>();
    AliasAnalysis &alias = aliasWrap.getAAResults();

    for(ATI i=begin; i!=end; ++i)
    {
      switch( alias.alias(i->ptr1, i->s1, i->ptr2, i->s2) )
      {
        case NoAlias:
          ++stat_row[AA_NO];
          if( truth == AA_MAY || truth == AA_MUST )
          {
            fprintf(stderr, "ERROR: Line %d \"%s\": Oracle says May/Must, but AA reports No Alias.\n",
              i->line, i->desc.c_str());
          }
          break;
        case MayAlias:
        case PartialAlias:
          ++stat_row[AA_MAY];
          if( truth != AA_MAY )
          {
            fprintf(stderr, "Imprecise: Line %d \"%s\": Oracle=%d, but AA reports May-Alias.\n",
              i->line, i->desc.c_str(), truth);
          }
          break;
        case MustAlias:
          ++stat_row[AA_MUST];
          if( truth != AA_MUST )
          {
            fprintf(stderr, "Imprecise: Line %d \"%s\": Oracle=%d, but AA reports Must-Alias.\n",
              i->line, i->desc.c_str(), truth);
          }
          break;
      }
    }
  }

  bool AAvsOracle::runOnModule(Module &mod)
  {
    fprintf(stderr, "AA vs Oracle:\n");
    unsigned stats[3][3] = {{0u}};

    AAvsOracle_EarlyHelper &helper = getAnalysis<AAvsOracle_EarlyHelper>();

    // Find our oracle functions:
    test(AA_NO, helper.no_alias_begin(), helper.no_alias_end(), stats[AA_NO]);
    test(AA_MAY, helper.may_alias_begin(), helper.may_alias_end(), stats[AA_MAY]);
    test(AA_MUST, helper.must_alias_begin(), helper.must_alias_end(), stats[AA_MUST]);

    fprintf(stderr, "\n");
    fprintf(stderr, "              AA-No    AA-May    AA-Must\n");
    fprintf(stderr, "Oracle-No  :    %3d       %3d        %3d\n", stats[AA_NO][AA_NO], stats[AA_NO][AA_MAY], stats[AA_NO][AA_MUST]);
    fprintf(stderr, "Oracle-May :    %3d       %3d        %3d\n", stats[AA_MAY][AA_NO], stats[AA_MAY][AA_MAY], stats[AA_MAY][AA_MUST]);
    fprintf(stderr, "Oracle-Must:    %3d       %3d        %3d\n", stats[AA_MUST][AA_NO], stats[AA_MUST][AA_MAY], stats[AA_MUST][AA_MUST]);

    fprintf(stderr, "\n");

    unsigned precise      = stats[AA_NO][AA_NO] + stats[AA_MAY][AA_MAY] + stats[AA_MUST][AA_MUST];
    unsigned conservative = stats[AA_NO][AA_MAY] + stats[AA_NO][AA_MUST] + stats[AA_MAY][AA_MUST] + stats[AA_MUST][AA_MAY];
    unsigned errors       = stats[AA_MAY][AA_NO] + stats[AA_MUST][AA_NO];

    unsigned total = precise + conservative + errors;
    fprintf(stderr, "Precise answers     : %d/%d\t\t%.1f%%\n", precise,      total, 100*precise     /(float)total);
    fprintf(stderr, "Conservative answers: %d/%d\t\t%.1f%%\n", conservative, total, 100*conservative/(float)total);
    fprintf(stderr, "Erroneous answers   : %d/%d\t\t%.1f%%\n", errors,       total, 100*errors      /(float)total);

    return false;
  }
}
