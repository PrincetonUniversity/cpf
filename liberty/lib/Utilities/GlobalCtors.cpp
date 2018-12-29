#include <vector>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"

#include "liberty/Utilities/GlobalCtors.h"


namespace liberty {
  using namespace llvm;


  // Wrap a function pointer in an array of structures so that it
  // is the type required for a global ctor list or global dtor list
  // then append it to those lists
  static void appendToConstructorArray( Function *f, const std::string &name, const unsigned int priority = 65535, const bool ascending = true) {

    LLVMContext &Context = f->getParent()->getContext();

    assert( f->arg_size() == 0 && "Cannot pass arguments to a function ``before main''");

    Module *module = f->getParent();
    assert( module && "Function has not yet been added to a module");

    // Several types we will use often.
    // We give them friendly names here.
    // int
    Type *intType = Type::getInt32Ty(Context);

    // void fcn(void)
    std::vector<Type*> formals(0);
    FunctionType *voidFcnVoidType = FunctionType::get(
      Type::getVoidTy(Context),
      formals,
      false);

    // The type of a global constructor
    // record, llvm calls this "{ i32, void () * }"
    std::vector<Type*> fieldsty(2);
    fieldsty[0] = intType;
    fieldsty[1] = PointerType::getUnqual( voidFcnVoidType );
    StructType *ctorRecordType = StructType::get(f->getContext(), fieldsty);



    // Build a new constructor list
    std::vector<Constant*> elts;

    // If there was already a constructor list...
    GlobalVariable *previous = module->getGlobalVariable(name);
    if( previous ) {

      // and it has an initializer
      if( previous->hasInitializer() ) {

        // and if that initializer was an array
        if( ConstantArray *ca = dyn_cast<ConstantArray>( previous->getInitializer() ) ) {

          // We might need to create a driver function to assert priorities.
          Function *driver = 0;
          BasicBlock *entry = 0;

          // Copy over it's elements
          for(User::op_iterator i=ca->op_begin(), e=ca->op_end(); i!=e; ++i) {

            ConstantStruct *oldRec = cast< ConstantStruct >( i->get() );
            ConstantInt *prio = cast< ConstantInt >( oldRec->getAggregateElement(0u) );

            if( ( (prio->getZExtValue() > priority) && ascending )
              || ( (prio->getZExtValue() < priority) && !ascending )
            )
            {
              if( !driver )
              {
                // Create a new driver function that will call US before them.
                driver = Function::Create(voidFcnVoidType,
                  GlobalValue::InternalLinkage, "callBeforeMain_driver.",
                  module);
                entry = BasicBlock::Create(Context, "entry", driver);
                CallInst::Create(f, "", entry);
                f = driver;
              }

              Function *initor  = cast< Function >(
                oldRec->getAggregateElement(1) );
              CallInst::Create(initor, "", entry);
            }
            else
            {
              elts.push_back( oldRec );
            }
          }

          if( driver )
            ReturnInst::Create(Context,entry);
        }
      }

      // and delete the old one
      previous->eraseFromParent();
    }

    // (global constructor record value)
    std::vector<Constant *> fields(2);
    fields[0] = ConstantInt::get( intType, priority);
    fields[1] = f;
    Constant *record = ConstantStruct::get(ctorRecordType,fields);

    // Add our new elements
    elts.push_back(record);

    // Add it to the module
    Constant *array = ConstantArray::get(
                        ArrayType::get(ctorRecordType, elts.size()),
                        elts);

    new GlobalVariable(
      *module,
      array->getType(),
      false,
      GlobalValue::AppendingLinkage,
      array,
      name);
  }

  //void callBeforeMain( Function *f, const unsigned int priority = 65535 ) {
  void callBeforeMain( Function *f, const unsigned int priority) {
    appendToConstructorArray(f, "llvm.global_ctors", priority, true);
  }

  //void callAfterMain( Function *f, const unsigned int priority = 65535 ) {
  void callAfterMain( Function *f, const unsigned int priority ) {
    appendToConstructorArray(f, "llvm.global_dtors", priority, false);
  }


}
