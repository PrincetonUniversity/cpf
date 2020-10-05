#define DEBUG_TYPE "profile"

#include "scaf/Utilities/CallSiteFactory.h"
#include "liberty/Speculation/UpdateOnCloneAdaptors.h"

namespace liberty
{
namespace SpecPriv
{

// Given a type T,
//   If T is of the form ``U *'', then return class U
//   Otherwise, return class T.
// (I love evil C++)
template <class T> struct StripPointer     { typedef T BaseType; };
template <class U> struct StripPointer<U*> { typedef U BaseType; };

// Interrogate a const ValueToValueMapTy: the most inconvenient of all llvm data structures...
template <class VALUE>
static bool maybe_map(VALUE &v, const ValueToValueMapTy &vmap)
{
  ValueToValueMapTy::const_iterator i = vmap.find(v);
  if( i == vmap.end() )
    return false;

  v = cast< typename StripPointer<VALUE>::BaseType >( &*( i->second ) );
  return true;
}

//sot remove this sanity check for now
/*
void UpdateEdgeLoopProfilers::sanity(StringRef time, const Function *fcn) const
{
  const double Epsilon = 1.e-6;
  for(Function::const_iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
  {
    const BasicBlock *bb = &*i;

    bool has_one_known_in_edge = false;
    double sum_in_edges = 0.0;
    for(const_pred_iterator j=pred_begin(bb), z=pred_end(bb); j!=z; ++j)
    {
      const BasicBlock *pred = *j;

      ProfileInfo::Edge inedge(pred,bb);
      double in_weight = edges.getEdgeWeight(inedge);
      // did not change for NOW sot // TODO. remove this sanity check
      //auto in_prob = edges.getEdgeProbability(pred,bb);
      //double in_weight = prob.getNumerator() / (double) prob.getDenominator();

      if( in_weight != ProfileInfo::MissingValue )
      {
        has_one_known_in_edge = true;
        sum_in_edges += in_weight;
      }
    }

    bool has_calls_that_dont_return_once = false;
    for(BasicBlock::const_iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      const Instruction *inst = &*j;
      if( isa<CallInst>(inst) || isa<InvokeInst>(inst) )
      {
        has_calls_that_dont_return_once = true;
        break;
      }
    }

    bool has_unknown_out_edge = false;
    double sum_out_edges = 0.0;
    // sigh: 'const_pred_iterator,' yet 'succ_const_iterator'
    for(succ_const_iterator j=succ_begin(bb), z=succ_end(bb); j!=z; ++j)
    {
      const BasicBlock *succ = *j;

      ProfileInfo::Edge outedge(bb,succ);
      double out_weight = edges.getEdgeWeight(outedge);

      if( out_weight == ProfileInfo::MissingValue )
        has_unknown_out_edge = true;

      else
        sum_out_edges += out_weight;
    }

    const double block_weight = edges.getExecutionCount(bb);
    if( block_weight != ProfileInfo::MissingValue )
    {
      const double in_error = block_weight - sum_in_edges;
      const double out_error = block_weight - sum_out_edges;

      if( in_error > Epsilon || -in_error > Epsilon )
      {
        if( has_one_known_in_edge )
        {
          errs() << time << " in function " << fcn->getName() << ":\n"
                 << "Block " << bb->getName() << " has block weight " << block_weight << '\n'
                 << "BUT has in-weight " << sum_in_edges << "\n\n";
        }
      }

      if( out_error > Epsilon || -out_error > Epsilon )
      {
        if( !has_calls_that_dont_return_once )
        {
          errs() << time << " in function " << fcn->getName() << ":\n"
                 << "Block " << bb->getName() << " has in-weight " << sum_in_edges << '\n'
                 << "BUT has out-weight " << sum_out_edges << '\n'
                 << "and LACKS any calls that might explain the difference.\n\n";
        }
      }

      if( has_unknown_out_edge )
      {
          errs() << time << " in function " << fcn->getName() << ":\n"
                 << "Block " << bb->getName() << " has block weight " << block_weight << '\n'
                 << "BUT has an out edge with unknown weight\n\n";
      }
    }

    else // block weight unknown
    {
      if( has_one_known_in_edge )
      {
        errs() << time << " in function " << fcn->getName() << ":\n"
               << "Block " << bb->getName() << " has unknown block weight\n"
               << "BUT has in-weight " << sum_in_edges << '\n'
               << "=> so it should know the block weight.\n\n";
      }
    }

    const double error = sum_in_edges - sum_out_edges;
    if( error > Epsilon || -error > Epsilon )
    {
      if( !has_calls_that_dont_return_once )
      {
        if( bb != &fcn->getEntryBlock() )
        {
          errs() << time << " in function " << fcn->getName() << ":\n"
                 << "Block " << bb->getName() << " has in-weight " << sum_in_edges << '\n'
                 << "BUT has out-weight " << sum_out_edges << '\n'
                 << "and LACKS any calls that might explain the difference.\n\n";
        }
      }
    }
  }
}
*/

// previous Profile Info in LLVM 2.8 changed the executionCount in blocks and
// the edgeWeight(which was also like a count); absolute values were changed.
// Now we can only change frequencies of blocks relative to entry blocks and
// probability of edges.
// We can just copy the relative probabilities and frequencies from the old function
// to the inlined one and just change the entry function count

void UpdateEdgeLoopProfilers::resetAfterInline(
  Instruction *callsite_no_longer_exists,
  Function *caller,
  Function *callee,
  const ValueToValueMapTy &vmap,
  const CallsPromotedToInvoke &call2invoke)
{
  errs() << "  . . - UpdateEdgeLoopProfilers::resetAfterInline: " << callee->getName() << '\n';
  // sot :remove sanity check for now
  //LLVM_DEBUG(sanity("before", callee));

  //sot update because new profile passes are Function passes and not ModulePass as was the case for ProfileInfo

  Function *old_fcn = callee;
  const BasicBlock *old_entry = &old_fcn->getEntryBlock();

  const BasicBlock *new_entry = old_entry;
  assert( maybe_map(new_entry, vmap) && "Callee's entry block not in vmap");

  const BasicBlock *beforeCall = new_entry->getSinglePredecessor();
  assert( beforeCall
  && "Inlined function should have ONE predecessor block");
  const Function *caller_fcn = caller;

  // Determine the scaling factor.
  //sot
  //const double total_count = edges.getExecutionCount( old_entry );
  //assert( total_count != ProfileInfo::MissingValue && total_count > 0 );

  // need to run multiple times since every call to getAnalysis for a pass overwrites the last call
  BlockFrequencyInfo& bfiCallee = proxy.getAnalysis< BlockFrequencyInfoWrapperPass >(*callee).getBFI();
  auto bbcnt = bfiCallee.getBlockProfileCount(old_entry);
  assert (bbcnt.hasValue() && bbcnt.getValue() > 0);
  auto bbcntV = bbcnt.getValue();
  BlockFrequencyInfo& bfiCaller = proxy.getAnalysis< BlockFrequencyInfoWrapperPass >(*caller).getBFI();
  const double site_count  = std::min(
    //edges.getExecutionCount( beforeCall ),
    bfiCaller.getBlockProfileCount( beforeCall ).getValue(),
    bbcntV);
    //total_count );
                    // maybe you're wondering why a callsite to 'callee' could
                   // ever execute more times than 'callee' is invoked.
                   // Yeah.  Me too.

  const double site_scale = site_count / (bbcnt.getValue() * 1.0);
  const double other_scale = 1.0 - site_scale;

  errs() << "This callsite represents "
         << (5+(unsigned)(1000.0 * site_scale))/10
         << "% of all invocations of " << old_fcn->getName() << '\n';



  // sot: need to change the entry count of the old_function, remove the counts correspoding to the inlined function
  // for the inlined function just need to copy over the freqs for the basic blocks
  // and the probabilities for the edges from the old_function
  // new_old_fn_entry_count is bbcnt - site_count
  uint64_t new_old_fn_entry_count = old_fcn->getEntryCount().getCount() * other_scale;
  old_fcn->setEntryCount( new_old_fn_entry_count );


  // Divide the edge counts between the inlined copy and the new copy.
  for(Function::const_iterator i=old_fcn->begin(), e=old_fcn->end(); i!=e; ++i)
  {
    const BasicBlock *old_bb = &*i;

    const BasicBlock *new_bb = old_bb;
    assert( maybe_map(new_bb,vmap)
    && "Can't find image of block after inlining");

    assert(old_bb->getParent() == old_fcn);
    assert(new_bb->getParent() == caller_fcn);

    const Instruction *old_term = old_bb->getTerminator(),
                         *new_term = new_bb->getTerminator();

    if( const InvokeInst *invoke = dyn_cast<InvokeInst>( new_term ) )
    {
      if( std::find(call2invoke.begin(), call2invoke.end(), invoke) != call2invoke.end() )
      {
        errs() << "This is because a call in " << invoke->getParent()->getName() << '\n'
               << "was promoted to " << *invoke << '\n';
      }

      assert(false && "TODO: implement this");
    }

    if( old_term->getNumSuccessors() > 0 )
    {
      assert( old_term->getNumSuccessors() == new_term->getNumSuccessors()
      && "Inlining changed number of successors!");

      //just copy over all the edge probabilities of function to the inlined version
      for(unsigned j=0, SN=old_term->getNumSuccessors(); j<SN; ++j)
      {
        //sot
        //const ProfileInfo::Edge old_edge( old_bb, old_term->getSuccessor(j) );
        //const double edge_weight = edges.getEdgeWeight( old_edge );
        BranchProbabilityInfo& bpiCallee = proxy.getAnalysis< BranchProbabilityInfoWrapperPass >(*callee).getBPI();
        auto old_prob = bpiCallee.getEdgeProbability(old_bb, old_term->getSuccessor(j));
        //const double edge_weight = old_prob.getNumerator() / (double) old_prob.getDenominator();

        // Distribute control-flow edge weight equitably
        //const ProfileInfo::Edge new_edge( new_bb, new_term->getSuccessor(j) );
        //auto new_prob = bpi.getEdgeProbability(new_bb, new_term->getSuccessor(j));

        //if( edge_weight >= 0.0 )
        {
          //sot
          //edges.setEdgeWeight( old_edge, other_scale * edge_weight );
          //edges.setEdgeWeight( new_edge,  site_scale * edge_weight );

          // copy the probability of each old_bb edge to the new_bb edge
          // the total count will get adjusted by adjusting the entry_count

          //assumes that nonone uses the numerator as absolute weights
          //specified as internal weights that can be scaled
          BranchProbabilityInfo& bpiCaller = proxy.getAnalysis< BranchProbabilityInfoWrapperPass >(*caller).getBPI();
          bpiCaller.setEdgeProbability(new_bb, j, old_prob);
        }
      }
    }

    // Distribute block execution weight equitably
    //sot
    //const double block_count = edges.getExecutionCount(old_bb);

    //no need to change the count of every block. Just copy the freqs of
    // old_bb to new_bb. Only the entry_count of the old_fn needs to adjusted

    //const double block_count = bfi.getBlockProfileCount(old_bb).getValue();

    // sot: copy over all the block frequencies as well
    BlockFrequencyInfo& bfiCallee = proxy.getAnalysis< BlockFrequencyInfoWrapperPass >(*callee).getBFI();
    auto freq_old_bb = bfiCallee.getBlockFreq(old_bb).getFrequency();

    BlockFrequencyInfo& bfiCaller = proxy.getAnalysis< BlockFrequencyInfoWrapperPass >(*caller).getBFI();
    bfiCaller.setBlockFreq(new_bb, freq_old_bb);

    //if( block_count >= 0.0 )
    //{
      //sot
      //edges.setExecutionCount(old_bb, other_scale * block_count);
      //edges.setExecutionCount(new_bb,  site_scale * block_count);
   //}
   // */

  }


  // Clean-up call/return effects
  {
    //if( site_count >= 0.0 )
    //{
      //  - weight of the edge from the 'call' block to the 'entry' block
      //sot
      //edges.setEdgeWeight( ProfileInfo::Edge(beforeCall,new_entry), site_count );

      const BasicBlock *beforeCallSucc = beforeCall->getSingleSuccessor();
      assert( beforeCallSucc && beforeCallSucc==new_entry
      && "beforeCall block should have ONE successor block, the entry block of the inlined function");

      //set probability to 1 for the edge between beforeCall and new_entry

      BranchProbabilityInfo& bpiCaller = proxy.getAnalysis< BranchProbabilityInfoWrapperPass >(*caller).getBPI();
      bpiCaller.setEdgeProbability(beforeCall, 0, BranchProbability::getOne());


      //  - weight of the inlined 'entry' block
      //edges.setExecutionCount( new_entry, site_count );

      //Nothing to do here. the new_entry has already the correct count from its predecessor(part of an existing function)

    //}

    //  - weight of the edge(s) from each inline 'return' block to the 'AfterCallBlock'
    const BasicBlock *afterCallBlock = 0;
    //double sumReturnWeights = 0.0;
    //uint64_t sumReturnWeights = 0;
    for(Function::const_iterator i=old_fcn->begin(), e=old_fcn->end(); i!=e; ++i)
    {
      const BasicBlock *old_bb = &*i;
      const Instruction *old_term = old_bb->getTerminator();
      if( ! isa<ReturnInst>(old_term) )
        continue;

      const BasicBlock *new_bb = old_bb;
      assert( maybe_map(new_bb,vmap)
      && "Can't find the image of a return block");

      const Instruction *new_term = new_bb->getTerminator();
      assert( new_term->getNumSuccessors() == 1
      && "Terminator in image of return block should have a single successor");

      const BasicBlock *uniqueSuccessor = new_term->getSuccessor(0);
      if( 0 == afterCallBlock )
        afterCallBlock = uniqueSuccessor;
      else
        assert( afterCallBlock == uniqueSuccessor
        && "Terminators from images of all returns blocks should branch to the same afterCallBlock");

      // Update the weight of return->afterCallBlock
      //sot
      //const double return_weight = edges.getExecutionCount(new_bb);

      //if( return_weight >= 0 )
      {
        //this implies that everything will go through  the edge between new_bb and afterCallBlock
        //new_bb end with a ret inst so that makes sense
        //edges.setEdgeWeight( ProfileInfo::Edge(new_bb,afterCallBlock), return_weight );

        //sot
        //find which successor of beforeCall is new_entry and set the prob of their edge
        //based on http://llvm.org/doxygen/BranchProbabilityInfo_8cpp_source.html#l00889
        // (implementation of BranchProbabilityInfo::getEdgeProbability for two BB)
        // because a returnInst could have multiple successors for some reason
        for (succ_const_iterator I = succ_begin(new_bb), E = succ_end(new_bb); I != E; ++I)
        {
          if (*I == afterCallBlock)
          {
            bpiCaller.setEdgeProbability(new_bb, I.getSuccessorIndex(), BranchProbability::getOne());
            break;
          }
        }
        //sumReturnWeights += return_weight;
      }
    }


    //  - weight of the 'AfterCallBlock'
    //sot
    //I don't think this block count should change. How do you know if afterCallBlock hasn't other predecessor other than the returns from inlined function
    // the frequency of the AfterCallBlock should probably be the same as before the inlining
    //edges.setExecutionCount(afterCallBlock,sumReturnWeights);
  }

  // Divide the loop execution times between the inlined copy and the new copy.
  // Must update:
  //  - execution weight of the inlined function,
  const unsigned long fcn_time = times.getFunctionTime(old_fcn);
  times.setFunctionTime(old_fcn, (unsigned long) (other_scale * fcn_time + 0.5) );

  //  - the execution weight of all loops in the inlined function
  for(Function::const_iterator i=old_fcn->begin(), e=old_fcn->end(); i!=e; ++i)
  {
    const BasicBlock *old_header = &*i;
    const unsigned long old_time = times.getLoopTime(old_header);
    if( old_time != 0 )
    {
      const BasicBlock *new_header = old_header;
      assert( maybe_map(new_header,vmap) && "Cannot find image of loop header in vmap");

      times.addLoop(new_header);

      times.setLoopTime( old_header, (unsigned long) (other_scale * old_time) );
      times.setLoopTime( new_header, (unsigned long) ( site_scale * old_time) );
    }

    // And any callsite found in this function
    for(BasicBlock::const_iterator k=old_header->begin(), K=old_header->end(); k!=K; ++k)
    {
      const Instruction *old_inst = &*k;
      CallSite cs = getCallSite(old_inst);
      if( !cs.getInstruction() )
        continue;

      const unsigned long old_cs_time = times.getCallSiteTime(old_inst);

      const Instruction *new_inst = old_inst;
      assert( maybe_map(new_inst,vmap) && "Cannot find image of call site in vmap");

      times.addCallSite(new_inst);

      times.setCallSiteTime(old_inst, (unsigned long) (other_scale * old_cs_time) );
      times.setCallSiteTime(new_inst, (unsigned long) ( site_scale * old_cs_time) );
    }
  }

  // sot :remove sanity check for now
  //LLVM_DEBUG(sanity("after", callee));
}


void UpdateLAMP::resetAfterInline(
  Instruction *callsite_no_longer_exists,
  Function *caller,
  Function *callee,
  const ValueToValueMapTy &vmap,
  const CallsPromotedToInvoke &call2invoke)
{
  errs() << "  . . - UpdateLAMP::resetAfterInline: " << callee->getName() << '\n';
  // LAMP loader is ugly code.  It has these data fields:
  //
  //  IdToInstMap: inst-id -> inst
  //  InstToIdMap: inst -> inst-id
  //    bijection between instruction IDs and instructions.
  //    only contains: Loads, Stores, MemIntrinsics and calls to external libraries.
  //
  //  biimap: (header:bb x i1:inst x i2:inst x lc:bool) -> double
  //    Observed probability of a (lc ? 'loop-carried' : 'intra-iteration')
  //    dependence from i2 to i1 in the loop specified by header.
  //
  //  DepToCountMap: (header:bb x i1:inst x i2:inst x lc:bool) -> int
  //    Number of times that we observed a (lc ? 'loop-carried' : 'intra-iteration')
  //    dependence from i2 to i1 with respect to the loop specified by header.
  //

  // To update:
  //  (1) ensure that the cloned instructions appear in the id maps.
  //  (2) duplicate the entries in biimap
  //  (3) duplicate the entries in DepToCountMap

  // Create new IDs for duplicated instructions, loop headers
  {
    Function *old_fcn = callee;

    // For every instruction in the old function which also has an inst-id:
    //  - create a new inst-id for the clone of that instruction.
    for(Function::iterator i=old_fcn->begin(), e=old_fcn->end(); i!=e; ++i)
    {
      BasicBlock *bb = &*i;

      for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
      {
        Instruction *old_inst = &*j;

        if( prof.InstToIdMap.count(old_inst) )
        {
          Instruction *new_inst = old_inst;
          assert( maybe_map(new_inst,vmap) && "Instruction not in vmap!");

          const unsigned new_id = prof.IdToInstMap.size();
          prof.IdToInstMap[ new_id   ] = new_inst;
          prof.InstToIdMap[ new_inst ] = new_id;
        }
      }
    }
  }

  // Duplicate entries in biimap
  {
    LAMPLoadProfile::BIIMap new_map( prof.biimap );
    for(LAMPLoadProfile::BIIMap::iterator i=prof.biimap.begin(), e=prof.biimap.end(); i!=e; ++i)
    {
      const biikey_t &old_key = i->first;

      const BasicBlock *bb = old_key.b;
      Instruction *i1 = prof.IdToInstMap[ old_key.i1 ],
                  *i2 = prof.IdToInstMap[ old_key.i2 ];

      const bool loop_cloned = maybe_map(bb,vmap);
      const bool i1_cloned = maybe_map(i1,vmap);
      const bool i2_cloned = maybe_map(i2,vmap);

      if( loop_cloned || i1_cloned || i2_cloned )
      {
        biikey_t new_key(bb, prof.InstToIdMap[i1], prof.InstToIdMap[i2], old_key.cross_iter);
        new_map[ new_key ] =i->second;
      }

      // A dependence from an instruction in a callsite to another
      // instruction in that callsite, qualified w.r.t. a loop outside of that callsite
      if( !loop_cloned && i1_cloned && i2_cloned )
      {
        biikey_t new_key(bb, old_key.i1, prof.InstToIdMap[i2], old_key.cross_iter);
        new_map[ new_key ] = i->second;

        new_key = biikey_t(bb, prof.InstToIdMap[i1], old_key.i2, old_key.cross_iter);
        new_map[ new_key ] = i->second;
      }

    }
    prof.biimap.swap( new_map );
  }

  // Duplicate entries in DepToCountMap
  {
    LAMPLoadProfile::BII_Count_Map new_map( prof.DepToCountMap );
    for(LAMPLoadProfile::BII_Count_Map::iterator i=prof.DepToCountMap.begin(), e=prof.DepToCountMap.end(); i!=e; ++i)
    {
      const biikey_t &old_key = i->first;

      const BasicBlock *bb = old_key.b;
      Instruction *i1 = prof.IdToInstMap[ old_key.i1 ],
                  *i2 = prof.IdToInstMap[ old_key.i2 ];

      const bool loop_cloned = maybe_map(bb,vmap);
      const bool i1_cloned = maybe_map(i1,vmap);
      const bool i2_cloned = maybe_map(i2,vmap);
      if( loop_cloned || i1_cloned || i2_cloned )
      {
        biikey_t new_key(bb, prof.InstToIdMap[i1], prof.InstToIdMap[i2], old_key.cross_iter);
        new_map[ new_key ] = i->second;
      }

      if( !loop_cloned && i1_cloned && i2_cloned )
      {
        biikey_t new_key(bb, old_key.i1, prof.InstToIdMap[i2], old_key.cross_iter);
        new_map[ new_key ] = i->second;

        new_key = biikey_t(bb, prof.InstToIdMap[i1], old_key.i2, old_key.cross_iter);
        new_map[ new_key ] = i->second;
      }
    }
    prof.DepToCountMap.swap( new_map );
  }
}

}
}
