#include <csignal>
#define EPRINT(error, isabort)\
	do{\
		printf("[%s:%d]-%s\n", __FILE__,__LINE__, (error));\
		if((isabort)){abort();}\
	}while(0)

#define DEBUG_CNT_PRINT(cnt_variable, target_cnt, function_name, function_line)\
	do{\
		static int cnt_variable=0;\
		if(target_cnt==-1){\
			printf("[%s:%u] cnt:%u\n",function_name, function_line,  ++cnt_variable);\
		}\
		else if(target_cnt==++cnt_variable){\
			std::raise(SIGINT);\
		}\
	}while(0)

#define GDB_MAKE_BREAKPOINT\
	do{std::raise(SIGINT);}while(0)
