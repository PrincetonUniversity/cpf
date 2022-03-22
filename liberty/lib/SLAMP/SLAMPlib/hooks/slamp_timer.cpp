#include <cstdint>
#include <string>
#include <fstream>
#include "slamp_timer.h"

uint64_t rdtsc() {

  uint32_t a, d;
  // __asm__ volatile("rdtsc" : "=a" (a), "=d" (d));
  __asm__ volatile("rdtscp" : "=a" (a), "=d" (d));
  return ((uint64_t)a) | (((uint64_t)d) <<32 );
}

uint64_t overhead_init_fini = 0;
uint64_t overhead_shadow_allocate = 0;
uint64_t overhead_shadow_write = 0;
uint64_t overhead_shadow_read = 0;
uint64_t overhead_log_total = 0;
uint64_t overhead_module_total = 0;
uint64_t overhead_out_of_path_extern_load = 0;
uint64_t overhead_extern_wrapper = 0;

void slamp_time_dump(std::string fname){
#ifdef PERFORMANCE_ANALYSIS
  std::ofstream of(fname, std::ios::app);

  // module_total is a part of log total
  uint64_t total_overhead = overhead_init_fini + 
    overhead_shadow_allocate + overhead_shadow_read +
    overhead_shadow_write + overhead_log_total +
    overhead_extern_wrapper + overhead_out_of_path_extern_load;

  of << "Total Overhead:\t"  << total_overhead << "\n"
     << "Overhead Init Fini:\t" <<  overhead_init_fini << "\n"
     << "Overhead Shadow Allocate:\t" <<  overhead_shadow_allocate << "\n"
     << "Overhead Shadow Write:\t" <<  overhead_shadow_write << "\n"
     << "Overhead Shadow Read:\t" << overhead_shadow_read << "\n"
     << "Overhead Log Total:\t" << overhead_log_total << "\n"
     << "Overhead Module Total:\t" << overhead_module_total << "\n"
     << "Overhead Out-Of-Path Extern Load:\t" << overhead_out_of_path_extern_load << "\n"
     << "Overhead Extern Wrapper:\t" << overhead_extern_wrapper << "\n";

  of.close();
#endif
}
