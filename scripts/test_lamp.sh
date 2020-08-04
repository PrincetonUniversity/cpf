cp benchmark.lamp.out result.lamp.profile
opt benchmark.bc -load ${LIBERTY_LIBS_DIR}/libUtil.so -load ${LIBERTY_LIBS_DIR}/libExclusions.so -load ${LIBERTY_LIBS_DIR}/libAnalysis.so -load ${LIBERTY_LIBS_DIR}/libLoopProf.so -load ${LIBERTY_LIBS_DIR}/libMetadata.so -load ${LIBERTY_LIBS_DIR}/libLAMPLoad.so -load ${LIBERTY_LIBS_DIR}/libSLAMP.so -load ${LIBERTY_LIBS_DIR}/libRedux.so -load ${LIBERTY_LIBS_DIR}/libPointsToProfiler.so -debug-only=lamp-load -lamp-load-profile
rm result.lamp.profile
