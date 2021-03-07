SRCS +=\
	   bloomfilter.c\
	   io.c\
	   level.c\
	   lftl_slab.c\
	   lsmtree.c\
	   lsmtree_param_module.c\
	   page_manager.c\
	   run.c\
	   write_buffer.c\
	   compaction.c\
	   compaction_assistant.c\
	   function_test.c\
	   version.c\
	   write_buffer.c\
	   sst_page_file_stream.c\
	   sst_block_file_stream.c\
	   sst_file.c\
	   gc.c\


DIRS := $(shell find ./caching/ -type d)

OBJS :=\
	$(patsubst %.c,%.o,$(SRCS))\

CACHE_OBJS:=\
	$(patsubst %.c,%.o,$(CACHE_SRCS))\

TARGETOBJ:=\
	$(addprefix ../../object/,$(OBJS))\


LEAK: all
all: $(TARGETOBJ)

libnom.a : $(TARGETOBJ)
	$(AR) r $(@) $(TARGETOBJ)

%.o :%.c
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

../../object/%.o:%.c
	$(CC) $(CFLAGS_ALGO) -c $< -o $@

clean : 
	@$(RM) libnom.a
	@$(RM) *.o
