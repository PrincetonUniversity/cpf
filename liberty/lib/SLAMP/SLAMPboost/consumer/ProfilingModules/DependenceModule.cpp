#include <cstdint>
#include <set>
#include <unordered_set>
#include <fstream>

#include "slamp_timestamp.h"
#include "slamp_logger.h"
#include "slamp_shadow_mem.h"
#include "DependenceModule.h"
#define SIZE_8M  0x800000


static slamp::MemoryMap* smmap = nullptr;
static std::unordered_set<slamp::KEY, slamp::KEYHash, slamp::KEYEqual> *deplog_set;
static uint64_t slamp_iteration = 0;
static uint64_t slamp_invocation = 0;
static uint32_t target_loop_id = 0;

namespace DepMod {
// init: setup the shadow memory
void init(uint32_t loop_id, uint64_t pid) {

  target_loop_id = loop_id;
  smmap = new slamp::MemoryMap(TIMESTAMP_SIZE_IN_BYTES);
  deplog_set = new std::unordered_set<slamp::KEY, slamp::KEYHash, slamp::KEYEqual>();

  smmap->init_stack(SIZE_8M, pid);
  smmap->allocate((void*)&errno, sizeof(errno));
  smmap->allocate((void*)&stdin, sizeof(stdin));
  smmap->allocate((void*)&stdout, sizeof(stdout));
  smmap->allocate((void*)&stderr, sizeof(stderr));
  smmap->allocate((void*)&sys_nerr, sizeof(sys_nerr));

  {
    const unsigned short int* ctype_ptr = (*__ctype_b_loc()) - 128;
    smmap->allocate((void*)ctype_ptr, 384 * sizeof(*ctype_ptr));
  }
  {
    const int32_t* itype_ptr = (*__ctype_tolower_loc()) - 128;
    smmap->allocate((void*)itype_ptr, 384 * sizeof(*itype_ptr));
  }
  {
    const int32_t* itype_ptr = (*__ctype_toupper_loc()) - 128;
    smmap->allocate((void*)itype_ptr, 384 * sizeof(*itype_ptr));
  }
}

void fini(const char *filename) {
  std::ofstream of(filename);
  of << target_loop_id << " " << 0 << " " << 0 << " "
       << 0 << " " << 0 << " " << 0 << "\n";

  std::set<slamp::KEY, slamp::KEYComp> ordered(deplog_set->begin(), deplog_set->end());
  for (auto &k: ordered) {
    of << target_loop_id << " " << k.src << " " << k.dst << " " << k.dst_bare << " "
       << (k.cross ? 1 : 0) << " " << 1 << " ";
    of << "\n";
  }

  delete smmap;
  delete deplog_set;
}

void allocate(void *addr, uint64_t size) {
  smmap->allocate(addr, size);
  // std::cout << "allocate " << addr << " " << size << std::endl;
}

// void log(TS ts, const uint32_t dst_inst, TS *pts, const uint32_t bare_inst,
         // uint64_t  addr, uint64_t  value, uint8_t  size) {
void log(TS ts, const uint32_t dst_inst, const uint32_t bare_inst){ 

    uint32_t src_inst = GET_INSTR(ts);
    uint64_t src_iter = GET_ITER(ts);

    // uint64_t src_invoc = GET_INVOC(ts);

    slamp::KEY key(src_inst, dst_inst, bare_inst, src_iter != slamp_iteration);
    
    // std::cout << "src_inst: " << src_inst << " dst_inst: " << dst_inst << " bare_inst: " << bare_inst << " src_iter: " << src_iter << " slamp_iteration: " << slamp_iteration << " src_iter != slamp_iteration: " << (src_iter != slamp_iteration) << std::endl;
    deplog_set->insert(key);
}

// template <unsigned size>
void load(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value){
  TS* s = (TS*)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  TS tss = s[0];
  if (tss != 0) {
    log(tss, instr, bare_instr);
  }
}

// template <unsigned size>
void store(uint32_t instr, uint32_t bare_instr, const uint64_t addr) {
  TS *s = (TS *)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  if (!smmap->is_allocated(s)) {
    // std::cout << "store not allocated: " << instr << " " << bare_instr << " " << addr << std::endl;

  }
  TS ts = CREATE_TS(instr, slamp_iteration, slamp_invocation);

  // TODO: handle output dependence. ignore it as of now.
  // if (ASSUME_ONE_ADDR) {
  s[0] = ts;
  // } else {
    // for (auto i = 0; i < size; i++)
      // s[i] = ts;
  // }
}

void loop_invoc() {
  slamp_iteration = 0;
  slamp_invocation++;
}

void loop_iter() {
  slamp_iteration++;
}
} // namespace DepMod
