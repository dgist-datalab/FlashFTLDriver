make:
	g++ -g -fsanitize=address -o plr plr.cpp plr_test.cpp 

test:
	g++ -g -O3 -o plr plr.cpp plr_quant_test.cpp

param:
	g++ -DPARAM -O2 -o plr plr.cpp plr_test.cpp
