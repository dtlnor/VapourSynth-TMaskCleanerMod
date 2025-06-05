#include "shared.h"

#define LABEL_CAPACITY 512

template<typename pixel_t>
void process_ccs(const VSFrame* src, VSFrame* dst, int bits, const TMCData* d, const VSAPI* vsapi) {
	const pixel_t* srcptr = reinterpret_cast<const pixel_t*>(vsapi->getReadPtr(src, 0));
	const int srcStride = vsapi->getStride(src, 0) / sizeof(pixel_t);
	int height = vsapi->getFrameHeight(src, 0);
	int width = vsapi->getFrameWidth(src, 0);
	VSMap* props = vsapi->getFramePropertiesRW(dst);

	thread_local std::vector<uint8_t> lookup;
	const size_t lookup_size = (height * width + 7) >> 3;
	if (lookup.size() != lookup_size) {
		lookup.resize(lookup_size, 0);
	} else {
		std::fill(lookup.begin(), lookup.end(), 0);
	}

	thread_local std::deque<Coordinates> coordinates;

	const auto& directions = d->directions;
	const int dir_count = d->dir_count;
	const auto thresh = d->thresh;

	auto num_labels = 0;
	thread_local std::vector<int64_t> areas;
	thread_local std::vector<int64_t> lefts;
	thread_local std::vector<int64_t> tops;
	thread_local std::vector<int64_t> widths;
	thread_local std::vector<int64_t> heights;
	thread_local std::vector<double> centroids_x;
	thread_local std::vector<double> centroids_y;
	areas.clear();
	lefts.clear();
	tops.clear();
	widths.clear();
	heights.clear();
	centroids_x.clear();
	centroids_y.clear();
	if (areas.capacity() < LABEL_CAPACITY) {
		areas.reserve(LABEL_CAPACITY);
		lefts.reserve(LABEL_CAPACITY);
		tops.reserve(LABEL_CAPACITY);
		widths.reserve(LABEL_CAPACITY);
		heights.reserve(LABEL_CAPACITY);
		centroids_x.reserve(LABEL_CAPACITY);
		centroids_y.reserve(LABEL_CAPACITY);
	}

	unsigned int bg_pixel_count = 0;
	double bg_sum_x = 0.0, bg_sum_y = 0.0;
	int bg_min_x = width, bg_max_x = -1;
	int bg_min_y = height, bg_max_y = -1;

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			if (!is_white<pixel_t>(srcptr[srcStride * y + x], thresh)) {
				bg_pixel_count++;
				bg_sum_x += x;
				bg_sum_y += y;
				bg_min_x = std::min(bg_min_x, x);
				bg_max_x = std::max(bg_max_x, x);
				bg_min_y = std::min(bg_min_y, y);
				bg_max_y = std::max(bg_max_y, y);
				continue;
			}
			if (visited(x, y, width, lookup)) {
				continue;
			}
			coordinates.clear();

			unsigned int pixel_count = 1;
			double sum_x = x, sum_y = y;
			int min_x = x, max_x = x;
			int min_y = y, max_y = y;

			coordinates.emplace_back(x, y);
			visit(x, y, width, lookup);

			while (!coordinates.empty()) {
				/* pop first coordinates (BFS) */
				Coordinates current = coordinates.front();
				coordinates.pop_front();

				for (int dir = 0; dir < dir_count; dir++) {
					const int i = current.first + directions[dir].first;
					const int j = current.second + directions[dir].second;
					if (i >= 0 && i < width && j >= 0 && j < height &&
						!visited(i, j, width, lookup) &&
						is_white<pixel_t>(srcptr[j * srcStride + i], thresh)) {
						coordinates.emplace_back(i, j);
						visit(i, j, width, lookup);

						pixel_count++;
						sum_x += i;
						sum_y += j;
						min_x = std::min(min_x, i);
						max_x = std::max(max_x, i);
						min_y = std::min(min_y, j);
						max_y = std::max(max_y, j);
					}
				}
			}

			num_labels++;
			areas.emplace_back(pixel_count);
			lefts.emplace_back(min_x);
			tops.emplace_back(min_y);
			widths.emplace_back(max_x - min_x + 1);
			heights.emplace_back(max_y - min_y + 1);
			centroids_x.emplace_back(sum_x / pixel_count);
			centroids_y.emplace_back(sum_y / pixel_count);
		}
	}

	// get black pixels stat
	num_labels++;
	if (bg_pixel_count == 0) {
		bg_min_x = 0;
		bg_min_y = 0;
	}
	areas.emplace(areas.begin(), bg_pixel_count);
	lefts.emplace(lefts.begin(), bg_min_x);
	tops.emplace(tops.begin(), bg_min_y);
	widths.emplace(widths.begin(), bg_max_x - bg_min_x + 1);
	heights.emplace(heights.begin(), bg_max_y - bg_min_y + 1);
	centroids_x.emplace(centroids_x.begin(), bg_sum_x / bg_pixel_count);
	centroids_y.emplace(centroids_y.begin(), bg_sum_y / bg_pixel_count);

	vsapi->mapSetIntArray(props, "_CCLStatAreas", areas.data(), areas.size());
	vsapi->mapSetIntArray(props, "_CCLStatLefts", lefts.data(), lefts.size());
	vsapi->mapSetIntArray(props, "_CCLStatTops", tops.data(), tops.size());
	vsapi->mapSetIntArray(props, "_CCLStatWidths", widths.data(), widths.size());
	vsapi->mapSetIntArray(props, "_CCLStatHeights", heights.data(), heights.size());
	vsapi->mapSetFloatArray(props, "_CCLStatCentroids_x", centroids_x.data(), centroids_x.size());
	vsapi->mapSetFloatArray(props, "_CCLStatCentroids_y", centroids_y.data(), centroids_y.size());
	vsapi->mapSetInt(props, "_CCLStatNumLabels", num_labels, maReplace);
}

static const VSFrame* VS_CC CCSGetFrame(int n, int activationReason, void* instanceData, void** frameData, VSFrameContext* frameCtx, VSCore* core, const VSAPI* vsapi) {
	TMCData* d = static_cast<TMCData*>(instanceData);

	if (activationReason == arInitial) {
		vsapi->requestFrameFilter(n, d->node, frameCtx);
	}
	else if (activationReason == arAllFramesReady) {
#ifdef VS_TARGET_CPU_X86
		no_subnormals();
#endif
		const VSFrame* src = vsapi->getFrameFilter(n, d->node, frameCtx);
		const VSVideoFormat* fi = vsapi->getVideoFrameFormat(src);
		int height = vsapi->getFrameHeight(src, 0);
		int width = vsapi->getFrameWidth(src, 0);
		const VSFrame* fr[] = { src, src, src };
		const int pl[] = { 0, 1, 2 };
		VSFrame* dst = vsapi->newVideoFrame2(fi, width, height, fr, pl, src, core);
		int bits = d->vi->format.bitsPerSample;

		try {
			d->process_c_func(src, dst, bits, d, vsapi);
		}
		catch (const std::exception& e) {
			vsapi->setFilterError((std::string("TMaskCleanerMod error: ") + e.what()).c_str(), frameCtx);
		}

		vsapi->freeFrame(src);
		return dst;
	}
	return nullptr;
}

void VS_CC CCSCreate(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi) {
	auto d{ std::make_unique<TMCData>() };
	int err{ 0 };

	d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
	d->vi = vsapi->getVideoInfo(d->node);

	try {
		if (!vsh::isConstantVideoFormat(d->vi) || (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample > 16) || (d->vi->format.sampleType == stFloat))
			throw std::string("only constant format 8-16 bits integer input supported.");

		d->thresh = static_cast<unsigned int>(vsapi->mapGetInt(in, "thresh", 0, &err));
		if (err)
			d->thresh = 235;

		auto connectivity = static_cast<unsigned int>(vsapi->mapGetInt(in, "connectivity", 0, &err));
		if (err)
			connectivity = 8;

		if (d->thresh <= 0)
			throw std::string("thresh must be greater than zero.");

		if (connectivity != 4 && connectivity != 8)
			throw std::string("connectivity must be either 4 or 8.");

		if (connectivity == 4) {
			d->directions = directions4;
			d->dir_count = 4;
		}
		else {
			d->directions = directions8;
			d->dir_count = 8;
		}

		if (d->vi->format.bytesPerSample == 1) {
			d->process_c_func = &process_ccs<uint8_t>;
		}
		else if (d->vi->format.bytesPerSample == 2) {
			d->process_c_func = &process_ccs<uint16_t>;
		}
	}
	catch (const std::string& error) {
		vsapi->mapSetError(out, ("GetCCStats: " + error).c_str());
		vsapi->freeNode(d->node);
		return;
	}

	VSFilterDependency deps[] = { {d->node, rpStrictSpatial} };
	vsapi->createVideoFilter(out, "GetCCStats", d->vi, CCSGetFrame, FilterFree, fmParallel, deps, 1, d.get(), core);
	d.release();
}
