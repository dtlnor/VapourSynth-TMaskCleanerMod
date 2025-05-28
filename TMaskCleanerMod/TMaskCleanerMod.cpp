#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <memory>
#include <vector>
#include "VapourSynth4.h"
#include "VSHelper4.h"
#include <string>
#include <deque>
#include <numeric>

struct TMCData;

typedef std::pair<int, int> Coordinates;
typedef void (*Process_c_Ptr)(const VSFrame*, VSFrame*, int, const TMCData*, const VSAPI*);

struct TMCData {
    VSNode * node;
    const VSVideoInfo * vi;
    unsigned int length;
    unsigned int thresh;
    unsigned int fade;
    Process_c_Ptr process_c_func;
    const Coordinates* directions;
    int dir_count;
};

template<typename pixel_t>
inline bool is_white(pixel_t value, unsigned int thresh) {
    return value >= thresh;
}

template<typename pixel_t>
inline bool visited(int x, int y, int width, pixel_t *lookup, int bits) {
    unsigned int normal_pos = y * width + x;
    unsigned int byte_pos = normal_pos / bits;

    return lookup[byte_pos] & (1 << (normal_pos - byte_pos * bits));
}

template<typename pixel_t>
inline void visit(int x, int y, int width, pixel_t *lookup, int bits) {
    unsigned int normal_pos = y * width + x;
    unsigned int byte_pos = normal_pos / bits;

    lookup[byte_pos] |= (1 << (normal_pos - byte_pos * bits));
}

constexpr Coordinates directions4[4] = { {0, -1}, {-1, 0}, {1, 0}, {0, 1} };
constexpr Coordinates directions8[8] = {
    {0, -1}, {-1, 0}, {1, 0}, {0, 1},
    {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
};

template<bool keep_less, typename pixel_t>
void setProcessFunction(TMCData* d, int mode) {
    switch (mode) {
    case 0: d->process_c_func = &process_c<0, keep_less, pixel_t>; break;
    case 1: d->process_c_func = &process_c<1, keep_less, pixel_t>; break;
    case 2: d->process_c_func = &process_c<2, keep_less, pixel_t>; break;
    case 3: d->process_c_func = &process_c<3, keep_less, pixel_t>; break;
    case 4: d->process_c_func = &process_c<4, keep_less, pixel_t>; break;
    case 5: d->process_c_func = &process_c<5, keep_less, pixel_t>; break;
    case 6: d->process_c_func = &process_c<6, keep_less, pixel_t>; break;
    case 7: d->process_c_func = &process_c<7, keep_less, pixel_t>; break;
    case 8: d->process_c_func = &process_c<8, keep_less, pixel_t>; break;
    default: throw std::string("mode must be in the range [0, 8].");
    }
}

template<int filter_mode, bool keep_less, typename pixel_t>
void process_c(const VSFrame * src, VSFrame * dst, int bits, const TMCData * d, const VSAPI * vsapi) {
    const pixel_t *srcptr = reinterpret_cast<const pixel_t *>(vsapi->getReadPtr(src, 0));
    pixel_t *VS_RESTRICT dstptr = reinterpret_cast<pixel_t *>(vsapi->getWritePtr(dst, 0));
    const int srcStride = vsapi->getStride(src, 0) / sizeof(pixel_t);
    const int dstStride = vsapi->getStride(dst, 0) / sizeof(pixel_t);
    int height = vsapi->getFrameHeight(src, 0);
    int width = vsapi->getFrameWidth(src, 0);
    memset(dstptr, 0, (dstStride * sizeof(pixel_t)) * vsapi->getFrameHeight(dst, 0));
    pixel_t *lookup = new pixel_t[height * width / bits];
    memset(lookup, 0, height * (width * sizeof(pixel_t)) / bits);

    std::deque<Coordinates> coordinates;
    std::vector<Coordinates> white_pixels;

    const auto& directions = d->directions;
    const int dir_count = d->dir_count;
    const double fade_inv = d->fade > 0 ? 1.0f / d->fade : 0.0f;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (visited<pixel_t>(x, y, width, lookup, bits) || !is_white<pixel_t>(srcptr[srcStride * y + x], d->thresh)) {
                continue;
            }
            coordinates.clear();
            white_pixels.clear();

            coordinates.emplace_back(x, y);
            white_pixels.emplace_back(x, y);
            visit<pixel_t>(x, y, width, lookup, bits);

            while (!coordinates.empty()) {
                /* pop first coordinates (BFS) */
                Coordinates current = coordinates.front();
                coordinates.pop_front();

                for (int dir = 0; dir < dir_count; dir++) {
                    int i = current.first + directions[dir].first;
                    int j = current.second + directions[dir].second;
                    if (i >= 0 && i < width && j >= 0 && j < height &&
                        !visited<pixel_t>(i, j, width, lookup, bits) &&
                        is_white<pixel_t>(srcptr[j * srcStride + i], d->thresh)) {
                        coordinates.emplace_back(i, j);
                        white_pixels.emplace_back(i, j);
                        visit<pixel_t>(i, j, width, lookup, bits);
                    }
                }
            }

            size_t component_value;
            if constexpr (filter_mode == 0) {
                // pixel count
                component_value = white_pixels.size();
            } else if constexpr (filter_mode == 1) {
                // centriod_x
                component_value = static_cast<size_t>(std::accumulate(white_pixels.begin(), white_pixels.end(), 0.0,
                    [](double sum, const Coordinates& p) { return sum + p.first; }) / white_pixels.size());
            } else if constexpr (filter_mode == 2) {
                // centriod_y
                component_value = static_cast<size_t>(std::accumulate(white_pixels.begin(), white_pixels.end(), 0.0,
                    [](double sum, const Coordinates& p) { return sum + p.second; }) / white_pixels.size());
            } else if constexpr (filter_mode == 3) {
                // min_x
                auto min_x = std::min_element(white_pixels.begin(), white_pixels.end(),
                    [](const Coordinates& a, const Coordinates& b) { return a.first < b.first; });
                component_value = min_x->first;
            } else if constexpr (filter_mode == 4) {
                // min_y
                auto min_y = std::min_element(white_pixels.begin(), white_pixels.end(),
                    [](const Coordinates& a, const Coordinates& b) { return a.second < b.second; });
                component_value = min_y->second;
            } else if constexpr (filter_mode == 5) {
                // max_x
                auto max_x = std::max_element(white_pixels.begin(), white_pixels.end(),
                    [](const Coordinates& a, const Coordinates& b) { return a.first < b.first; });
                component_value = max_x->first;
            } else if constexpr (filter_mode == 6) {
                // max_y
                auto max_y = std::max_element(white_pixels.begin(), white_pixels.end(),
                    [](const Coordinates& a, const Coordinates& b) { return a.second < b.second; });
                component_value = max_y->second;
            } else if constexpr (filter_mode == 7) {
                // width
                int min_x = width, max_x = -1;
                for (const auto& pixel : white_pixels) {
                    min_x = std::min(min_x, pixel.first);
                    max_x = std::max(max_x, pixel.first);
                }
                component_value = max_x - min_x + 1;
            } else if constexpr (filter_mode == 8) {
                // height
                int min_y = height, max_y = -1;
                for (const auto& pixel : white_pixels) {
                    min_y = std::min(min_y, pixel.second);
                    max_y = std::max(max_y, pixel.second);
                }
                component_value = max_y - min_y + 1;
            }

            if constexpr (!keep_less) {
                if (component_value >= d->length) {
                    if ((d->fade == 0) || (component_value - d->length > d->fade)) {
                        for (const auto& pixel : white_pixels) {
                            const auto dst_pos = dstStride * pixel.second + pixel.first;
                            const auto src_pos = srcStride * pixel.second + pixel.first;
                            dstptr[dst_pos] = srcptr[src_pos];
                        }
                    }
                    else {
                        for (const auto& pixel : white_pixels) {
                            const auto dst_pos = dstStride * pixel.second + pixel.first;
                            const auto src_pos = srcStride * pixel.second + pixel.first;
                            dstptr[dst_pos] = srcptr[src_pos] * (component_value - d->length) * fade_inv;
                        }
                    }
                }
            }
            else {
                if (component_value <= d->length) {
                    if ((d->fade == 0) || (d->length - component_value > d->fade)) {
                        for (const auto& pixel : white_pixels) {
                            const auto dst_pos = dstStride * pixel.second + pixel.first;
                            const auto src_pos = srcStride * pixel.second + pixel.first;
                            dstptr[dst_pos] = srcptr[src_pos];
                        }
                    }
                    else {
                        for (const auto& pixel : white_pixels) {
                            const auto dst_pos = dstStride * pixel.second + pixel.first;
                            const auto src_pos = srcStride * pixel.second + pixel.first;
                            dstptr[dst_pos] = srcptr[src_pos] * (d->length - component_value) * fade_inv;
                        }
                    }
                }
            }
        }
    }
    delete[] lookup;
}

static const VSFrame *VS_CC TMCGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    TMCData * d = static_cast<TMCData *>(instanceData);

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
        const VSFrame* fr[] = { nullptr, src, src };
        const int pl[] = { 0, 1, 2 };
        VSFrame* dst = vsapi->newVideoFrame2(fi, width, height, fr, pl, src, core);
        int bits = d->vi->format.bitsPerSample;

        try {
            d->process_c_func(src, dst, bits, d, vsapi);
        } catch (const std::exception &e) {
            vsapi->setFilterError((std::string("TMaskCleanerMod error: ") + e.what()).c_str(), frameCtx);
        }

        vsapi->freeFrame(src);
        return dst;
    }
    return nullptr;
}

static void VS_CC TMCFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    auto d{ static_cast<TMCData *>(instanceData) };
    vsapi->freeNode(d->node);
    delete d;
}

void VS_CC TMCCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    auto d{ std::make_unique<TMCData>() };
    int err{ 0 };

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d->vi = vsapi->getVideoInfo(d->node);

    try {
        if (!vsh::isConstantVideoFormat(d->vi) || (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample > 16) || (d->vi->format.sampleType == stFloat))
            throw std::string("only constant format 8-16 bits integer input supported.");

        d->length = static_cast<float>(vsapi->mapGetInt(in, "length", 0, &err));
        if (err)
            d->length = 5;

        d->thresh = static_cast<float>(vsapi->mapGetInt(in, "thresh", 0, &err));
        if (err)
            d->thresh = 235;

        d->fade = static_cast<float>(vsapi->mapGetInt(in, "fade", 0, &err));
        if (err)
            d->fade = 0;

        auto connectivity = static_cast<unsigned int>(vsapi->mapGetInt(in, "connectivity", 0, &err));
        if (err)
            connectivity = 8;

        auto keep_less = static_cast<unsigned int>(vsapi->mapGetInt(in, "keepless", 0, &err));
        if (err)
            keep_less = 0;

        auto mode = static_cast<int>(vsapi->mapGetInt(in, "mode", 0, &err));
        if (err)
            mode = 0;

        if (d->length <= 0 || d->thresh <= 0)
            throw std::string("length and thresh must be greater than zero.");

        if (d->fade < 0)
            throw std::string("fade cannot be negative.");

        if (connectivity != 4 && connectivity != 8)
            throw std::string("connectivity must be either 4 or 8.");

        if (keep_less != 0 && keep_less != 1)
            throw std::string("keepless must be either 0 or 1.");

        if (mode < 0 || mode > 8)
            throw std::string("mode must be in the range [0, 8].");
        
        if (connectivity == 4) {
            d->directions = directions4;
            d->dir_count = 4;
        }
        else {
            d->directions = directions8;
            d->dir_count = 8;
        }

        if (d->vi->format.bytesPerSample == 1) {
            if (keep_less) {
                setProcessFunction<true, uint8_t>(d.get(), mode);
            } else {
                setProcessFunction<false, uint8_t>(d.get(), mode);
            }
        } else if (d->vi->format.bytesPerSample == 2) {
            if (keep_less) {
                setProcessFunction<true, uint16_t>(d.get(), mode);
            }
            else {
                setProcessFunction<false, uint16_t>(d.get(), mode);
            }
        }
    }
    catch (const std::string & error) {
        vsapi->mapSetError(out, ("TMaskCleanerMod: " + error).c_str());
        vsapi->freeNode(d->node);
        return;
    }

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "TMaskCleanerMod", d->vi, TMCGetFrame, TMCFree, fmParallel, deps, 1, d.get(), core);
    d.release();
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.dtlnor.tmcm", "tmcm", "A really simple mask cleaning plugin for VapourSynth based on mt_hysteresis.", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);

    vspapi->registerFunction("TMaskCleanerMod",
        "clip:vnode;"
        "length:int:opt;"
        "thresh:int:opt;"
        "fade:int:opt;"
        "connectivity:int:opt;"
        "keepless:int:opt;"
        "mode:int:opt;",
        "clip:vnode;",
        TMCCreate, nullptr, plugin);
}
