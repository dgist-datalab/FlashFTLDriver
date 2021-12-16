#include <iostream>
#include "plr.h"
using namespace std;

char input[1000];

int main(int argc, char* argv[]) {
	const int32_t ELEMENT_PER_BLOCK = 4;
	int range = atoi(argv[1]);

	int correct = 0, wrong = 0;
	long long total_memory = 0;

	int32_t PPA, LBA;
	int32_t PPA_prev, LBA_prev;
	bool must_insert = 0;

	while (1) {
		PLR plr(7, range);
		vector<pair<int32_t, int32_t>> inp;

		if (must_insert) {
			must_insert = 0;
			PPA_prev = PPA;
			LBA_prev = LBA;
			PPA /= ELEMENT_PER_BLOCK;
			inp.push_back({LBA, PPA});
		}
		else {
			PPA_prev = -1;
			LBA_prev = -1;
		}

		bool exit_sign = 0;
		while (1) {
			if (!cin.getline(input, 1000)) {
				exit_sign = 1;
				break;
			}
			if (strlen(input) == 0) break;
			sscanf(input, "%d %d", &PPA, &LBA);

			if (PPA < PPA_prev || LBA < LBA_prev) {
				must_insert = 1;
				break;
			}

			PPA_prev = PPA;
			LBA_prev = LBA;
			PPA /= ELEMENT_PER_BLOCK;
			inp.push_back({LBA, PPA});
		}
		if (exit_sign) {
			plr.clear();
			break;
		}

		for (int i = 0; i < inp.size(); ++i) {
			plr.insert(inp[i].first, inp[i].second);
		}
		plr.insert_end();

		for (int i = 0; i < inp.size(); ++i) {
			if (abs(inp[i].second - plr.get(inp[i].first)) == 0) {
				correct++;
			}
			else if (abs(inp[i].second - plr.get(inp[i].first)) == 1) {
				wrong++;
			}
			else {
				printf("Error on %d\n", inp[i].first);
				printf("plr get: %ld\n", plr.get(inp[i].first));
				printf("original PPA: %d\n", inp[i].second);
				exit(0);
			}
		}
		total_memory += plr.memory_usage(48);
		
		plr.clear();
	}

	cout << argv[1] << "%\n";
	cout << total_memory << '\n';
	cout << "RAF: " << (double)(correct + 2 * wrong) / (correct + wrong) << '\n';
}
