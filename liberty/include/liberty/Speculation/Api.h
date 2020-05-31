// This file is responsible for knowing
// the names and types of all methods of
// the runtime library.
#ifndef LLVM_LIBERTY_SPEC_PRIV_API_H
#define LLVM_LIBERTY_SPEC_PRIV_API_H

#include "llvm/IR/Constant.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"

#include "liberty/Redux/Reduction.h"
#include "liberty/Speculation/Classify.h"

#include <vector>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

struct Api
{
  Api() : mod(0) {}

  Api(Module *m, StringRef p = "__specpriv") : mod(m), personality(p)
  {
    LLVMContext &ctx = mod->getContext();

    voidty = Type::getVoidTy(ctx);
    u1 = Type::getInt1Ty(ctx);
    u8 = Type::getInt8Ty(ctx);
    u16 = Type::getInt16Ty(ctx);
    u32 = Type::getInt32Ty(ctx);
    u64 = Type::getInt64Ty(ctx);
    voidptr = PointerType::getUnqual(u8);

    std::string name = (Twine(personality) + "_queue").str();
    queueTy = mod->getTypeByName( name );
    if( !queueTy )
    {
      queueTy = StructType::create(ctx, name);
    }
    queueTyPtr = PointerType::getUnqual( queueTy );

    genericPersonality = "__parallel";

    std::vector<Type *> formals;
    fv2v = FunctionType::get(voidty, formals, false);
    fv2i = FunctionType::get(u32, formals, false);

    formals.push_back(u32);
    fi2v = FunctionType::get(voidty, formals,false);
    fi2i = FunctionType::get(u32, formals, false);
    fi2i64 = FunctionType::get(u64, formals, false);

    formals.clear();
    formals.push_back(voidptr);
    fvp2v = FunctionType::get(voidty, formals, false);
    formals.push_back(u32);
    fvpi2v = FunctionType::get(voidty, formals, false);
    formals.push_back(u32);
    fvpii2v = FunctionType::get(voidty, formals, false);

    formals.clear();
    formals.push_back(voidptr);
    formals.push_back(u32);
    formals.push_back(voidptr);
    fvpivp2v = FunctionType::get(voidty, formals, false);

    formals.clear();
    formals.push_back(u32);
    formals.push_back( PointerType::getUnqual(fvp2v) );
    formals.push_back(voidptr);
    ficvp2i = FunctionType::get(u32, formals, false);

    formals.clear();
    formals.push_back(u32);
    formals.push_back(u32);
    fii2q = FunctionType::get(queueTyPtr, formals, false);
    f2i2v = FunctionType::get(voidty, formals,false);
    formals.push_back(u32);
    formals.push_back(u32);
    f4i2v = FunctionType::get(voidty, formals, false);

    formals.clear();
    formals.push_back(u32);
    formals.push_back(u64);
    fii2v = FunctionType::get(voidty, formals, false);

    formals.clear();
    formals.push_back(u64);
    fi642vp = FunctionType::get(voidptr, formals, false);
    formals.push_back(u64);
    fi64i642vp = FunctionType::get(voidptr, formals, false);
    fi642v = FunctionType::get(voidty, formals, false);

    formals.clear();
    formals.push_back(voidptr);
    formals.push_back(u64);
    fvpi642vp = FunctionType::get(voidptr, formals, false);

    formals.clear();
    formals.push_back(queueTyPtr);
    fq2i = FunctionType::get(u64, formals, false);
    fq2v = FunctionType::get(voidty, formals, false);

    formals.push_back(u64);
    fqi2v = FunctionType::get(voidty, formals, false);

    formals.clear();
    formals.push_back(voidptr);
    formals.push_back(u64);
    formals.push_back(u64);
    formals.push_back(u64);
    fvpi643vp = FunctionType::get(voidty, formals, false);

    formals.clear();
    formals.push_back(u32);
    formals.push_back(PointerType::getUnqual(fvpi643vp));
    formals.push_back(voidptr);
    formals.push_back(u64);
    formals.push_back(u64);
    fvpdisp = FunctionType::get(voidty, formals, false);
  }

  Constant* getInit()
  {
    std::string name = (Twine(personality) + "_init").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getFini()
  {
    std::string name = (Twine(personality) + "_fini").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getDeclareNumLocalValues()
  {
    std::string name = (Twine(personality) + "_declare_num_lv").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getStoreLocalValue()
  {
    // (u32, u64) -> void
    std::string name = (Twine(personality) + "_store_lv").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fii2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getLoadLocalValue()
  {
    // u32 -> u64
    std::string name = (Twine(personality) + "_load_lv").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi2i64);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getRecoverDone()
  {
    std::string name = (Twine(personality) + "_recovery_finished").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getLastCommitted()
  {
    std::string name = (Twine(personality) + "_last_committed").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2i);
    return cast<Constant>(wrapper.getCallee());
  }
  Constant *getMisspecIter()
  {
    std::string name = (Twine(personality) + "_misspec_iter").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2i);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getPredict()
  {
    std::vector<Type*> formals(2);
    formals[0] = u64;
    formals[1] = u64;
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = (Twine(personality) + "_predict").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getIO(Function *callee)
  {
    FunctionType *fty = cast< FunctionType >( cast< PointerType >( callee->getType() ) ->getElementType()  );
    std::string name = (Twine(personality) + "_io_" + callee->getName()).str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getUO()
  {
    std::vector<Type*> formals(4);
    formals[0] = voidptr; // pointer
    formals[1] = u64;      // heap number or (-1)
    formals[2] = u64;      // sub-heap number or (-1)
    formals[3] = voidptr; // error on misspeculation
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = (Twine(personality) + "_uo").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getPtrResidue()
  {
    std::vector<Type*> formals(3);
    formals[0] = voidptr;
    formals[1] = u16;
    formals[2] = voidptr;
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = (Twine(personality) + "_ptr_residue").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getSetPstageReplica()
  {
    std::string name = (Twine(personality) + "_set_pstage_replica_id").str();
    //Function *cc = cast<Function>( mod->getOrInsertFunction(name, fi2v) );
    FunctionCallee cc = mod->getOrInsertFunction(name, fi2v);
    return cast<Constant>(cc.getCallee());
  }

  Constant *getWorkerFinishes()
  {
    std::string name = (Twine(personality) + "_worker_finishes").str();
    //Function *cc = cast<Function>( mod->getOrInsertFunction(name, fi2v) );
    FunctionCallee cc = mod->getOrInsertFunction(name, fi2v);
    // with spawning process just once worker_finishes returns
    //cc->setDoesNotReturn();
    return cast<Constant>(cc.getCallee());
  }

  Constant *getWorkerId()
  {
    std::string name = (Twine(personality) + "_my_worker_id").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2i);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getFinalIterCkptCheck()
  {
    std::string name = (Twine(personality) + "_final_iter_ckpt_check").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi642v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getCkptCheck()
  {
    std::string name = (Twine(personality) + "_ckpt_check").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2i);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getMisspeculate()
  {
    std::vector<Type*> formals(1);
    formals[0] = voidptr;
    FunctionType *fty = FunctionType::get(voidty, formals, false);
    std::string name = (Twine(personality) + "_misspec").str();
    Function *cc = mod->getFunction(name);
    cc->setDoesNotReturn();
    return cc;
  }

  Constant *getCurrentIter()
  {
    std::string name = (Twine(personality) + "_current_iter").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2i);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getAlloc(HeapAssignment::Type heap)
  {
    if( heap == HeapAssignment::Redux )
      return getAllocRedux();
    else
      return getAlloc( getNameForHeap(heap) );
  }

  Constant *getFree(HeapAssignment::Type heap)
  {
    return getFree( getNameForHeap(heap) );
  }

  Constant *getPrivateReadRange()
  {
    std::vector<Type*> formals(3);
    formals[0] = voidptr;
    formals[1] = u32;
    formals[2] = voidptr;
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = (Twine(personality) + "_private_read_range").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }
  Constant *getPrivateReadRange(uint64_t fixed)
  {
    assert( fixed == 1  || fixed == 2 || fixed == 4 || fixed == 8 );

    std::vector<Type*> formals(2);
    formals[0] = voidptr;
    formals[1] = voidptr;
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    Twine name = Twine(personality) + "_private_read_" + Twine(fixed) + "b";
    FunctionCallee wrapper = mod->getOrInsertFunction(name.str(),fty);
    return cast<Constant>(wrapper.getCallee());
  }
  Constant *getPrivateReadRangeStride()
  {
    std::vector<Type*> formals(5);
    formals[0] = voidptr;   // base
    formals[1] = u32;       // num strides
    formals[2] = u32;       // stride width
    formals[3] = u32;       // len/stride
    formals[4] = voidptr;   // name
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = (Twine(personality) + "_private_read_range_stride").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getPrivateWriteRange()
  {
    std::vector<Type*> formals(2);
    formals[0] = voidptr;
    formals[1] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = (Twine(personality) + "_private_write_range").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }
  Constant *getSharePrivateWriteRange()
  {
    std::vector<Type*> formals(2);
    formals[0] = voidptr;
    formals[1] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = (Twine(personality) + "_shareprivate_write_range").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }
  Constant *getReduxWriteRange()
  {
    std::vector<Type*> formals(2);
    formals[0] = voidptr;
    formals[1] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = (Twine(personality) + "_redux_write_range").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }


  Constant *getPrivateWriteRange(uint64_t fixed)
  {
    assert( fixed == 1  || fixed == 2 || fixed == 4 || fixed == 8 );

    std::vector<Type*> formals(1);
    formals[0] = voidptr;
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    Twine name = Twine(personality) + "_private_write_" + Twine(fixed) + "b";
    FunctionCallee wrapper = mod->getOrInsertFunction(name.str(),fty);
    return cast<Constant>(wrapper.getCallee());
  }
  Constant *getPrivateWriteRangeStride()
  {
    std::vector<Type*> formals(4);
    formals[0] = voidptr;   // base
    formals[1] = u32;       // num strides
    formals[2] = u32;       // stride width
    formals[3] = u32;       // len/stride
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = (Twine(personality) + "_private_write_range_stride").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getSpawn()
  {
    std::string name = (Twine(personality) + "_spawn_workers_callback").str();
    //return mod->getOrInsertFunction(name, ficvp2i);
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvpdisp);
    return cast<Constant>(wrapper.getCallee());
  }
  Constant *getNumWorkers()
  {
    std::string name = (Twine(personality) + "_num_workers").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2i);
    return cast<Constant>(wrapper.getCallee());
  }
  Constant *getNumAvailableWorkers()
  {
    std::string name = (Twine(personality) + "_num_available_workers").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2i);
    return cast<Constant>(wrapper.getCallee());
  }
  Constant *getJoin()
  {
    std::string name = (Twine(personality) + "_join_children").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2i);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getSetLoopID()
  {
    std::string name = (Twine(personality) + "_set_loopID").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getGetLoopID()
  {
    std::string name = (Twine(personality) + "_get_loopID").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2i);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getBeginInvocation()
  {
    std::string name = (Twine(personality) + "_begin_invocation").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2i);
    return cast<Constant>(wrapper.getCallee());
  }
  Constant *getEndInvocation()
  {
    std::string name = (Twine(personality) + "_end_invocation").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2i);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getBeginIter()
  {
    std::string name = (Twine(personality) + "_begin_iter").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }
  Constant *getEndIter()
  {
    std::string name = (Twine(personality) + "_end_iter").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi2v);
    return cast<Constant>(wrapper.getCallee());
  }

   Constant *getSetLastReduxUpIter()
  {
    std::string name = (Twine(personality) + "_set_last_redux_update_iter").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getNumLocals()
  {
    std::string name = (Twine(personality) + "_num_local").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2i);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getAddNumLocals()
  {
    std::string name = (Twine(personality) + "_add_num_local").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getBegin()
  {
    std::string name = (Twine(personality) + "_begin").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }
  Constant *getEnd()
  {
    std::string name = (Twine(personality) + "_end").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getGenericBegin()
  {
    std::string name = (Twine(genericPersonality) + "_begin").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }
  Constant *getGenericEnd()
  {
    std::string name = (Twine(genericPersonality) + "_end").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getSpawnWorkersBegin()
  {
    std::string name = "__spawn_workers_begin";
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Type *getQueueType()
  {
    return queueTyPtr;
  }

  IntegerType *getU64()
  {
    return u64;
  }

  IntegerType *getU32()
  {
    return u32;
  }

  Type *getVoid()
  {
    return voidty;
  }

  Constant *getProduce()
  {
    std::string name = (Twine(personality) + "_produce").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fqi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getProduceLocal()
  {
    std::string name = (Twine(personality) + "_produce_locals").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fq2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getFlushQueue()
  {
    std::string name = (Twine(personality) + "_flush").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fq2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getClearQueue()
  {
    std::string name = (Twine(personality) + "_clear").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fq2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getConsume()
  {
    std::string name = (Twine(personality) + "_consume").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fq2i);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getConsumeLocal()
  {
    std::string name = (Twine(personality) + "_consume_locals").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fq2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getProduceToReplicated()
  {
    std::string name = (Twine(personality) + "_produce_replicated").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fqi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getConsumeInReplicated()
  {
    std::string name = (Twine(personality) + "_consume_replicated").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fq2i);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getCreateQueue()
  {
    std::string name = (Twine(personality) + "_create_queue").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,f4i2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getFetchQueue()
  {
    std::string name = (Twine(personality) + "_fetch_queue").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fii2q);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getAllocQueues()
  {
    std::string name = (Twine(personality) + "_alloc_queues").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getAllocStageQueues()
  {
    std::string name = (Twine(personality) + "_alloc_stage_queues").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,f2i2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getResetQueue()
  {
    std::string name = (Twine(personality) + "_reset_queue").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fq2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getFreeQueue()
  {
    std::string name = (Twine(personality) + "_free_queue").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fq2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getFreeQueues()
  {
    std::string name = (Twine(personality) + "_free_queues").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getEnablePrivate()
  {
    std::string name = (Twine(personality) + "_enable_private").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getVerWrite()
  {
    std::string name = (Twine(personality) + "_ver_write").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvpi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getVerWrite1()
  {
    std::string name = (Twine(personality) + "_ver_write1").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvp2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getVerWrite2()
  {
    std::string name = (Twine(personality) + "_ver_write2").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvp2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getVerWrite4()
  {
    std::string name = (Twine(personality) + "_ver_write4").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvp2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getVerWrite8()
  {
    std::string name = (Twine(personality) + "_ver_write8").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvp2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getVerRead()
  {
    std::string name = (Twine(personality) + "_ver_read").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvpi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getVerRead1()
  {
    std::string name = (Twine(personality) + "_ver_read1").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvp2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getVerRead2()
  {
    std::string name = (Twine(personality) + "_ver_read2").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvp2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getVerRead4()
  {
    std::string name = (Twine(personality) + "_ver_read4").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvp2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getVerRead8()
  {
    std::string name = (Twine(personality) + "_ver_read8").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvp2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getVerMemMove()
  {
    std::string name = (Twine(personality) + "_ver_memmove").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvpivp2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getVerMalloc()
  {
    std::string name = (Twine(personality) + "_ver_malloc").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi642vp);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getVerCalloc()
  {
    std::string name = (Twine(personality) + "_ver_calloc").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi64i642vp);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getVerRealloc()
  {
    std::string name = (Twine(personality) + "_ver_realloc").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvpi642vp);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getVerFree()
  {
    std::string name = (Twine(personality) + "_ver_free").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvp2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getMalloc()
  {
    std::string name = (Twine(personality) + "_malloc").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi642vp);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getCalloc()
  {
    std::string name = (Twine(personality) + "_calloc").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi64i642vp);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getRealloc()
  {
    std::string name = (Twine(personality) + "_realloc").str();
     FunctionCallee wrapper = mod->getOrInsertFunction(name,fvpi642vp);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getFree()
  {
    std::string name = (Twine(personality) + "_free").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvp2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getSeparationInit()
  {
    std::vector<Type*> formals(2);
    formals[0] = u32;
    formals[1] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = (Twine(personality) + "_separation_init").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getSeparationFini()
  {
    std::vector<Type*> formals(2);
    formals[0] = u32;
    formals[1] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = (Twine(personality) + "_separation_fini").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getSepMalloc()
  {
    std::vector<Type*> formals(2);
    formals[0] = u64;
    formals[1] = u32;
    FunctionType *fty = FunctionType::get(voidptr, formals, false);

    std::string name = (Twine(personality) + "_separation_malloc").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getSepCalloc()
  {
    std::vector<Type*> formals(3);
    formals[0] = u64;
    formals[1] = u64;
    formals[2] = u32;
    FunctionType *fty = FunctionType::get(voidptr, formals, false);

    std::string name = (Twine(personality) + "_separation_calloc").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getSepRealloc()
  {
    std::vector<Type*> formals(3);
    formals[0] = voidptr;
    formals[1] = u64;
    formals[2] = u32;
    FunctionType *fty = FunctionType::get(voidptr, formals, false);

    std::string name = (Twine(personality) + "_separation_realloc").str();
     FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getSepFree()
  {
    std::string name = (Twine(personality) + "_separation_free").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvpi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getVerSepMalloc()
  {
    std::vector<Type*> formals(2);
    formals[0] = u64;
    formals[1] = u32;
    FunctionType *fty = FunctionType::get(voidptr, formals, false);

    std::string name = (Twine(personality) + "_ver_separation_malloc").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getVerSepCalloc()
  {
    std::vector<Type*> formals(3);
    formals[0] = u64;
    formals[1] = u64;
    formals[2] = u32;
    FunctionType *fty = FunctionType::get(voidptr, formals, false);

    std::string name = (Twine(personality) + "_ver_separation_calloc").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getVerSepRealloc()
  {
    std::vector<Type*> formals(3);
    formals[0] = voidptr;
    formals[1] = u64;
    formals[2] = u32;
    FunctionType *fty = FunctionType::get(voidptr, formals, false);

    std::string name = (Twine(personality) + "_ver_separation_realloc").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getVerSepFree()
  {
    std::string name = (Twine(personality) + "_ver_separation_free").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fvpi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getClearSeparationHeaps()
  {
    std::string name = (Twine(personality) + "_clear_separation_heaps").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getRegisterVRO()
  {
    std::vector<Type*> formals(1);
    formals[0] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, true);

    std::string name = (Twine(personality) + "_register_versioned_ro").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getRegisterNVRO()
  {
    std::vector<Type*> formals(1);
    formals[0] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, true);

    std::string name = (Twine(personality) + "_register_ro").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getRegisterVNRBW()
  {
    std::vector<Type*> formals(1);
    formals[0] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, true);

    std::string name = (Twine(personality) + "_register_versioned_nrbw").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getRegisterNVNRBW()
  {
    std::vector<Type*> formals(1);
    formals[0] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, true);

    std::string name = (Twine(personality) + "_register_nrbw").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getRegisterVSP()
  {
    std::vector<Type*> formals(2);
    formals[0] = u32;
    formals[1] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, true);

    std::string name = (Twine(personality) + "_register_versioned_stage_private").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getRegisterNVSP()
  {
    std::vector<Type*> formals(2);
    formals[0] = u32;
    formals[1] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, true);

    std::string name = (Twine(personality) + "_register_stage_private").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getRegisterVUC()
  {
    std::vector<Type*> formals(1);
    formals[0] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, true);

    std::string name = (Twine(personality) + "_register_versioned_unclassified").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getRegisterNVUC()
  {
    std::vector<Type*> formals(1);
    formals[0] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, true);

    std::string name = (Twine(personality) + "_register_unclassified").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getSeparationAllocContext()
  {
    std::string name = (Twine(personality) + "_get_separation_alloc_context").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2i);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getPushSeparationAllocContext()
  {
    std::string name = (Twine(personality) + "_push_separation_alloc_context").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getPopSeparationAllocContext()
  {
    std::string name = (Twine(personality) + "_pop_separation_alloc_context").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getInitPredictors()
  {
    std::vector<Type*> formals(3);
    formals[0] = u32;
    formals[1] = u32;
    formals[2] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, true);

    std::string name = (Twine(personality) + "_init_predictors").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant* getFiniPredictors()
  {
    std::string name = (Twine(personality) + "_fini_predictors").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getCheckLoopInvariant()
  {
    std::vector<Type*> formals(4);
    formals[0] = u32;
    formals[1] = u64;
    formals[2] = voidptr;
    formals[3] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = (Twine(personality) + "_check_loop_invariant").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getRegisterLoopInvariantBuffer()
  {
    std::vector<Type*> formals(4);
    formals[0] = u32;
    formals[1] = u64;
    formals[2] = voidptr;
    formals[3] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = (Twine(personality) + "_register_loop_invariant_buffer").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getCheckRegisterLinearPredictor()
  {
    std::vector<Type*> formals(5);
    formals[0] = u1;
    formals[1] = u32;
    formals[2] = u64;
    formals[3] = voidptr;
    formals[4] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = (Twine(personality) + "_check_and_register_linear_predictor").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getDebugPrintf()
  {
    std::string name = (Twine(personality) + "_debugprintf").str();

    std::vector<Type*> formals(1);
    formals[0] = voidptr;
    FunctionType *fty = FunctionType::get(u32, formals, true);

    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getInformStrategy()
  {
    std::string name = (Twine(personality) + "_inform_strategy").str();

    std::vector<Type*> formals(3);
    formals[0] = u32;
    formals[1] = u32;
    formals[2] = u32;
    FunctionType *fty = FunctionType::get(voidty, formals, true);

    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getAllocStratInfo()
  {
    std::string name = (Twine(personality) + "_alloc_strategies_info").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getCleanupStrategy()
  {
    std::string name = (Twine(personality) + "_cleanup_strategy").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getSetCurLoopStrat()
  {
    std::string name = (Twine(personality) + "_set_current_loop_strategy").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getLoopInvocation()
  {
    std::string name = (Twine(personality) + "_loop_invocation").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getLoopExit()
  {
    std::string name = (Twine(personality) + "_loop_exit").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getPushContext()
  {
    std::string name = (Twine(personality) + "_push_context").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fi2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getPopContext()
  {
    std::string name = (Twine(personality) + "_pop_context").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fv2v);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getGetContext()
  {
    std::string name = (Twine(personality) + "_get_context").str();

    FunctionType* fty = FunctionType::get(u64, false);

    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  static uint64_t getHeapCodeMask()
  {
    // other version of runtime with subheaps
    //return (15ULL << 43);

    return (15ULL << 43);
    /* return (7ULL << 44); */
  }

  static uint64_t getCodeForHeap(HeapAssignment::Type heap)
  {
    // These magic numbers must correspond with
    // the codes in support/specpriv-executive/api.c
    const uint64_t codes[] =
      { /* ro       */ (5ULL << 43),
        /* shared   */ (4ULL << 43),
        /* redux    */ (2ULL << 43),
        /* local    */ (6ULL << 43),
        /* killpriv */ (8ULL << 43),
        /* cheappriv */ (10ULL << 43),
        /* priv     */ (3ULL << 43)
      };

    // other version of runtime with subheaps
    #if 0
    const uint64_t codes[] =
      { /* ro       */ (5ULL << 43),
        /* shared   */ (4ULL << 43),
        /* redux    */ (2ULL << 43),
        /* local    */ (1ULL << 43),
        /* priv     */ (3ULL << 43)
      };
    #endif

    return codes[heap];
  }

  static StringRef getNameForHeap(HeapAssignment::Type heap)
  {
    StringRef names[] =
      { "ro", "shared",  "redux", "local", "killpriv", "sharepriv", "priv", "unclassified" };

    return names[heap];
  }

  static uint64_t getSubHeapCodeMask()
  {
    return (7ULL << 40);
  }

  static uint64_t getCodeForSubHeap(int subheap)
  {
    assert( subheap >= 0 && "Attempt to get code for -1");
    uint64_t sh = (uint64_t)subheap;
    return sh << 40;
  }

private:
  Module *mod;
  StringRef personality;
  StringRef genericPersonality;
  Type *voidty, *voidptr, *queueTy;
  PointerType *queueTyPtr;
  IntegerType *u1, *u8, *u16, *u32, *u64;
  FunctionType *fv2v, *fv2i, *fi2i, *fi2v, *fii2v;
  FunctionType *fqi2v, *fq2i, *fq2v, *fii2q, *f4i2v, *f2i2v;
  FunctionType *ficvp2i;
  FunctionType *fvp2v, *fvpi2v, *fvpii2v, *fvpivp2v;
  FunctionType *fi2i64, *fi642v;
  FunctionType *fi642vp, *fi64i642vp, *fvpi642vp;
  FunctionType *fvpdisp, *fvpi643vp;

  Constant *getAlloc(StringRef suffix)
  {
    std::vector<Type*> formals(2);
    formals[0] = u32; // size
    formals[1] = u8;  // sub-heap
    FunctionType *fty = FunctionType::get(voidptr, formals, false);

    std::string name = ( Twine(personality) + "_alloc_" + suffix ).str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getAllocRedux()
  {
    std::vector<Type*> formals(7);
    formals[0] = u32; // size
    formals[1] = u8;  // sub-heap
    formals[2] = u8;  // redux type
    formals[3] = u8;  // is register redux?
    formals[4] = voidptr; // dep au
    formals[5] = u32; // dep size
    formals[6] = u8; // dep type

    FunctionType *fty = FunctionType::get(voidptr, formals, false);
    std::string name = (Twine(personality) + "_alloc_redux").str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

  Constant *getFree(StringRef suffix)
  {
    std::vector<Type*> formals(1);
    formals[0] = voidptr;
    FunctionType *fty = FunctionType::get(voidty, formals, false);

    std::string name = ( Twine(personality) + "_free_" + suffix ).str();
    FunctionCallee wrapper = mod->getOrInsertFunction(name,fty);
    return cast<Constant>(wrapper.getCallee());
  }

};

}
}

#endif

