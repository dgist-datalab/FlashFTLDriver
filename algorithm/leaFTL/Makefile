SRCS +=\
	CRB.cc\
	group.cc\
	issue_io.cc\
	lea_cache.cc\
	leaFTL.cc\
	page_manager.cc\
	PLR_segment.cc\
	./plr/plr.cc\
	write_buffer.cc\


OBJS :=\
	$(patsubst %.cc,%.o,$(SRCS))\


TARGETOBJ:=\
	$(addprefix ../../object/,$(OBJS))\

LEAK: all
all: $(OBJS) 
	mv ./plr/plr.o ../../object/
	mv *.o ../../object/

libnom.a : $(TARGETOBJ)
	$(AR) r $(@) $(TARGETOBJ)

%.o :%.cc
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

plr/plr.o: ./plr/plr.cc
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

../../object/%.o:%.cc
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

clean : 
	@$(RM) libnom.a
	@$(RM) *.o
