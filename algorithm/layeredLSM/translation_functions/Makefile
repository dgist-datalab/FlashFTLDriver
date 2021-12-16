SRCS +=\
	exact_mapping.c\
	bf.c\
	bf_mapping.c\
	./plr_mapping.c\
	plr/plr.c\
	./bf_guard_mapping.c\
	./tree_mapping.c\


OBJS :=\
	$(patsubst %.c,%.o,$(SRCS))\

TARGETOBJ:=\
	$(addprefix ../../../object/,$(OBJS))\

LEAK: all
all: $(OBJS)

libnom.a : $(TARGETOBJ)
	$(AR) r $(@) $(TARGETOBJ)

%.o :%.c
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

../../../object/%.o:%.c
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

plr/plr.o: ./plr/plr.cpp
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

clean : 
	@$(RM) *.o
	@$(RM) ./plr/*.o
