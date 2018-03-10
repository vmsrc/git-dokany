ZCC=gcc

INCLUDES=-I./build/libgit2/include
LINKDIRS=-L./build/libgit2/lib

CCOPTS=-O3 -DNDEBUG -fPIC -c -Wall -std=gnu11 -Iv_lib $(INCLUDES) -MMD

LINKOPTS= -lfuse -lpthread -lz -lgit2 $(LINKDIRS)

BUILDDIR=build
TARGET=git-dokany

OBJDIR=$(BUILDDIR)/obj
ifeq (${wildcard $(BUILDDIR)},)
  DUMMY := ${shell mkdir $(BUILDDIR)}
endif
ifeq (${wildcard $(OBJDIR)},)
  DUMMY := ${shell mkdir $(OBJDIR)}
endif

.PHONY: all

all : $(BUILDDIR)/$(TARGET)

SRC:=

SRC+=../src/fusedgit.c
SRC+=../src/config.c
SRC+=../src/main.c
SRCPATH+=:../src
INCLUDES+=-I../src

SRC+=../src/v_lib/v_abq.c
SRC+=../src/v_lib/v_mem_pool.c
SRC+=../src/v_lib/v_thread.posix.c
SRCPATH+=:../src/v_lib
INCLUDES+=-I../src/v_lib

vpath %.c $(SRCPATH)

OBJ = $(addsuffix .o, $(addprefix $(OBJDIR)/, $(basename $(notdir $(SRC)))))

include $(wildcard $(OBJDIR)/*.d)

$(OBJDIR)/%.o: %.c
	$(ZCC) $(CCOPTS) $< -o $@

$(BUILDDIR)/$(TARGET): $(OBJ)
	$(ZCC) -o $(BUILDDIR)/$(TARGET) $(OBJ) $(LINKOPTS)

.PHONY: clean
clean:
	rm -f $(OBJDIR)/*
