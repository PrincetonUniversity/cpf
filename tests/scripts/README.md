Scripts to automate profiling and experiment runs.

## File Description

Main Scripts:

- aa : Alias analysis passes used
- collaborative-pipeline : Read profiling results, parallelize the code and generate binaries for parallel execution. Also generate dump file that shows interval steps and statistics
- devirtualize : Do devirtualization (for indirect function call)
- regressions-watchdog : A watchdog that limits time and memory usage of a profiling /experiment instance
- lamp-profile : Do loop-aware memory profiling (LAMP) profiling
- loop-profile : Do Loop profiling (execution time of loops and function calls)
- specpriv-profile : Do value prediction, points-to and short-lived objects (object that live only for one loop iteration) profiling
