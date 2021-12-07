SRCS +=\
	   ./block_table.c\
	   ./sorted_table.c\
	   ./page_aligner.c\
	   ./layered_lsm_operation.c\
	   ./summary_page.c\
	   ./mapping_function.c\
	   ./run_query.c\
	   ./run_insert.c\
	   ./run_util.c\


OBJS :=\
	$(patsubst %.c,%.o,$(SRCS))\

TARGETOBJ:=\
	$(addprefix ../../object/,$(OBJS))\

LEAK: all
all: $(TARGETOBJ)
	cd ./translation_functions && make

libnom.a : $(TARGETOBJ)
	$(AR) r $(@) $(TARGETOBJ)

%.o :%.c
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

../../object/%.o:%.c
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

clean : 
	@$(RM) *.o
