SRCS +=\
	   bloomfilter.c\
	   bf_set.c\
	   compressed_bloomfilter.c\
	   guard_bf_set.c\

	
OBJS :=\
	$(patsubst %.c,%.o,$(SRCS))\

all: $(OBJS)


%.o :%.c
	$(CC) $(CFLAGS_ALGO) -c $< -o $@
