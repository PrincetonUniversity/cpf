##===- projects/sample/lib/Makefile ------------------------*- Makefile -*-===##

#
# Relative path to the top of the source tree.
#
LEVEL=..

#
# List all of the subdirectories that we will compile.
#

CHAR=
ifdef SYSCHAR_INCLUDE_DIR
  CHAR=Characterization
endif

DIRS= SLAMP LAMP Analysis Utilities \
LoopProf LAMPLoad Metadata Devirt Priv PointsToProfiler \
Exclusions

include $(LEVEL)/Makefile.common
