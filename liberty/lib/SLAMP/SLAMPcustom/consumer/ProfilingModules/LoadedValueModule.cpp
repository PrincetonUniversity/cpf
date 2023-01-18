#include "LoadedValueModule.h"
#include <utility>

void LoadedValueModule::init(uint32_t loop_id, uint32_t pid) {
}

void LoadedValueModule::fini(const char *filename) {
  std::ofstream specprivfs(filename);
  specprivfs << "BEGIN SPEC PRIV PROFILE\n";
  for (auto &[key, cp] : constmap_value) {
    if (cp != constmap_value.MAGIC_INVALID) {
      // instr and value
      auto instr = key.first;
      // auto value = cp->value;
      auto value = cp;
      // later, we need to parse the instruction to see if it's a pointer or a
      // regular integer
      specprivfs << "PRED VAL " << instr << " " << value << " ; \n";
    }
  }

  specprivfs << " END SPEC PRIV PROFILE\n";
  specprivfs.close();
}

void LoadedValueModule::load(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value, uint8_t size) {
  local_write(instr, [&]() {
    AccessKey key(instr, bare_instr);
    constmap_value.emplace(std::make_pair(key, value));
  });
  // if (constmap_value.count(key) != 0) {
  // auto cp = constmap_value[key];

  // if (cp->valid) {
  // if (cp->value != value) {
  // cp->valid = false;
  // }
  // }
  // // // Remove check for constant need to have the same address
  // // if (cp->valueinit && cp->addr != addr)
  // //   cp->valid = false;
  // // if (cp->valid) {
  // //   if (cp->valueinit && cp->value != value) {
  // //     cp->valid = false;
  // //   }
  // //   else {
  // //     cp->valueinit = true;
  // //     cp->value = value;
  // //     cp->addr = addr;
  // //   }
  // // }
  // } else {
    // auto cp = new Constant(true, value);
    // constmap_value[key] = cp;
    // // auto cp = new Constant(true, true, size, addr, value);
    // // constmap_value->insert(std::make_pair(key, cp));
  // }
}

void LoadedValueModule::merge_values(LoadedValueModule &other) {
  constmap_value.merge(other.constmap_value);
}
