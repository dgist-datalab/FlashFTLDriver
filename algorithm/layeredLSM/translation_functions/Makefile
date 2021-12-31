SRCS +=\
	exact_mapping.cpp\
	bf.cpp\
	bf_mapping.cpp\
	./plr_mapping.cpp\
	plr/plr.cpp\
	./bf_guard_mapping.cpp\
	./tree_mapping.cpp\


OBJS :=\
	$(patsubst %.cpp,%.o,$(SRCS))\

TARGETOBJ:=\
	$(addprefix ../../../object/,$(OBJS))\

LEAK: all
all: $(OBJS)

libnom.a : $(TARGETOBJ)
	$(AR) r $(@) $(TARGETOBJ)

%.o :%.cpp
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

../../../object/%.o:%.cpp
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

plr/plr.o: ./plr/plr.cpp
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

clean : 
	@$(RM) *.o
	@$(RM) ./plr/*.o
