#ifndef INST_INSERTION_POINT_H
#define INST_INSERTION_POINT_H

/* liberty/Utilities/InstInsertPt.h
 *
 * Defines class InstInsertPt, which
 * is effectively a /better/ output iterator
 * for inserting instructions into
 * basic blocks.  In particular, it specifies
 * not an instruction but a position
 * between instructions, and allows
 * you to insert multiple instructions
 * before or after an instruction
 *
 * This guarantees that for * any sequence of inserts i1, i2, ..., ik
 * that i1 will dominate i2 will dominate ... will dominate ik
 * and that they will all be inserted to the same basic block.
 *
 * It does not guarantee that they will be inserted adjacently,
 * because all PHIs must appear together before all non-PHIs.
 *
 * This guarantees that it will only insert non-PHIs
 * after PHIs.  Any violation of PHI-ordering will be
 * caught immediately.
 */

#include <utility>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"


namespace liberty {

  using namespace llvm;



  // Represents an insertion point in a function.
  // Simplifies the handling of before/after
  // a particular instruction.
  class InstInsertPt {
    private:
      // Use one of the static factories instead.
      InstInsertPt(bool b4, Instruction *position)
        : invalid(false),
          _before(b4),
          pos(position)
      {
        assert(pos && "Position cannot be null");
      }

      // Make a basic block non-empty by
      // inserting a no-op operation.
      // return that operation
      static Instruction *makeNonEmpty(BasicBlock *bb) {

        LLVMContext &Context = bb->getParent()->getContext();


        Value *zero = ConstantInt::get( Type::getInt32Ty(Context), 0 );
        Instruction *nop = BinaryOperator::Create(
          llvm::Instruction::Add,
          zero, zero, "Empty Basic Blocks Suck.", bb);

        return nop;
      }

    public:
      // We need a default constructor so these
      // can be inserted into a stl collection.
      // This constructs an invalid InstInsertPt
      InstInsertPt()
        : invalid(true) {}

      InstInsertPt(const InstInsertPt &pt)
        : invalid(pt.invalid), _before(pt._before), pos(pt.pos) {}

      bool operator<(const InstInsertPt& other) const {
        if (invalid < other.invalid) {
          return true;
        } else if (_before > other._before) {
          return true;
        } else if (pos < other.pos) {
          return true;
        } else {
          return false;
        }
      }

      // Create a new insertion point at the beginning of this function
      static InstInsertPt Beginning(Function *f) {
        if( f->isDeclaration() ) {
          LLVMContext& context = f->getContext();
          BasicBlock *bb = BasicBlock::Create( context );
          f->getBasicBlockList().push_back( bb );
          return Beginning( bb );
        } else {
          return Beginning( &f->getEntryBlock() );
        }
      }

      // Create a new insertion point at the beginning of this basic
      // block: after all the phi nodes, but before anything else.
      static InstInsertPt Beginning(BasicBlock *bb) {

        if( bb->empty() ) {
          // empty basic blocks suck
          Instruction *nop = makeNonEmpty(bb);
          return After(nop);

        } else if( isa<PHINode>(bb->back()) ) {
          // Contrary to what the comment in BasicBlock.h says, getFirstNonPHI
          // chokes if there are no non-PHIs.
          //return InstInsertPt(true, bb->getFirstNonPHI() );

          return After(&bb->back());
        } else {
          return InstInsertPt(true, &*(bb->getFirstInsertionPt()) );
        }
      }

      // Create a new insertion point at the end of this basic block:
      // before the terminator, but after everything else.
      static InstInsertPt End(BasicBlock *bb) {
        if( bb->empty() ) {

          // empty basic blocks suck
          Instruction *nop = makeNonEmpty(bb);
          return After(nop);

        } else if( !bb->back().isTerminator() ) {
          if( isa<PHINode>(bb->back()) ) {

            // If the bb only contains PHIs, we should insert a "real"
            // instruction to avoid the following behavior: say one creates
            // both Beginning and End InsertPts, inserts some instructions at
            // Beginning, and then inserts some instructions at End.
            // Counterintuitively, the Beginning instructions will appear
            // *after* the End instructions.

            Instruction *nop = makeNonEmpty(bb);
            return After(nop);
          } else {
            return After(&bb->back());
          }
        } else {
          return InstInsertPt(true, bb->getTerminator() );
        }
      }

      // Create a new insertion point before the given instruction
      static InstInsertPt Before(Instruction *i, BasicBlock *bb=0) {
        if (i)
          return InstInsertPt(true, i);
        else
          return Beginning(bb);
      }

      // Create a new insertion point after the given instruction
      static InstInsertPt After(Instruction *i, BasicBlock *bb = 0) {
        if( i )
          return InstInsertPt(false, i);
        else
          return Beginning(bb);
      }

      // Debugging facilities
      void print(llvm::raw_ostream &out) const;
      void dump() const;

      // Place the instruction at the specified point
      void insert(Instruction *i);

      // Ugly syntax for Tom ;)
      InstInsertPt &operator<<(Instruction *i) {
        insert(i);
        return *this;
      }

      // Interrogate this position
      Module *getModule() {
        assert( !invalid );
        return getFunction()->getParent();
      }

      Function *getFunction() {
        assert( !invalid );
        return getBlock()->getParent();
      }

      BasicBlock *getBlock() {
        assert( !invalid );
        return pos->getParent();
      }

      Instruction *getPosition() {
        assert( !invalid );
        return pos;
      }

      bool before() const {
        assert( !invalid );
        return _before;
      }

    private:
      bool                                    invalid;
      bool                                    _before;
      Instruction *                           pos;
  };

  typedef std::vector<InstInsertPt> InstInsertPts;

}

#endif
