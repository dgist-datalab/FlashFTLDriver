SRCS+=\
	  nocpu_sftl_cache.cpp\

OBJS :=\
	   $(patsubst %.cpp, %.o, $(SRCS))\

all:$(OBJS)
	mv *.o ../../../../object/

.cpp.o:
	$(CC) $(CFLAGS_ALGO) -c $< -o $@
