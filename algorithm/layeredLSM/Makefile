SRCS +=\
	   ./block_table.c\
	   ./sorted_table.c\
	   ./page_aligner.c\
	   ./layered_lsm_operation.c\
	   ./summary_page.c\
	   ./summary_page_iter.c\
	   ./summary_page_set.c\
	   ./mapping_function.c\
	   ./run.c\
	   ./run_query.c\
	   ./run_insert.c\
	   ./run_merge.c\
	   ./run_util.c\
	   ./shortcut.c\
	   ./compaction.c\
	   ./gc.c\


OBJS :=\
	$(patsubst %.c,%.o,$(SRCS))\

TARGETOBJ:=\
	$(addprefix ../../object/,$(OBJS))\

LEAK: all
all: $(TARGETOBJ)
	cd ./translation_functions && make
	mv ./translation_functions/*.o ../../object/
	mv ./translation_functions/plr/*.o ../../object/

libnom.a : $(TARGETOBJ)
	$(AR) r $(@) $(TARGETOBJ)

%.o :%.c
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

../../object/%.o:%.c
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

clean : 
	@$(RM) *.o
	cd ./translation_functions/  && make clean
