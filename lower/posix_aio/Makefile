
SRCS +=\
	   posix.c\

OBJS :=\
	$(patsubst %.c,%.o,$(SRCS))\

TARGETOBJ:=\
	$(addprefix ../../object/,$(OBJS))\

DEBUG : all
all: $(TARGETOBJ)

libpos.a : $(TARGETOBJ)
	$(AR) r $(@) $(TARGETOBJ)
	

.cc.o :
	$(CC) $(CFLAGS_LOWER) -c $< -o $@

../../object/%.o: %.c
	$(CC) $(CFLAGS_LOWER) -c $< -o $@

%.o : %.cc
	$(CC) $(CFLAGS_LOWER) -c $< -o $@

clean : 
	@$(RM) libpos.a
	@$(RM) *.o
