#ifndef DEBUG_UTIL_H
#define DEBUG_UTIL_H
#include <csignal>
#include <dlfcn.h>
#include <cxxabi.h>
#include <stdio.h>
#include <string.h>
#include <sstream>
#include <iostream>
#include <cxxabi.h> // __cxa_demangle
//#include <elfutils/libdwfl.h> // Dwfl*
#include <execinfo.h> // backtrace
#include <unistd.h> // getpid
#include <cassert>
#include <memory>

#include "./settings.h"

#define EPRINT(error, isabort, ... )\
	do{\
		printf("[%s:%d]-",__FILE__, __LINE__);\
		printf(error, ##__VA_ARGS__);\
		printf("\n");\
		if((isabort)){abort();}\
	}while(0)

#define EPRINT_CNT(cnt, target_cnt, error, isabort, ... )\
	do{\
        static int cnt=0;\
		printf("[%s:%d:%u]-",__FILE__, __LINE__,++cnt);\
		printf(error, ##__VA_ARGS__);\
		printf("\n");\
		if((isabort)){abort();}\
        if(target_cnt!=-1 && cnt==target_cnt){\
            std::raise(SIGINT);\
        }\
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
/*
static std::string demangle(const char* name) {
    int status = -4;
    std::unique_ptr<char, void(*)(void*)> res {
        abi::__cxa_demangle(name, NULL, NULL, &status),
        std::free
    };
    return (status==0) ? res.get() : name ;
}

static std::string debug_info(Dwfl* dwfl, void* ip) {
    std::string function;
    int line = -1;
    char const* file;
    uintptr_t ip2 = reinterpret_cast<uintptr_t>(ip);
    Dwfl_Module* module = dwfl_addrmodule(dwfl, ip2);
    char const* name = dwfl_module_addrname(module, ip2);
    function = name ? demangle(name) : "<unknown>";
    if (Dwfl_Line* dwfl_line = dwfl_module_getsrc(module, ip2)) {
        Dwarf_Addr addr;
        file = dwfl_lineinfo(dwfl_line, &addr, &line, nullptr, nullptr, nullptr);
    }
    std::stringstream ss;
    ss << ip << ' ' << function;
    if (file)
        ss << " at " << file << ':' << line;
    ss << std::endl;
    return ss.str();
}
*/
static inline void print_stacktrace(uint32_t max)
{
#if 1
	return;
#else
	    Dwfl* dwfl = nullptr;
    {
        Dwfl_Callbacks callbacks = {};
        char* debuginfo_path = nullptr;
        callbacks.find_elf = dwfl_linux_proc_find_elf;
        callbacks.find_debuginfo = dwfl_standard_find_debuginfo;
        callbacks.debuginfo_path = &debuginfo_path;
        dwfl = dwfl_begin(&callbacks);
        assert(dwfl);
        int r;
        r = dwfl_linux_proc_report(dwfl, getpid());
        assert(!r);
        r = dwfl_report_end(dwfl, nullptr, nullptr);
        assert(!r);
        static_cast<void>(r);
    }

    // Loop over stack frames.
    std::stringstream ss;
    {
        void* stack[512];
        int stack_size = ::backtrace(stack, sizeof stack / sizeof *stack);
        for (int i = 0; i < MIN(stack_size,max); ++i) {
            ss << i << ": ";
            // Works.
            ss << debug_info(dwfl, stack[i]);

        }
    }
    dwfl_end(dwfl);
	printf("%s\n", ss.str().c_str());
#endif
}

#endif
