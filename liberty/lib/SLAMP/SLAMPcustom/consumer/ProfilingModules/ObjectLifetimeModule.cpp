#include "ObjectLifetimeModule.h"
#include "slamp_timestamp.h"

void ObjectLifetimeModule::allocate(void *addr, uint32_t instr, uint64_t size) {
  void* shadow = smmap->allocate(addr, size);

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
  for (auto i = 0; i < size; i++)
    s[i] = ts;
}

void ObjectLifetimeModule::free(void *addr) {
  local_write((uint64_t)addr, [&](){
    if (addr == nullptr)
      return;
    // if we are still in the loop and the iteration is the same, mark it as
    // local otherwise mark it as not local
    TS *s = (TS *)GET_SHADOW_OL(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
    // auto instr = GET_INSTR(s[0]);
    // auto hash = GET_HASH(s[0]);
    auto iteration = GET_INVOC(s[0]) & 0xF0 >> 4;
    auto invocation = GET_INVOC(s[0]) & 0xF;

    auto instrAndHash = s[0] & 0xFFFFFFFFFFFFFF00;

    if (iteration == (0xf & slamp_iteration) &&
        invocation == (0xf & slamp_invocation) && in_loop == true) {
      shortLivedObjects.emplace(instrAndHash);
    } else {
      // is not short-lived
      longLivedObjects.emplace(instrAndHash);
    }
  });
}

// manage context
void ObjectLifetimeModule::func_entry(uint32_t fcnId) {
  auto contextId = ContextId(FunctionContext, fcnId);
  contextManager.pushContext(contextId);
}

void ObjectLifetimeModule::func_exit(uint32_t fcnId) {
  auto contextId = ContextId(FunctionContext, fcnId);
  contextManager.popContext(contextId);
}


void ObjectLifetimeModule::loop_invoc() {
  slamp_iteration = 0;
  slamp_invocation++;
  in_loop = true;
}

void ObjectLifetimeModule::loop_iter() { slamp_iteration++; }

void ObjectLifetimeModule::loop_exit() {
  in_loop = false;
}


void ObjectLifetimeModule::init(uint32_t loop_id, uint32_t pid) {
  target_loop_id = loop_id;
#define SIZE_8M  0x800000
  smmap->init_stack(SIZE_8M, pid);
}

// TODO: implement dumping
void ObjectLifetimeModule::fini(const char *filename) {
  // Dump compatible one as SpecPriv output
  std::ofstream specprivfs(filename);
  specprivfs << "BEGIN SPEC PRIV PROFILE\n";
  specprivfs << "COMPLETE ALLOCATION INFO ; \n";

  // local objects
  for (auto &obj : shortLivedObjects) {
    // LOCAL OBJECT AU HEAP main if.else.i call.i4.i FROM  CONTEXT { LOOP main
    // for.cond15 1 WITHIN FUNCTION main WITHIN TOP }  IS LOCAL TO  CONTEXT {
    // LOOP main for.cond15 1 WITHIN FUNCTION main WITHIN TOP }  COUNT 300 ;
    if (longLivedObjects.count(obj))
      continue;

    auto instr = GET_INSTR(obj);
    auto hash = GET_HASH(obj);
    specprivfs << "LOCAL OBJECT " << instr << " at context ";
    contextManager.printContext(specprivfs, hash);
    specprivfs << ";\n";
  }

  specprivfs << " END SPEC PRIV PROFILE\n";
}

