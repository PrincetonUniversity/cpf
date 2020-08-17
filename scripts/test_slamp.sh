cp benchmark.result.slamp.profile result.slamp.profile
opt benchmark.bc -load ${LIBERTY_LIBS_DIR}/libUtil.so -load ${LIBERTY_LIBS_DIR}/libExclusions.so -load ${LIBERTY_LIBS_DIR}/libAnalysis.so -load ${LIBERTY_LIBS_DIR}/libLoopProf.so -load ${LIBERTY_LIBS_DIR}/libMetadata.so -load ${LIBERTY_LIBS_DIR}/libLAMPLoad.so -load ${LIBERTY_LIBS_DIR}/libSLAMP.so -load ${LIBERTY_LIBS_DIR}/libRedux.so -load ${LIBERTY_LIBS_DIR}/libPointsToProfiler.so -debug-only=slamp-load -slamp-load-profile
rm result.slamp.profile
