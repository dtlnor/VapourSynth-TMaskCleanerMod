#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <memory>
#include <vector>
#include "VapourSynth4.h"
#include "VSHelper4.h"
#include <string>
#include <numeric>
#include <algorithm>

struct TMCData;

typedef std::pair<int, int> Coordinates;
typedef void (*Process_c_Ptr)(const VSFrame*, VSFrame*, int, const TMCData*, const VSAPI*);

struct TMCData {
	VSNode* node;
	const VSVideoInfo* vi;
	unsigned int length;
	union {
		uint8_t thresh_u8;
		uint16_t thresh_u16;
		float thresh_f32;
	} thresh_typed;
	unsigned int fade;
	Process_c_Ptr process_c_func;
	const Coordinates* directions;
	int dir_count;

	template<typename pixel_t>
	pixel_t get_thresh() const {
		if constexpr (std::is_same_v<pixel_t, uint8_t>) {
			return thresh_typed.thresh_u8;
		}
		else if constexpr (std::is_same_v<pixel_t, uint16_t>) {
			return thresh_typed.thresh_u16;
		}
		else {
			return thresh_typed.thresh_f32;
		}
	}

	template<typename pixel_t>
	void set_thresh(pixel_t value) {
		if constexpr (std::is_same_v<pixel_t, uint8_t>) {
			thresh_typed.thresh_u8 = value;
		}
		else if constexpr (std::is_same_v<pixel_t, uint16_t>) {
			thresh_typed.thresh_u16 = value;
		}
		else {
			thresh_typed.thresh_f32 = value;
		}
	}
};

template<typename pixel_t>
inline bool is_black(pixel_t value, pixel_t thresh) {
	return value < thresh;
}

inline bool visited(int x, int y, int width, const std::vector<uint8_t>& lookup) {
	unsigned int normal_pos = y * width + x;
	return lookup[normal_pos >> 3] & (1 << (normal_pos & 7));
}

inline void visit(int x, int y, int width, std::vector<uint8_t>& lookup) {
	unsigned int normal_pos = y * width + x;
	lookup[normal_pos >> 3] |= (1 << (normal_pos & 7));
}

constexpr Coordinates directions4[4] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
constexpr Coordinates directions8[8] = {
	{-1, -1}, {0, -1}, {1, -1},
	{-1, 0},           {1, 0},
	{-1, 1},  {0, 1},  {1, 1}
};

template<bool binarize, bool reverse, typename pixel_t>
void setProcessFunction(TMCData* d, int mode) {
	switch (mode) {
	case 0: d->process_c_func = &process_c<0, binarize, reverse, pixel_t>; break;
	case 1: d->process_c_func = &process_c<1, binarize, reverse, pixel_t>; break;
	case 2: d->process_c_func = &process_c<2, binarize, reverse, pixel_t>; break;
	case 3: d->process_c_func = &process_c<3, binarize, reverse, pixel_t>; break;
	case 4: d->process_c_func = &process_c<4, binarize, reverse, pixel_t>; break;
	case 5: d->process_c_func = &process_c<5, binarize, reverse, pixel_t>; break;
	case 6: d->process_c_func = &process_c<6, binarize, reverse, pixel_t>; break;
	case 7: d->process_c_func = &process_c<7, binarize, reverse, pixel_t>; break;
	case 8: d->process_c_func = &process_c<8, binarize, reverse, pixel_t>; break;
	default: throw std::string("mode must be in the range [0, 8].");
	}
}

extern void VS_CC FilterFree(void* instanceData, VSCore* core, const VSAPI* vsapi);
extern void VS_CC TMCCreate(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi);
extern void VS_CC CCLSCreate(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi);
