
CC= g++
NAME = ../scc
BGR_NAME = ../scc_bgr
CORE_OBJS = my_work_queue.o scc_tarjan.o scc_color.o scc_trim1.o scc_fb_seq.o scc_post_analyze.o scc_trim2.o scc_fb_global.o scc_weak.o scc_fb_seq2.o scc_trim2_new.o
OBJS = scc_main.o $(CORE_OBJS)
INCDIR = ../gm_graph/inc
LIBDIR = ../gm_graph/lib
OUTDIR = ./
LIB = gmgraph
LFLAGS = -L$(LIBDIR) -l$(LIB)
OPT_FLAGS ?= -O3 -DNDEBUG
ARCH_FLAGS ?=
CFLAGS = $(OPT_FLAGS) $(ARCH_FLAGS) -I. -I$(INCDIR) -I. -fopenmp -std=gnu++0x -DGM_EDGE64
INCFILES = $(shell ls $(INCDIR))
H_FILES = $(addprefix $(INCDIR)/, $(INCFILES))

all : $(NAME)

bgr : $(BGR_NAME)

$(NAME) : $(OBJS) ../gm_graph/lib/libgmgraph.a
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@

$(BGR_NAME) : scc_bgr_main.o $(CORE_OBJS) ../gm_graph/lib/libgmgraph.a
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@

%.o:%.cc
	$(CC) $(CFLAGS) -c $^ -o $@

clean :
	\rm -f $(NAME) $(BGR_NAME) *.o
