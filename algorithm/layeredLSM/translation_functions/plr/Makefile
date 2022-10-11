make:
	g++ -g -fsanitize=address -o plr plr.cpp plr_test.cpp 

test:
	g++ -g -O3 -o plr plr.cpp plr_quant_test.cpp

2test: plr_test2.cpp
	g++ -g -O3 -o 2test plr.cpp plr_test2.cpp

temp_test: test.cpp
	g++ -g -O3 -o temp_test test.cpp

param:
	g++ -DPARAM -O2 -o plr plr.cpp plr_test.cpp
