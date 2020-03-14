
CFLAGS_ALGO +=\

SRCS +=\
		demand.c\
		rw.c\
		page.c\
		range.c\
		dvalue.c\
		dvalue_gc.c\
		utility.c\
		cache.c\
		fine.c\
		coarse.c\
		d_htable.c\

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
