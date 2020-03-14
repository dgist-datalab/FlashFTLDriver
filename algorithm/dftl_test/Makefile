CC = g++

CFLAGS_ALGO +=\

SRCS +=\
	   dftl.c\
	   garbage_collection.c\
	   dftl_utils.c\
	   lru_list.c\

OBJS :=\
	$(patsubst %.c,%.o,$(SRCS))\

OBJS+=skiplist.o

TARGETOBJ:=\
	$(addprefix ../../object/,$(OBJS))\

LEAK: all
all: $(TARGETOBJ) 

../../object/skiplist.o: ../Lsmtree/skiplist.c
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

libnom.a : $(TARGETOBJ)
	$(AR) r $(@) $(TARGETOBJ)

.c.o :
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

../../object/%.o: %.c
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

clean : 
	@$(RM) libnom.a
	@$(RM) *.o
