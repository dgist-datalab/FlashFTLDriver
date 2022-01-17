SRCS +=\
	   ./block_table.cpp\
	   ./sorted_table.cpp\
	   ./page_aligner.cpp\
	   ./layered_lsm_operation.cpp\
	   ./summary_page.cpp\
	   ./summary_page_iter.cpp\
	   ./summary_page_set.cpp\
	   ./mapping_function.cpp\
	   ./run.cpp\
	   ./run_query.cpp\
	   ./run_insert.cpp\
	   ./run_merge.cpp\
	   ./run_util.cpp\
	   ./shortcut.cpp\
	   ./compaction.cpp\
	   ./level.cpp\
	   ./gc.cpp\
	   ./debug.cpp\
	   ./lsmtree_parameter.cpp\
	   ./lsmtree.cpp\
	   ./shortcut_dir.cpp\
	   ./lsmtree_data_dump.cpp\


OBJS :=\
	$(patsubst %.cpp,%.o,$(SRCS))\

TARGETOBJ:=\
	$(addprefix ../../object/,$(OBJS))\

LEAK: all
all: $(TARGETOBJ)
	cd ./translation_functions && make
	mv ./translation_functions/*.o ../../object/
	mv ./translation_functions/plr/*.o ../../object/
	cd design_knob && make 
	mv design_knob/*.o ../../object/

libnom.a : $(TARGETOBJ)
	$(AR) r $(@) $(TARGETOBJ)

%.o :%.cpp
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

../../object/%.o:%.cpp
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

clean : 
	@$(RM) *.o
	cd ./translation_functions/  && make clean
