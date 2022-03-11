#include <cstdint>
#include <string>

#define PERFORMANCE_ANALYSIS 1

#if PERFORMANCE_ANALYSIS
#define TOUT(...)   do { __VA_ARGS__ ; } while(0)
#define TIME(v)     do { v = rdtsc() ; } while(0)
#define TADD(d,s)   do { d += rdtsc() - s; } while(0)
#else
#define TOUT(...)
#define TIME(v)     do { (void)v; } while(0)
#define TADD(d,s)   do { (void)d; (void)s; } while(0)

#endif

uint64_t rdtsc();

// overhead
extern uint64_t overhead_init_fini;
extern uint64_t overhead_shadow_allocate;
extern uint64_t overhead_shadow_write;
extern uint64_t overhead_shadow_read;
extern uint64_t overhead_log_total;
extern uint64_t overhead_module_total;
extern uint64_t overhead_out_of_path_extern_load;
extern uint64_t overhead_extern_wrapper;

void slamp_time_dump(std::string);

