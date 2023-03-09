#pragma once
#include <iostream>
#include <cstdint>
#include <vector>
#include <list>
#include <tuple>
#include <memory.h>
//using namespace std;

struct dot {
	__int128_t x;
	__int128_t y;
};

struct slope {
	__int128_t n;
	__int128_t d;
	bool operator<(slope other) {
		return this->n * other.d < this->d * other.n;
	}
};

class PLR {
	private:
	// Parameter
	static uint32_t plr_idx;
	uint32_t now_idx;
	int32_t TICK = 200;
	int32_t DELTA_SIZE = 30;
	int32_t CORRECTION = 60;

	bool isfinish;
	int64_t slope_bit;
	int32_t range;
	
	// 아래 두 parameter는 input 받을 때
	int A_BIT, B_BIT, A_FULL, B_FULL;
	int64_t bit;
	int32_t delta;

	// 메모리 압축에 사용되는 것들
	int X_BIT = 11;
	int Y_BIT = 9;

	/*constructor target*/
	//int X_BIT = 20;
	//int Y_BIT = 20;

	int X_FULL = (1 << X_BIT) - 1;
	int Y_FULL = (1 << Y_BIT) - 1;

	uint64_t* x_compressed;
	uint64_t* y_compressed;
	uint64_t* a_compressed;
	uint64_t* b_compressed;
	uint32_t x_size, y_size, a_size, b_size;

	int32_t* chunk_x;
	int32_t* chunk_y;
	int32_t* chunk_offset;
	uint32_t chunk_size, chunk_cnt;

	inline void set_x(int index, uint64_t value);
	inline void set_y(int index, uint64_t value);
	inline void set_a(int index, uint64_t value);
	inline void set_b(int index, uint64_t value);

	int line_cnt;
	uint32_t inserted_kp_num;
	uint32_t min_lba, max_lba;
	int32_t x_prev, y_prev;

	inline uint64_t get_x(int index);
	inline uint64_t get_y(int index);
	inline uint64_t get_a(int index);
	inline uint64_t get_b(int index);

	void line_push_back(int64_t x, int64_t y, int32_t a, int32_t b);

	// PLR을 만들 때 필요한 것들
	slope get_slope(dot a, dot b);
	int64_t exist_quantized_slope(slope lower, slope upper);
	int64_t find_intercept(std::vector<dot>& vec, int64_t slp);
	int64_t find_intercept_op(std::vector<dot>& vec, int64_t slp); //slow building time, but good RAF
	int64_t find_intercept_o1(std::vector<dot>& vec, int64_t slp); //fast building time, but RAF
	int ccw(dot a, dot b, dot c);

	std::vector<dot> member;
	slope upper, lower;
	dot s_a, s_b, s_c, s_d;
	std::list<dot> cvx_upper, cvx_lower;
	dot start;

	// Exception
	int32_t PPA_min, PPA_max;

	// Parameter에 문제가 있는지 체크
	int32_t LBA_prev;
#ifdef PARAM
	int x_over_counter;
	int y_over_counter;
#endif

	public:
/*
 * 생성자
 * slope_bit: 기울기를 표현하는 데에 몇 bit 쓸지
 * range: 양쪽 page 침범 가능 범위. 0, 5, 10, ..., 95, 100 사이 정수
 */ 
	PLR(int64_t slope_bit, int32_t range);

/*
 * insert
 * 주의사항: LBA는 반드시 증가하는 순으로 주어져야 함
 */ 
	void insert(int32_t LBA, int32_t PPA);

/*
 * insert_end
 * PLR이 종료되면 선을 끊고 몇 가지 표시를 하기 위해 호출
 */ 
	void insert_end();

/*
 * memory_usage
 * 메모리 사용량 반환. 단위: bit
 */ 
	uint64_t memory_usage(uint32_t lba_unit);
	uint64_t get_normal_memory_usage(uint32_t lba_unit);
	uint64_t get_line_cnt();
	uint64_t get_chunk_cnt();

/*
 * get: PPA 반환
 */ 
	int64_t get(int64_t LBA);

	void set_X_bit(uint32_t x){
		this->X_BIT=x;
	}

	void set_Y_bit(uint32_t y){
		this->Y_BIT=y;
	}

	void set_delta_size(uint32_t size){
		this->DELTA_SIZE=size;
	}

/*
 * clear
 * 메모리 free
 */
	PLR* copy();
	void clear();

	double get_line_per_chunk();

	void print_line();
	void parameter_analysis();
};
