# Define the projects to be built

DEMO = rwtest

# include common makefile options

include makeopt

CXXSRC = $(SRC)/main.cpp	\
	$(SRC)/Pad.cpp		\
	$(SRC)/FileMgr.cpp		\

CSRC = $(SRC)/camera.c	\
	$(SRC)/skyfs.c		\

#CSRC += $(SKELSRC)

ifeq ($(RWMETRICS), 1)
CSRC += $(SKEL)/vecfont.c   \
        $(SKEL)/metrics.c
endif

RWLIBS += $(RWLIBDIR)/$(LP)rtcharse.$(L) \
	$(RWLIBDIR)/$(LP)rpworld.$(L)	\
	$(RWLIBDIR)/$(LP)rwcore.$(L)

SYSLIBS += $(SCELIBDIR)/libcdvd.a

include maketarg


