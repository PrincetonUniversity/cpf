#include "PointsToModule.h"
#include "context.h"
#include "slamp_timestamp.h"

// Points-to module
// Requires events:
//  - Stack alloca
//  - Stack free?
//  - [x] Head allocation and free
//  - [x] Target loop invocation, iteration
//  - [x] Loop entry, exit
//  - [x] Fcn entry, exit
//  - Report base
//  - init, fini
#define  FORMAT_INST_ARG(fcnId, argId) (fcnId << 5 | (0x1f & (argId  << 4) | 0x1))
#define FORMAT_INST_INST(instId) (instId << 1 | 0x0)

void PointsToModule::allocate(void *addr, uint32_t instr, uint64_t size) {
  void* shadow = smmap->allocate(addr, size);

  TS *s = (TS *)shadow;
  // log all data into sigle TS
  // FIXME: static instruction and the dynamic context?
  // context: static instr + function + loop
  auto hash = contextManager->encodeActiveContext();
  // print active context
  // contextManager->activeContext->print(std::cerr);

  // currentContext->print(std::cerr);
  // std::cerr << "malloc hash: " << hash << "\n";

  // TS ts = CREATE_TS(instr, hash, __slamp_invocation);
  TS ts = CREATE_TS_HASH(instr, hash, slamp_iteration, slamp_iteration);

  //8 bytes per byte TODO: can we reduce this?
  for (auto i = 0; i < size; i++)
    s[i] = ts;
}

void PointsToModule::free(void *addr) {
  if (addr == nullptr)
    return;
  // if we are still in the loop and the iteration is the same, mark it as local
  // otherwise mark it as not local
  TS *s = (TS *)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  // auto instr = GET_INSTR(s[0]);
  // auto hash = GET_HASH(s[0]);
  auto iteration = GET_INVOC(s[0]) & 0xF0 >> 4;
  auto invocation = GET_INVOC(s[0]) & 0xF;

  auto instrAndHash = s[0] & 0xFFFFFFFFFFFFFF00;

  // if (instr == 0) {
  // std::cerr << "TS: " << s[0] << " ptr: " << addr << "\n";
  // }

  if (iteration == (0xf & slamp_iteration) &&
      invocation == (0xf & slamp_invocation) &&
      in_loop == true) {
    //  FIXME: if invokedepth is 0, it means we are not in a loop
    // && invokedepth > 0) {
    // is short-lived, put in the set
    shortLivedObjects->insert(instrAndHash);
    // for (auto &obj : *shortLivedObjects) {
      // std::cerr << "short lived object: " << obj << "\n";
    // }
  } else {
    // is not short-lived
    longLivedObjects->insert(instrAndHash);
  }
}

// manage context
void PointsToModule::func_entry(uint32_t fcnId) {
  auto contextId = SpecPrivLib::ContextId(SpecPrivLib::FunctionContext, fcnId);
  contextManager->updateContext(contextId);
}

void PointsToModule::func_exit(uint32_t fcnId) {
  auto contextId = SpecPrivLib::ContextId(SpecPrivLib::FunctionContext, fcnId);
  contextManager->popContext(contextId);
}

void PointsToModule::loop_entry(uint32_t loopId) {
  auto contextId = SpecPrivLib::ContextId(SpecPrivLib::LoopContext, loopId);
  contextManager->updateContext(contextId);

  if (loopId == target_loop_id) {
    in_loop = true;
  }
}

void PointsToModule::loop_exit(uint32_t loopId) {
  auto contextId = SpecPrivLib::ContextId(SpecPrivLib::LoopContext, loopId);
  contextManager->popContext(contextId);
  if (loopId == target_loop_id) {
    in_loop = false;
  }
}


void PointsToModule::loop_invoc() {
  targetLoopContexts->emplace(contextManager->encodeActiveContext());
  slamp_iteration = 0;
  slamp_invocation++;
}

void PointsToModule::loop_iter() { slamp_iteration++; }

void PointsToModule::points_to_arg(uint32_t fcnId, uint32_t argId, void *ptr) {

  auto instr = FORMAT_INST_ARG(fcnId, argId);
  auto contextHash = contextManager->encodeActiveContext();
  uint64_t instrAndHash = ((uint64_t)instr << 32) | contextHash;
  if (ptr == nullptr) {
    (*pointsToMap)[instrAndHash].insert(0);
    return;
  }
  TS* s = (TS*)GET_SHADOW(ptr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  TS ts;
  if (!smmap->is_allocated(ptr))
    return;
  ts = s[0];

  if (ts != 0) {
    // mask off the iteration count
    ts = ts & 0xffffffffffffff00;
    // ts = ts & 0xfffffffffffffff0;
    // ts = ts & 0xfffff0000000000f;
    //create set of objects for each load/store
    (*pointsToMap)[instrAndHash].insert(ts);
  }
}

void PointsToModule::points_to_inst(uint32_t instId, void *ptr) {
  auto instr = FORMAT_INST_INST(instId);
  auto contextHash = contextManager->encodeActiveContext();
  uint64_t instrAndHash = ((uint64_t)instr << 32) | contextHash;
  if (ptr == nullptr) {
    (*pointsToMap)[instrAndHash].insert(0);
    return;
  }
  TS* s = (TS*)GET_SHADOW(ptr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  TS ts;

  // not all pointers are well-defined
  if (!smmap->is_allocated(ptr))
    return;
  ts = s[0];

  if (ts != 0) {
    // mask off the iteration count
    ts = ts & 0xffffffffffffff00;
    // ts = ts & 0xfffffffffffffff0;
    // ts = ts & 0xfffff0000000000f;
    //create set of objects for each load/store
    (*pointsToMap)[instrAndHash].insert(ts);
  }
}


void PointsToModule::init(uint32_t loop_id, uint32_t pid) {
  target_loop_id = loop_id;
#define SIZE_8M  0x800000
  smmap->init_stack(SIZE_8M, pid);
}

// TODO: implement dumping
void PointsToModule::fini(const char *filename) {
    // // dump out the points-to map
    // std::ofstream ofs(filename);
    // if (ofs.is_open()) {
    //   ofs << "Points-to map\n";
    //   for (auto &it : *pointsToMap) {
    //     ofs << it.first << ": "; // instruction ID
    //     for (auto &it2 : it.second) { // the set of allocation units
    //       auto hash = GET_HASH(it2);
    //       ofs << "instr - "<< GET_INSTR(it2) << "\n"; // << " iter - " << GET_ITER(it2) << " invoc - " << GET_INVOC(it2) << "\n";
    //     }
    //     ofs << "\n";
    //   }
    //   ofs << "Short-lived object: " << shortLivedObjects->size() << "\n";
    //   for (auto &obj: *shortLivedObjects) {
    //     // if short-lived
    //     ofs << obj << "\n";
    //   }

    //   ofs << "Long-lived object: " << longLivedObjects->size() << "\n";
    //   for (auto &obj: *longLivedObjects) {
    //     // if long-lived
    //     ofs << obj << "\n";
    //   }

    //   ofs.close();
    // }

  // Dump compatible one as SpecPriv output
  std::ofstream specprivfs(filename);
  specprivfs << "BEGIN SPEC PRIV PROFILE\n";
  specprivfs << "COMPLETE ALLOCATION INFO ; \n";

  auto printContext =
      [&specprivfs](const std::vector<SpecPrivLib::ContextId> &ctx) {
        for (auto &c : ctx) {
          specprivfs << "(" << c.type << "," << c.metaId << ")";
        }
      };

  // print all loop contexts
  specprivfs << "LOOP CONTEXTS: " << targetLoopContexts->size() << "\n";
  for (auto contextHash : *targetLoopContexts) {
    auto context = contextManager->decodeContext(contextHash);
    printContext(context);
    specprivfs << "\n";
  }

  // local objects
  for (auto &obj : *shortLivedObjects) {
    // LOCAL OBJECT AU HEAP main if.else.i call.i4.i FROM  CONTEXT { LOOP main
    // for.cond15 1 WITHIN FUNCTION main WITHIN TOP }  IS LOCAL TO  CONTEXT {
    // LOOP main for.cond15 1 WITHIN FUNCTION main WITHIN TOP }  COUNT 300 ;
    auto instr = GET_INSTR(obj);
    auto hash = GET_HASH(obj);
    auto context = contextManager->decodeContext(hash);
    specprivfs << "LOCAL OBJECT " << instr << " at context ";
    printContext(context);
    specprivfs << ";\n";
  }

  // predict OBJ
  //  PRED OBJ main if.else.i $0 AT  CONTEXT { LOOP main for.cond15 1 WITHIN FUNCTION main WITHIN TOP }  AS PREDICTABLE 300 SAMPLES OVER 1 VALUES {  ( OFFSET 0 BASE AU HEAP allocate_matrices for.end call7 FROM  CONTEXT { FUNCTION allocate_matrices WITHIN FUNCTION main WITHIN TOP }  COUNT 300 )  } ;
    for (auto &it : *pointsToMap) {
      auto instr = it.first >> 32;
      auto instrHash = it.first & 0xFFFFFFFF;
      std::vector<SpecPrivLib::ContextId> instrContext = contextManager->decodeContext(instrHash);
      specprivfs << "PRED OBJ " << instr << " at ";
      printContext(instrContext);
      specprivfs << ": " << it.second.size() << "\n"; // instruction ID
      for (auto &it2 : it.second) { // the set of allocation units
        auto hash = GET_HASH(it2);

        specprivfs << "AU "; 
        if (it2 == 0xffffffffffffff00) {
          specprivfs << " UNMANAGED";
        }
        else if (it2 == 0) {
          specprivfs << " NULL";
        } else {
          std::vector<SpecPrivLib::ContextId> context = contextManager->decodeContext(hash);

          specprivfs <<GET_INSTR(it2);
          specprivfs << " FROM CONTEXT " ;
          printContext(context);
        }
        specprivfs << ";\n";
      }
    }


  specprivfs << " END SPEC PRIV PROFILE\n";

}

