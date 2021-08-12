INCDIR  = -I/usr/local/sce/common/include
DBGFLAG  = -g
#OPTFLAG  = -O2
CFLAGS   = -G0 $(INCDIR) -Wall $(DBGFLAG) $(OPTFLAG)
ASFLAGS  = $(DBGFLAG) 
LDFLAGS  = $(DBGFLAG)
LOADLIBES=
LDLIBS = -ilb=cdvdman.ilb

all: cdstream.irx

clean:
	rm -f *.o *.irx *.ilb *.map

cdstream.irx : cdstream.o cdstream.tbl
	$(ILBGEN) cdstream.tbl -e cdstreament.s
	$(AS) -o cdstreament.o cdstreament.s
#	rm -f cdstreament.s
	$(LINK.o) -e start cdstream.o cdstreament.o  \
		$(LOADLIBES) $(LDLIBS) -o $@

cdstream.ilb: cdstream.tbl
	$(ILBGEN) cdstream.tbl -d cdstream.ilb

#----------- rules --------------
%.irx: %.o
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@

-include PathDefs
PathDefs:
	iop-path-setup > PathDefs || (rm -f PathDefs ; exit 1)

