#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <thread>
#include <unordered_set>
#include <vector>

#include "DependenceModule.h"
#include "slamp_logger.h"
#include "slamp_shadow_mem.h"
#include "slamp_timestamp.h"

// static std::map<uint32_t, uint64_t> *inst_count;
static inline uint64_t rdtsc() {
  uint64_t a, d;
  __asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
  return (d << 32) | a;
}

// init: setup the shadow memory
void DependenceModule::init(uint32_t loop_id, uint32_t pid) {
  target_loop_id = loop_id;

#define SIZE_8M  0x800000
  smmap->init_stack(SIZE_8M, pid);
  // inst_count = new std::map<uint32_t, uint64_t>();

}

static uint64_t log_time = 0;


void DependenceModule::fini(const char *filename) {
  std::cout << "Load count: " << load_count << std::endl;
  std::cout << "Store count: " << store_count << std::endl;

  // std::ofstream of(filename);
  // of << target_loop_id << " " << 0 << " " << 0 << " "
       // << 0 << " " << 0 << " " << 0 << "\n";

  // std::set<slamp::KEY, slamp::KEYComp> ordered(dep_set.begin(), dep_set.end());
  // for (auto &k: ordered) {
    // of << target_loop_id << " " << k.src << " " << k.dst << " " << k.dst_bare << " "
       // << (k.cross ? 1 : 0) << " " << 1 << " ";
    // of << "\n";
  // }

  // std::cout << "Log time: " << log_time/ 2.6e9 << " s" << std::endl;

  // for (auto &i : *inst_count) {
    // of << target_loop_id << " " << i.first << " " << i.second << "\n";
  // }

  delete smmap;
}

void DependenceModule::allocate(void *addr, uint64_t size) {
  smmap->allocate(addr, size);
}

void DependenceModule::log(TS ts, const uint32_t dst_inst, const uint32_t bare_inst, const uint64_t load_invocation, const uint64_t load_iteration){

    uint32_t src_inst = GET_INSTR(ts);

    uint64_t src_invoc = GET_INVOC(ts);
    uint64_t src_iter = GET_ITER(ts);

    if (src_invoc != GET_INVOC(load_invocation)) {
      return;
    }

    slamp::KEY key(src_inst, dst_inst, bare_inst, src_iter != load_iteration);

    dep_set.emplace_back(key);
}

void DependenceModule::load(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value) {

  // // increase inst_count for this instr
  // if (inst_count->find(instr) == inst_count->end()) {
  //   inst_count->insert(std::make_pair(instr, 1));
  // } else {
  //   inst_count->at(instr)++;
  // }

  local_write(addr, [&]() {
    load_count++;
    TS *s = (TS *)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);

    TS tss = s[0];
    if (tss != 0) {
      // uint64_t start = rdtsc();
      log(tss, instr, instr, slamp_invocation, slamp_iteration);
      // uint64_t end = rdtsc();
      // log_time += end - start;
    }
  });
}

void DependenceModule::store(uint32_t instr, uint32_t bare_instr, const uint64_t addr)  {

  local_write(addr, [&]() {
    store_count++;
    TS ts = CREATE_TS(instr, slamp_iteration, slamp_invocation);
    TS *shadow_addr = (TS *)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
    shadow_addr[0] = ts;
  });
}

void DependenceModule::loop_invoc() {
  slamp_iteration = 0;
  slamp_invocation++;
}

void DependenceModule::loop_iter() {
  slamp_iteration++;
}
