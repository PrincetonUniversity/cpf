#define DEBUG_TYPE "inst-insert"

#include <utility>
#include <ostream>
#include <sstream>

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Utilities/InstInsertPt.h"

namespace liberty {

  using namespace llvm;

  void InstInsertPt::print(llvm::raw_ostream &out) const{
    if( invalid ) {
      out << "Invalid InstInsertPt\n";
      return;
    }

    out << "InstInsertPt @ fcn "
        << pos->getParent()->getParent()->getName().str()
        << " block "
        << pos->getParent()->getName().str()
        << '\n';

    if( _before )
      out << "Before inst: ";
    else
      out << "After inst: ";
    pos->print(out);

    out << "Basic Block:\n";
    pos->getParent()->print(out);
  }

  void InstInsertPt::dump() const {
    print(errs());
  }

  void InstInsertPt::insert(Instruction *i) {
    assert( !invalid );

    if( _before ) {
      // This error would be caught eventually by the module verifier
      // but its easier if we know right when it happens
      if( isa<PHINode>(pos) && ! isa<PHINode>(i) ) {
        errs() << "Attempting to insert a non-PHI before a PHI!\n"
             << "non-PHI is:\n";
        i->dump();
        errs() << "Position is:\n";
        dump();
        assert(0);
      }

      i->insertBefore(pos);
    } else {
      // This error would be caught eventually by the module verifier
      // but its easier if we know right when it happens
      if( !isa<PHINode>(pos) && isa<PHINode>(i) ) {
        errs() << "Attempting to insert a PHI after a non-PHI!\n"
             << "PHI is:\n";
        i->dump();
        errs() << "Position is:\n";
        dump();
        assert(0);
      }

      // Attempting to insert a non-PHI after a PHI is
      // valid, but pos might not be the last PHI in the block.
      // If so, skip...
      if( !isa<PHINode>(i) ) {
        // We are not inserting a PHI

        // Make sure we are past all of the PHIs in this block.

        //sot
        //while( PHINode *next = dyn_cast<PHINode>( ilist_nextprev_traits<Instruction>::getNext(pos) ) ) {
        while ( pos->getNextNode() && isa<PHINode>( pos->getNextNode() ) ) {
          DEBUG(errs() << "InstInsertPt: skipping PHIs\n");
          pos = dyn_cast<PHINode>( pos->getNextNode() );
        }
      }

      i->insertAfter(pos);
      pos = i;
    }
  }


}
