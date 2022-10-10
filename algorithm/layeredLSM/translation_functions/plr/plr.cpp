#include "plr.h"
uint32_t PLR::plr_idx=0;
inline uint64_t PLR::get_x(int index) {
	int start_bit = index * X_BIT;
	if ((start_bit >> 6) == ((start_bit + X_BIT - 1) >> 6)) {
		return (x_compressed[start_bit >> 6] >> (start_bit & 0x3f)) & X_FULL;
	}
	else {
		return ((x_compressed[start_bit >> 6] >> (start_bit & 0x3f))
			| (x_compressed[(start_bit >> 6) + 1] << (64 - (start_bit & 0x3f)))) & X_FULL;
	}
}

inline uint64_t PLR::get_y(int index) {
	int start_bit = index * Y_BIT;
	if ((start_bit >> 6) == ((start_bit + Y_BIT - 1) >> 6)) {
		return (y_compressed[start_bit >> 6] >> (start_bit & 0x3f)) & Y_FULL;
	}
	else {
		return ((y_compressed[start_bit >> 6] >> (start_bit & 0x3f))
			| (y_compressed[(start_bit >> 6) + 1] << (64 - (start_bit & 0x3f)))) & Y_FULL;
	}
}

inline uint64_t PLR::get_a(int index) {
	int start_bit = index * A_BIT;
	if ((start_bit >> 6) == ((start_bit + A_BIT - 1) >> 6)) {
		return (a_compressed[start_bit >> 6] >> (start_bit & 0x3f)) & A_FULL;
	}
	else {
		return ((a_compressed[start_bit >> 6] >> (start_bit & 0x3f))
			| (a_compressed[(start_bit >> 6) + 1] << (64 - (start_bit & 0x3f)))) & A_FULL;
	}
}

inline uint64_t PLR::get_b(int index) {
	int start_bit = index * B_BIT;
	if ((start_bit >> 6) == ((start_bit + B_BIT - 1) >> 6)) {
		return (b_compressed[start_bit >> 6] >> (start_bit & 0x3f)) & B_FULL;
	}
	else {
		return ((b_compressed[start_bit >> 6] >> (start_bit & 0x3f))
			| (b_compressed[(start_bit >> 6) + 1] << (64 - (start_bit & 0x3f)))) & B_FULL;
	}
}

inline void PLR::set_x(int index, uint64_t value) {
	int start_bit = index * X_BIT;
	if ((start_bit >> 6) == ((start_bit + X_BIT - 1) >> 6)) {
		if ((start_bit >> 6) >= x_size) {
			uint64_t* temp = (uint64_t*)calloc(x_size * 2, sizeof(uint64_t));
			memcpy(temp, x_compressed, x_size * sizeof(uint64_t));
			free(x_compressed);
			x_compressed = temp;
			x_size *= 2;
		}
		x_compressed[start_bit >> 6] |= (value << (start_bit & 0x3f));
	}
	else {
		if (((start_bit >> 6) + 1) >= x_size) {
			uint64_t* temp = (uint64_t*)calloc(x_size * 2, sizeof(uint64_t));
			memcpy(temp, x_compressed, x_size * sizeof(uint64_t));
			free(x_compressed);
			x_compressed = temp;
			x_size *= 2;
		}
		x_compressed[start_bit >> 6] |= (value << (start_bit & 0x3f));
		x_compressed[(start_bit >> 6) + 1] |= (value >> (64 - (start_bit & 0x3f)));
	}
}

inline void PLR::set_y(int index, uint64_t value) {
	int start_bit = index * Y_BIT;
	if ((start_bit >> 6) == ((start_bit + Y_BIT - 1) >> 6)) {
		if ((start_bit >> 6) >= y_size) {
			uint64_t* temp = (uint64_t*)calloc(y_size * 2, sizeof(uint64_t));
			memcpy(temp, y_compressed, y_size * sizeof(uint64_t));
			free(y_compressed);
			y_compressed = temp;
			y_size *= 2;
		}
		y_compressed[start_bit >> 6] |= (value << (start_bit & 0x3f));
	}
	else {
		if (((start_bit >> 6) + 1) >= y_size) {
			uint64_t* temp = (uint64_t*)calloc(y_size * 2, sizeof(uint64_t));
			memcpy(temp, y_compressed, y_size * sizeof(uint64_t));
			free(y_compressed);
			y_compressed = temp;
			y_size *= 2;
		}
		y_compressed[start_bit >> 6] |= (value << (start_bit & 0x3f));
		y_compressed[(start_bit >> 6) + 1] |= (value >> (64 - (start_bit & 0x3f)));
	}
}

inline void PLR::set_a(int index, uint64_t value) {
	int start_bit = index * A_BIT;
	if ((start_bit >> 6) == ((start_bit + A_BIT - 1) >> 6)) {
		if ((start_bit >> 6) >= a_size) {
			uint64_t* temp = (uint64_t*)calloc(a_size * 2, sizeof(uint64_t));
			memcpy(temp, a_compressed, a_size * sizeof(uint64_t));
			free(a_compressed);
			a_compressed = temp;
			a_size *= 2;
		}
		a_compressed[start_bit >> 6] |= (value << (start_bit & 0x3f));
	}
	else {
		if (((start_bit >> 6) + 1) >= a_size) {
			uint64_t* temp = (uint64_t*)calloc(a_size * 2, sizeof(uint64_t));
			memcpy(temp, a_compressed, a_size * sizeof(uint64_t));
			free(a_compressed);
			a_compressed = temp;
			a_size *= 2;
		}
		a_compressed[start_bit >> 6] |= (value << (start_bit & 0x3f));
		a_compressed[(start_bit >> 6) + 1] |= (value >> (64 - (start_bit & 0x3f)));
	}
}

inline void PLR::set_b(int index, uint64_t value) {
	int start_bit = index * B_BIT;
	if ((start_bit >> 6) == ((start_bit + B_BIT - 1) >> 6)) {
		if ((start_bit >> 6) >= b_size) {
			uint64_t* temp = (uint64_t*)calloc(b_size * 2, sizeof(uint64_t));
			memcpy(temp, b_compressed, b_size * sizeof(uint64_t));
			free(b_compressed);
			b_compressed = temp;
			b_size *= 2;
		}
		b_compressed[start_bit >> 6] |= (value << (start_bit & 0x3f));
	}
	else {
		if (((start_bit >> 6) + 1) >= b_size) {
			uint64_t* temp = (uint64_t*)calloc(b_size * 2, sizeof(uint64_t));
			memcpy(temp, b_compressed, b_size * sizeof(uint64_t));
			free(b_compressed);
			b_compressed = temp;
			b_size *= 2;
		}
		b_compressed[start_bit >> 6] |= (value << (start_bit & 0x3f));
		b_compressed[(start_bit >> 6) + 1] |= (value >> (64 - (start_bit & 0x3f)));
	}
}

void PLR::line_push_back(int64_t x, int64_t y, int32_t a, int32_t b) {
	x /= CORRECTION;
	y -= TICK / 2;
	y /= TICK;

#ifdef PARAM
	if (a == -1) {
		cout << "AHHHHHHHHHHHHHHH\n";
		abort();
	}
#endif

	if (chunk_cnt == 0 || line_cnt - chunk_offset[chunk_cnt - 1] == DELTA_SIZE || x - x_prev > X_FULL || y - y_prev > Y_FULL) {
		if (chunk_cnt >= chunk_size) {
			chunk_size *= 2;
			chunk_x = (int32_t*)realloc(chunk_x, chunk_size * sizeof(int32_t));
			chunk_y = (int32_t*)realloc(chunk_y, chunk_size * sizeof(int32_t));
			chunk_offset = (int32_t*)realloc(chunk_offset, chunk_size * sizeof(int32_t));
		}

#ifdef PARAM
		if (x - x_prev < X_FULL) x_over_counter++;
		if (y - y_prev < Y_FULL) y_over_counter++;
#endif
		chunk_x[chunk_cnt] = x;
		chunk_y[chunk_cnt] = y;
		chunk_offset[chunk_cnt] = line_cnt;
		chunk_cnt++;

		set_x(line_cnt, X_FULL);
		set_y(line_cnt, Y_FULL);
	}
	else {
		set_x(line_cnt, x - x_prev);
		set_y(line_cnt, y - y_prev);
	}

	set_a(line_cnt, a);

	if (b < 0) set_b(line_cnt, -b + (1 << (B_BIT - 1)) - 1);
	else set_b(line_cnt, b);

	x_prev = x;
	y_prev = y;

	line_cnt++;
}

slope PLR::get_slope(dot a, dot b) {
	return {b.y - a.y, b.x - a.x};
}

// slope의 분모는 항상 양수로 유지됨
int64_t PLR::exist_quantized_slope(slope lower, slope upper) {
	if (upper.n <= 0) return -1;
	if (lower.n < 0) return 0;
	if (lower.n * bit >= lower.d * (bit - 1)) return -1;
	int64_t low = 0, high = bit;
	while (high - low != 1) {
		int64_t mid = (low + high) / 2;
		if (mid * upper.d >= upper.n * bit) high = mid;
		else if (mid * lower.d <= lower.n * bit) low = mid;
		else return mid;
	}
	return -1;
}

int64_t PLR::find_intercept(std::vector<dot>& vec, int64_t slp) {
	int64_t low = -bit * delta, high = bit * delta;
	bool pos = 1;
	for (int i = 1; i < vec.size(); ++i) {
		int64_t y = vec[0].y * bit + low + (vec[i].x - vec[0].x) * slp;
		if ((vec[i].y - delta) * bit > y) {
			pos = 0;
			break;
		}
	}
	if (pos) return low;
	while (high - low != 1) {
		pos = 1;
		int64_t mid = (low + high) / 2;
		for (int i = 1; i < vec.size(); ++i) {
			int64_t y = vec[0].y * bit + mid + (vec[i].x - vec[0].x) * slp;
			if ((vec[i].y - delta) * bit > y) {
				pos = 0;
				low = mid;
			}
			if ((vec[i].y + delta) * bit <= y) {
				pos = 0;
				high = mid;
			}
		}
		if (pos) return mid;
	}
	return -1;
}

int64_t PLR::find_intercept_op(std::vector<dot>& vec, int64_t slp) {
	int64_t up_low = -bit * delta, up_high = bit * delta;
	int64_t down_low = -bit * delta, down_high = bit * delta;
	int64_t down, up;
	{ // find down
		bool pos = 1;
		for (int i = 1; i < vec.size(); ++i) {
			int64_t y = vec[0].y * bit + down_low + (vec[i].x - vec[0].x) * slp;
			if ((vec[i].y - delta) * bit > y) {
				pos = 0;
				break;
			}
		}
		if (pos) down = down_low;
		else {
			while (down_high - down_low != 1) {
				pos = 1;
				int64_t mid = (down_low + down_high) / 2;
				for (int i = 1; i < vec.size(); ++i) {
					int64_t y = vec[0].y * bit + mid + (vec[i].x - vec[0].x) * slp;
					if ((vec[i].y - delta) * bit > y) {
						pos = 0;
						break;
					}
				}
				if (pos) down_high = mid;
				else down_low = mid;
			}
			down = down_high;
		}
	}
	{ // find up
		bool pos = 1;
		for (int i = 1; i < vec.size(); ++i) {
			int64_t y = vec[0].y * bit + up_high + (vec[i].x - vec[0].x) * slp;
			if ((vec[i].y + delta) * bit < y) {
				pos = 0;
				break;
			}
		}
		if (pos) up = up_high;
		else {
			while (up_high - up_low != 1) {
				pos = 1;
				int64_t mid = (up_low + up_high) / 2;
				for (int i = 1; i < vec.size(); ++i) {
					int64_t y = vec[0].y * bit + mid + (vec[i].x - vec[0].x) * slp;
					if ((vec[i].y + delta) * bit < y) {
						pos = 0;
						break;
					}
				}
				if (pos) up_low = mid;
				else up_high = mid;
			}
			up = up_low;
		}
	}
	int wrong = 987654321;
	int64_t ret;
	for (int64_t i = down; i < up; ++i) {
		int cnt = 0;
		for (int j = 0; j < vec.size(); ++j) {
			int64_t y = vec[0].y * bit + i + (vec[j].x - vec[0].x) * slp;
			if (y < (vec[j].y - TICK / 2) * bit || y >= (vec[j].y + TICK / 2) * bit) cnt++;
		}
		if (cnt < wrong) {
			wrong = cnt;
			ret = i;
		}
	}
	return ret;
}

int64_t PLR::find_intercept_o1(std::vector<dot>& vec, int64_t slp) {
	// 원점 보정
	dot a = s_a, b = s_b, c = s_c, d = s_d;
	a.x -= vec[0].x; a.y -= vec[0].y;
	b.x -= vec[0].x; b.y -= vec[0].y;
	c.x -= vec[0].x; c.y -= vec[0].y;
	d.x -= vec[0].x; d.y -= vec[0].y;

	// line1: cross (a, c), y = p1 * x + q1
	double p1 = (double)(c.y - a.y) / (c.x - a.x);
	double q1 = -p1 * a.x + a.y;

	// line2: cross (b, d), y = p2 * x + q2
	double p2 = (double)(d.y - b.y) / (d.x - b.x);
	double q2 = -p2 * b.x + b.y;

	// solve with x
	double x_intersect = (q2 - q1) / (p1 - p2);
	double y_intersect = p1 * x_intersect + q1;

#define EPS 1e-9
	double ret = y_intersect - x_intersect * slp / bit + delta + EPS;
	ret *= bit;
	return (int64_t)ret - delta * bit;
}

int PLR::ccw(dot a, dot b, dot c) {
	dot ab = {b.x - a.x, b.y - a.y};
	dot ac = {c.x - a.x, c.y - a.y};
	int64_t d = (ab.x * ac.y) - (ab.y * ac.x);
	if (d > 0) return 1;
	else if (d < 0) return -1;
	else return 0;
}

// range: 0.5 => 1
PLR::PLR(int64_t _slope_bit, int32_t _range) : slope_bit(_slope_bit), range(_range), isfinish(false){
	now_idx=plr_idx++;
	A_BIT = slope_bit;
	A_FULL = (1 << A_BIT) - 1;
	bit = (1 << A_BIT);

	delta = TICK / 2 + range;
	B_BIT = slope_bit + 1;
	int temp = delta;
	while (temp) {
		temp /= 2;
		B_BIT++;
	}
	B_FULL = (1 << B_BIT) - 1;

	x_compressed = (uint64_t*)calloc(1, sizeof(uint64_t));
	y_compressed = (uint64_t*)calloc(1, sizeof(uint64_t));
	a_compressed = (uint64_t*)calloc(1, sizeof(uint64_t));
	b_compressed = (uint64_t*)calloc(1, sizeof(uint64_t));
	x_size = 1;
	y_size = 1;
	a_size = 1;
	b_size = 1;

	chunk_x = (int32_t*)malloc(1 * sizeof(int32_t));
	chunk_y = (int32_t*)malloc(1 * sizeof(int32_t));
	chunk_offset = (int32_t*)malloc(1 * sizeof(int32_t));
	chunk_size = 1;
	chunk_cnt = 0;

	line_cnt = 0;

	LBA_prev = -1;

	PPA_min = 0x7fffffff;
	PPA_max = (1 << 31);
#ifdef PARAM
	x_over_counter = 0;
	y_over_counter = 0;
#endif

	inserted_kp_num=0;
	min_lba=UINT32_MAX;
	max_lba=0;
}

void PLR::insert(int32_t LBA, int32_t PPA) {
	if (PPA < PPA_min) PPA_min = PPA;
	if (PPA > PPA_max) PPA_max = PPA;

	inserted_kp_num++;
	if(min_lba>LBA) min_lba=LBA;
	if(max_lba<LBA) max_lba=LBA;
	

	if (LBA_prev >= LBA) {
		std::cout << "AHHHHHHHHHHHHHHH\n";
		std::cout << "LBA must be inserted in increasing order\n";
		std::cout << "LBA_prev: " << LBA_prev << '\n';
		std::cout << "LBA: " << LBA << '\n';
		abort();
	}
	LBA_prev = LBA;
	dot p = {(int64_t)LBA * CORRECTION, (int64_t)PPA * TICK + TICK / 2};
	if (member.empty()) {
		s_a = {p.x, p.y + delta};
		s_b = {p.x, p.y - delta};

		cvx_lower.push_back(s_a);
		cvx_upper.push_back(s_b);

		member.push_back(p);
	}
	else if (member.size() == 1) {
		s_c = {p.x, p.y - delta};
		s_d = {p.x, p.y + delta};

		lower = get_slope(s_a, s_c);
		upper = get_slope(s_b, s_d);

		if (exist_quantized_slope(lower, upper) == -1) {
			line_push_back(member[0].x, member[0].y, 0, 0);

			cvx_lower.clear();
			cvx_upper.clear();

			member.clear();

			s_a = {p.x, p.y + delta};
			s_b = {p.x, p.y - delta};

			cvx_lower.push_back(s_a);
			cvx_upper.push_back(s_b);

			member.push_back(p);
		}
		else {
			cvx_upper.push_back(s_c);
			cvx_lower.push_back(s_d);

			member.push_back(p);
		}
	}
	else {
		#ifdef PRINT
		printf("s_a: (%d, %d)\n", s_a.x, s_a.y);
		printf("s_b: (%d, %d)\n", s_b.x, s_b.y);
		printf("s_c: (%d, %d)\n", s_c.x, s_c.y);
		printf("s_d: (%d, %d)\n", s_d.x, s_d.y);
		printf("lower: %ld/%ld\n", lower.n, lower.d);
		printf("upper: %ld/%ld\n", upper.n, upper.d);
		printf("cvx_lower: ");
		for (auto it = cvx_lower.begin(); it != cvx_lower.end(); ++it) {
			printf("(%d, %d) ", it->x, it->y);
		}
		printf("\n");
		printf("cvx_upper: ");
		for (auto it = cvx_upper.begin(); it != cvx_upper.end(); ++it) {
			printf("(%d, %d) ", it->x, it->y);
		}
		printf("\n\n");
		#endif

		if (lower.d * (p.y + delta - s_c.y) <= (p.x - s_c.x) * lower.n || upper.d * (p.y - delta - s_d.y) >= (p.x - s_d.x) * upper.n) {
			// out of range
			int64_t a = exist_quantized_slope(lower, upper);
			line_push_back(member[0].x, member[0].y, a, find_intercept_o1(member, a));

			cvx_lower.clear();
			cvx_upper.clear();

			member.clear();

			s_a = {p.x, p.y + delta};
			s_b = {p.x, p.y - delta};

			cvx_lower.push_back(s_a);
			cvx_upper.push_back(s_b);

			member.push_back(p);
		}
		else {
			int64_t a = exist_quantized_slope(lower, upper);
			int64_t b = find_intercept_o1(member, a);

			if (lower.d * (p.y - delta - s_c.y) > (p.x - s_c.x) * lower.n) {
				slope slp_max = {-10000, 1};
				std::list<dot>::iterator max_it;
				for (auto it = cvx_lower.begin(); it != cvx_lower.end(); ++it) {
					if (slp_max < get_slope(*it, {p.x, p.y - delta})) {
						slp_max = get_slope(*it, {p.x, p.y - delta});
						max_it = it;
					}
					else break;
				}
				s_a = *max_it;
				s_c = {p.x, p.y - delta};

				while (cvx_lower.begin() != max_it) {
					cvx_lower.pop_front();
				}

				lower = get_slope(s_a, s_c);
				cvx_upper.push_back(s_c);

				// ccw check
				auto it = cvx_upper.end(); it--;
				auto jt = it; jt--;
				auto kt = jt; kt--;

				while (cvx_upper.size() >= 3 && ccw(*it, *jt, *kt) != 1) {
					cvx_upper.erase(jt);
					jt = kt;
					kt--;
				}
			}
			if (upper.d * (p.y + delta - s_d.y) < (p.x - s_d.x) * upper.n) {
				slope slp_min = {10000, 1};
				std::list<dot>::iterator min_it;
				for (auto it = cvx_upper.begin(); it != cvx_upper.end(); ++it) {
					if (get_slope(*it, {p.x, p.y + delta}) < slp_min) {
						slp_min = get_slope(*it, {p.x, p.y + delta});
						min_it = it;
					}
					else break;
				}
				s_b = *min_it;
				s_d = {p.x, p.y + delta};

				while (cvx_upper.begin() != min_it) {
					cvx_upper.pop_front();
				}

				upper = get_slope(s_b, s_d);
				cvx_lower.push_back(s_d);

				// ccw check
				auto it = cvx_lower.end(); it--;
				auto jt = it; jt--;
				auto kt = jt; kt--;

				while (cvx_lower.size() >= 3 && ccw(*it, *jt, *kt) != -1) {
					cvx_lower.erase(jt);
					jt = kt;
					kt--;
				}
			}
			if (exist_quantized_slope(lower, upper) == -1) {
				line_push_back(member[0].x, member[0].y, a, b);

				cvx_lower.clear();
				cvx_upper.clear();

				member.clear();

				s_a = {p.x, p.y + delta};
				s_b = {p.x, p.y - delta};

				cvx_lower.push_back(s_a);
				cvx_upper.push_back(s_b);

				member.push_back(p);
			}
			else {
				member.push_back(p);
			}
			#ifdef PRINT
			printf("s_a: (%d, %d)\n", s_a.x, s_a.y);
			printf("s_b: (%d, %d)\n", s_b.x, s_b.y);
			printf("s_c: (%d, %d)\n", s_c.x, s_c.y);
			printf("s_d: (%d, %d)\n", s_d.x, s_d.y);
			printf("lower: %ld/%ld\n", lower.n, lower.d);
			printf("upper: %ld/%ld\n", upper.n, upper.d);
			printf("cvx_lower: ");
			for (auto it = cvx_lower.begin(); it != cvx_lower.end(); ++it) {
				printf("(%d, %d) ", it->x, it->y);
			}
			printf("\n");
			printf("cvx_upper: ");
			for (auto it = cvx_upper.begin(); it != cvx_upper.end(); ++it) {
				printf("(%d, %d) ", it->x, it->y);
			}
			printf("\n\n");
			#endif
		}
	}
}

void PLR::insert_end() {
	if (member.size()) {
		int64_t a = exist_quantized_slope(lower, upper);
		int64_t b = find_intercept_o1(member, a);
		line_push_back(member[0].x, member[0].y, a, b);
	}
	set_x(line_cnt, X_FULL);
	set_y(line_cnt, Y_FULL);
	if (chunk_cnt >= chunk_size) {
		chunk_size *= 2;
		chunk_x = (int32_t*)realloc(chunk_x, chunk_size * sizeof(int32_t));
		chunk_y = (int32_t*)realloc(chunk_y, chunk_size * sizeof(int32_t));
		chunk_offset = (int32_t*)realloc(chunk_offset, chunk_size * sizeof(int32_t));
	}
	chunk_offset[chunk_cnt] = line_cnt;

	isfinish=true;

	member.clear();
	cvx_upper.clear();
	cvx_lower.clear();
	return;
}

uint64_t PLR::memory_usage(uint32_t lba_unit) {
	/*
	uint64_t ret = 0;
	ret += x_size * sizeof(uint64_t);
	ret += y_size * sizeof(uint64_t);
	ret += a_size * sizeof(uint64_t);
	ret += b_size * sizeof(uint64_t);
	ret += chunk_size * sizeof(int32_t);
	ret += chunk_size * sizeof(int32_t);
	ret += chunk_size * sizeof(int32_t);*/
	//std::cout << line_cnt << '\n';
	//std::cout << chunk_cnt << '\n';

	uint64_t memory_usage_res=line_cnt*(X_BIT+Y_BIT+A_BIT+B_BIT)+chunk_cnt*lba_unit*3;
	//line_cnt/chunk_cnt-->작으면 delta가 안됨-->x 비트 증가필요

	/*printf("[%u]avg bit:%.4lf - ratio:%.4lf(%u)\n", now_idx,
			(double)memory_usage_res/inserted_kp_num, (double)inserted_kp_num/(max_lba-min_lba+1), inserted_kp_num);*/
	return memory_usage_res;
}



uint64_t PLR::get_line_cnt(){
	return line_cnt;
}

uint64_t PLR::get_chunk_cnt(){
	return chunk_cnt;
}

double PLR::get_line_per_chunk(){
	if(line_cnt/chunk_cnt>DELTA_SIZE){
		printf("PLR::not allowed\n");
		abort();
	}
	return line_cnt/chunk_cnt;
}

uint64_t PLR::get_normal_memory_usage(uint32_t lba_unit){
	return line_cnt*(2*lba_unit+sizeof(double)*8);
}

int64_t PLR::get(int64_t LBA) {

	int low_x = 0, high_x = chunk_cnt;
	while (high_x - low_x != 1) {
		int mid_x = (low_x + high_x) / 2;
		if (LBA >= chunk_x[mid_x]) low_x = mid_x;
		else high_x = mid_x;
	}

	int64_t x = chunk_x[low_x];
	int64_t y = chunk_y[low_x];
	int index = chunk_offset[low_x] + 1;

	while (index < chunk_offset[low_x + 1]) {
		int dx = get_x(index);
		if (LBA >= x + dx) {
			x += dx;
			y += get_y(index);
			index++;
		}
		else break;
	}
	LBA *= CORRECTION;
	x *= CORRECTION;
	y = (y * TICK) + TICK / 2;

	int64_t a = get_a(index - 1);
	int64_t b = get_b(index - 1);

	if (b & (1 << (B_BIT - 1))) {
		b = -(b - ((1 << (B_BIT - 1)) - 1));
	}

	int64_t expected = (y * bit + (LBA - x) * a + b) / bit;

	if (expected / TICK < PPA_min) return PPA_min;
	if (expected / TICK > PPA_max) return PPA_max;

	return expected / TICK;
}

void PLR::clear() {
	free(x_compressed);
	free(y_compressed);
	free(a_compressed);
	free(b_compressed);

	free(chunk_x);
	free(chunk_y);
	free(chunk_offset);
}

void PLR::print_line() {
	for (int i = 0; i < line_cnt; ++i) {
		std::cout << get_x(i) << ' ' << get_y(i) << ' ';
		std::cout << get_a(i) << ' ';
		int64_t b = get_b(i);
		if (b & (1 << (B_BIT - 1))) {
			b = -(b - ((1 << (B_BIT - 1)) - 1));
		}
		std::cout << b << '\n';
	}
}

void PLR::parameter_analysis() {
#ifdef PARAM
	if (x_over_counter > chunk_cnt * 0.05) {
		cout << "X_BIT is too small (make >5\% of chunk)\n";
	}
	if (x_over_counter > chunk_cnt * 0.05) {
		cout << "Y_BIT is too small (make >5\% of chunk)\n";
	}
#endif
}

PLR *PLR::copy(){
	if(this->isfinish){
		printf("cannot copy not finished PLR\n");
		abort();
	}
	PLR *res=new PLR(this->slope_bit, this->range);
	*res=*this;
	res->x_compressed=(uint64_t*)malloc(sizeof(uint64_t)*this->x_size);
	res->y_compressed=(uint64_t*)malloc(sizeof(uint64_t)*this->y_size);
	res->a_compressed=(uint64_t*)malloc(sizeof(uint64_t)*this->a_size);
	res->b_compressed=(uint64_t*)malloc(sizeof(uint64_t)*this->b_size);

	res->chunk_x=(int32_t*)malloc(sizeof(int32_t)*this->chunk_size);
	res->chunk_y=(int32_t*)malloc(sizeof(int32_t)*this->chunk_size);
	res->chunk_offset=(int32_t*)malloc(sizeof(int32_t)*this->chunk_size);
	return res;
}
