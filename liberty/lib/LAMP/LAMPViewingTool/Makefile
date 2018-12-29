#---------------------------------------------------------------------
# Macros
#---------------------------------------------------------------------

CC = gcc

CCFLAGS = -O3

#---------------------------------------------------------------------
# Build rules for non-file targets
#---------------------------------------------------------------------

all: LAMPDirectory LAMPDirByLine LAMPDumpLoop LAMPDumpLoopLines

clean:
	rm -f *.o LAMPDirectory LAMPDirByLine LAMPDirByLoop

#---------------------------------------------------------------------
# Build rules for executables
#---------------------------------------------------------------------

LAMPDirectory: LAMPDirectory.o lookup3.o
	$(CC) $(CCFLAGS) LAMPDirectory.o lookup3.o -o LAMPDirectory
LAMPDirByLine: LAMPDirByLine.o lookup3.o
	$(CC) $(CCFLAGS) LAMPDirByLine.o lookup3.o -o LAMPDirByLine
LAMPDumpLoop: LAMPDirByLoop.o lookup3.o
	$(CC) $(CCFLAGS) LAMPDirByLoop.o lookup3.o -o LAMPDumpLoop
LAMPDumpLoopLines: LAMPDirByLoopLO.o lookup3.o
	$(CC) $(CCFLAGS) LAMPDirByLoopLO.o lookup3.o -o LAMPDumpLoopLines
LAMPDumpLoopLCDCount: LAMPDirGetLCDCount.o lookup3.o
	$(CC) $(CCFLAGS) LAMPDirGetLCDCount.o lookup3.o -o LAMPDumpLoopLCDCount

#---------------------------------------------------------------------
# Build rules for object files
#---------------------------------------------------------------------

LAMPDirectory.o: LAMPDirectory.c
	$(CC) $(CCFLAGS) -c LAMPDirectory.c

LAMPDirByLine.o: LAMPDirByLine.c
	$(CC) $(CCFLAGS) -c LAMPDirByLine.c

LAMPDirByLoop.o: LAMPDirByLoop.c
	$(CC) $(CCFLAGS) -c LAMPDirByLoop.c

LAMPDirByLoopLO.o: LAMPDirByLoopLO.c
	$(CC) $(CCFLAGS) -c LAMPDirByLoopLO.c

LAMPDirGetLCDCount.o: LAMPDirGetLCDCount.c
	$(CC) $(CCFLAGS) -c LAMPDirGetLCDCount.c

lookup3.o: lookup3.c
	$(CC) $(CCFLAGS) -c lookup3.c


