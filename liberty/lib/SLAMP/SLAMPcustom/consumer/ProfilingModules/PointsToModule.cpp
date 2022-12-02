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

bool in_func5 = false;

void PointsToModule::allocate(void *addr, uint32_t instr, uint64_t size) {
  // FIXME: is it leagal
  void* shadow = smmap->allocate(addr, size + 1);
  // void* shadow = smmap->allocate(addr, size);

  TS *s = (TS *)shadow;
  // log all data into sigle TS
  // FIXME: static instruction and the dynamic context?
  // context: static instr + function + loop
  auto hash = contextManager.encodeActiveContext();
  // print active context
  // contextManager->activeContext->print(std::cerr);

  // currentContext->print(std::cerr);
  // std::cerr << "malloc hash: " << hash << "\n";

  // TS ts = CREATE_TS(instr, hash, __slamp_invocation);
  TS ts = CREATE_TS_HASH(instr, hash, slamp_iteration, slamp_iteration);

  //8 bytes per byte TODO: can we reduce this?
  for (auto i = 0; i < size; i++) {
    // FIXME: make it more compact
    local_write((uint64_t)addr + i, [&](){
      s[i] = ts;
    });
  }

  // FIXME: is this legal?
  // Guard the last byte out of the range
  local_write((uint64_t)addr + size, [&](){
    s[size] = ts;
  });

}

void PointsToModule::free(void *addr) {
}

// manage context
void PointsToModule::func_entry(uint32_t fcnId) {
  if (fcnId == 5) {
    in_func5 = true;
  }

  auto contextId = ContextId(FunctionContext, fcnId);
  contextManager.pushContext(contextId);
}

void PointsToModule::func_exit(uint32_t fcnId) {
  if (fcnId == 5) {
    in_func5 = false;
  }
  auto contextId = ContextId(FunctionContext, fcnId);
  contextManager.popContext(contextId);
}

void PointsToModule::loop_entry(uint32_t loopId) {
  auto contextId = ContextId(LoopContext, loopId);
  contextManager.pushContext(contextId);

  if (loopId == target_loop_id) {
    in_loop = true;
  }
}

void PointsToModule::loop_exit(uint32_t loopId) {
  auto contextId = ContextId(LoopContext, loopId);
  contextManager.popContext(contextId);
  if (loopId == target_loop_id) {
    in_loop = false;
  }
}


void PointsToModule::loop_invoc() {
  targetLoopContexts.emplace(contextManager.encodeActiveContext());
  slamp_iteration = 0;
  slamp_invocation++;
}

void PointsToModule::loop_iter() { slamp_iteration++; }

void PointsToModule::points_to_arg(uint32_t fcnId, uint32_t argId, void *ptr) {
  local_write((uint64_t)ptr, [&]() {
    auto instr = FORMAT_INST_ARG(fcnId, argId);
    auto contextHash = contextManager.encodeActiveContext();
    uint64_t instrAndHash = ((uint64_t)instr << 32) | contextHash;
    if (ptr == nullptr) {
      pointsToMap[instrAndHash].insert(0);
      // pointsToMap.emplace(std::make_pair(instrAndHash, 0));
      return;
    }
    if (ptr < (void *)0x1000) {
      // Protect against null pointers
      // If it gets dereferenced, it will be caught by segfault
      return;
    }
    TS *s = (TS *)GET_SHADOW(ptr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
    TS ts;
    // if (!smmap->is_allocated(ptr))
    // return;
    ts = s[0];

    if (ts != 0) {
      // mask off the iteration count
      ts = ts & 0xffffffffffffff00;
      // ts = ts & 0xfffffffffffffff0;
      // ts = ts & 0xfffff0000000000f;
      // create set of objects for each load/store
      pointsToMap[instrAndHash].insert(ts);
      // pointsToMap.emplace(std::make_pair(instrAndHash, ts));
    }
  });
}

void PointsToModule::points_to_inst(uint32_t instId, void *ptr) {
  local_write((uint64_t)ptr, [&]() {
    auto instr = FORMAT_INST_INST(instId);
    auto contextHash = contextManager.encodeActiveContext();
    uint64_t instrAndHash = ((uint64_t)instr << 32) | contextHash;
    if (ptr == nullptr) {
      pointsToMap[instrAndHash].insert(0);
      // pointsToMap.emplace(std::make_pair(instrAndHash, 0));
      return;
    }
    if (ptr < (void *)0x1000) {
      // Protect against null pointers
      // If it gets dereferenced, it will be caught by segfault
      return;
    }
    TS *s = (TS *)GET_SHADOW(ptr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
    TS ts;

    // not all pointers are well-defined
    // if (!smmap->is_allocated(ptr))
    // return;
    ts = s[0];

    if (ts != 0) {
      // mask off the iteration count
      ts = ts & 0xffffffffffffff00;
      // ts = ts & 0xfffffffffffffff0;
      // ts = ts & 0xfffff0000000000f;
      // create set of objects for each load/store
      pointsToMap[instrAndHash].insert(ts);
      // pointsToMap.emplace(std::make_pair(instrAndHash, ts));
    }
  });
}

void PointsToModule::init(uint32_t loop_id, uint32_t pid) {
  target_loop_id = loop_id;
#define SIZE_8M  0x800000
  smmap->init_stack(SIZE_8M, pid);
}

void PointsToModule::fini(const char *filename) {
  // Dump compatible one as SpecPriv output
  std::ofstream specprivfs(filename);
  specprivfs << "BEGIN SPEC PRIV PROFILE\n";
  specprivfs << "COMPLETE ALLOCATION INFO ; \n";


  // print all loop contexts
  specprivfs << "LOOP CONTEXTS: " << targetLoopContexts.size() << "\n";
  for (auto contextHash : targetLoopContexts) {
    contextManager.printContext(specprivfs, contextHash);
    specprivfs << "\n";
  }

  // predict OBJ
  //  PRED OBJ main if.else.i $0 AT  CONTEXT { LOOP main for.cond15 1 WITHIN
  //  FUNCTION main WITHIN TOP }  AS PREDICTABLE 300 SAMPLES OVER 1 VALUES {  (
  //  OFFSET 0 BASE AU HEAP allocate_matrices for.end call7 FROM  CONTEXT {
  //  FUNCTION allocate_matrices WITHIN FUNCTION main WITHIN TOP }  COUNT 300 )
  //  } ;

  // // get ordered key from the map
  // std::vector<uint64_t> keys;
  // for (auto &kv : pointsToMap) {
  //   keys.push_back(kv.first);
  // }
  // std::sort(keys.begin(), keys.end());

  // for (auto &key : keys) {
  //   auto v = pointsToMap[key];
  //   auto instr = key  >> 32;
  //   auto instrHash = key & 0xFFFFFFFF;
  //   std::vector<SpecPrivLib::ContextId> instrContext =
  //       contextManager.decodeContext(instrHash);
  //   specprivfs << "PRED OBJ " << instr << " at " << instrHash << " ";
  //   printContext(instrContext);
  //   specprivfs << ": " << v.size() << "\n"; // instruction ID
  //   for (auto &it2 : v) { // the set of allocation units
  //     auto hash = GET_HASH(it2);

  //     specprivfs << "AU ";
  //     if (it2 == 0xffffffffffffff00) {
  //       specprivfs << " UNMANAGED";
  //     } else if (it2 == 0) {
  //       specprivfs << " NULL";
  //     } else {
  //       std::vector<SpecPrivLib::ContextId> context =
  //           contextManager.decodeContext(hash);

  //       specprivfs << GET_INSTR(it2);
  //       specprivfs << " FROM CONTEXT " << instrHash << " ";
  //       printContext(context);
  //     }
  //     specprivfs << ";\n";
  //   }
  // }

  auto printContext = [&](std::vector<ContextId> context) {
    for (auto it = context.rbegin(); it != context.rend(); it++) {
      auto &c = *it;
      c.print(specprivfs);
    }
  };

  for (auto &kv : decodedContextMap) {
    auto ptrAndContext = kv.first;
    auto instr = ptrAndContext.first;
    auto context = ptrAndContext.second;

    specprivfs << "PRED OBJ " << instr << " at ";
    printContext(context);
    auto auSet = kv.second;
    specprivfs << ": " << auSet.size() << "\n"; // instruction ID
    for (auto &[au, context] : auSet) {
      specprivfs << "AU ";
      if (au == -2) {
        specprivfs << " UNMANAGED";
      } else if (au == -1) {
        specprivfs << " NULL";
      } else {
        specprivfs << au;
        specprivfs << " FROM CONTEXT ";
        printContext(context);
      }
      specprivfs << ";\n";
    }
  }

  specprivfs << " END SPEC PRIV PROFILE\n";
}

void PointsToModule::decode_all() {
  // convert it to decodedContextMap
  for (auto &it : pointsToMap) {
    auto instr = it.first  >> 32;
    auto instrHash = it.first & 0xFFFFFFFF;
    std::vector<ContextId> instrContext =
        contextManager.decodeContext(instrHash);
    InstrAndContext instrAndContext = {instr, instrContext};

    for (auto &it2 : it.second) { // the set of allocation units
      auto hash = GET_HASH(it2);
      if (it2 == 0xffffffffffffff00) {
        // insert -2, empty
        decodedContextMap[instrAndContext].insert({-2, {}});
      } else if (it2 == 0) {
        // insert -1, empty
        decodedContextMap[instrAndContext].insert({-1, {}});
      } else {
        std::vector<ContextId> context =
            contextManager.decodeContext(hash);
        decodedContextMap[instrAndContext].insert({GET_INSTR(it2), context});
      }
    }
  }
}

void PointsToModule::merge(PointsToModule &other) {
  other.decode_all();

  for (auto &it : other.decodedContextMap) {
    auto instrAndContext = it.first;
    if (decodedContextMap.find(instrAndContext) == decodedContextMap.end()) {
      // not found, insert
      decodedContextMap[instrAndContext] = it.second;
    } else {
      // found, merge
      for (auto &it2 : it.second) {
        decodedContextMap[instrAndContext].insert(it2);
      }
    }
  }

  // // FIXME: this won't work because the context map is different
  // for (auto &it : other.pointsToMap) {
  //   if (pointsToMap.find(it.first) == pointsToMap.end()) {
  //     pointsToMap[it.first] = it.second;
  //   } else {
  //     pointsToMap[it.first].merge(it.second);
  //   }
  // }
}
