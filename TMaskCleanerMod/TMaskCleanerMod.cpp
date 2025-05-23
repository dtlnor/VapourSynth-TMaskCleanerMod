#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <memory>
#include <vector>
#include "VapourSynth4.h"
#include "VSHelper4.h"
#include <string>

typedef std::pair<int, int> Coordinates;

struct TMCData {
    VSNode * node;
    const VSVideoInfo * vi;
    unsigned int length;
    unsigned int thresh;
    unsigned int fade;
    unsigned int mode;
    unsigned int connectivity;
};

template<typename pixel_t>
bool is_white(pixel_t value, unsigned int thresh) {
    return value >= thresh;
}

template<typename pixel_t>
bool visited(int x, int y, int width, pixel_t *lookup, int bits) {
    unsigned int normal_pos = y * width + x;
    unsigned int byte_pos = normal_pos / bits;

    return lookup[byte_pos] & (1 << (normal_pos - byte_pos * bits));
}

template<typename pixel_t>
void visit(int x, int y, int width, pixel_t *lookup, int bits) {
    unsigned int normal_pos = y * width + x;
    unsigned int byte_pos = normal_pos / bits;

    lookup[byte_pos] |= (1 << (normal_pos - byte_pos * bits));
}

template<bool use4Way, bool keep_small, typename pixel_t>
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

    std::vector<Coordinates> coordinates;
    std::vector<Coordinates> white_pixels;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (visited<pixel_t>(x, y, width, lookup, bits) || !is_white<pixel_t>(srcptr[srcStride * y + x], d->thresh)) {
                continue;
            }
            coordinates.clear();
            white_pixels.clear();

            coordinates.emplace_back(x, y);

            while (!coordinates.empty()) {
                /* pop last coordinates */
                Coordinates current = coordinates.back();
                coordinates.pop_back();

                /* check surrounding positions */
                int x_min = current.first == 0 ? 0 : current.first - 1;
                int x_max = current.first == width - 1 ? width : current.first + 2;
                int y_min = current.second == 0 ? 0 : current.second - 1;
                int y_max = current.second == height - 1 ? height : current.second + 2;

                for (int j = y_min; j < y_max; ++j) {
                    for (int i = x_min; i < x_max; ++i) {
                        if constexpr (use4Way) {
                            // Skip diagonal pixels when using 4-way connectivity
                            if ((i == current.first || j == current.second) &&
                                !visited<pixel_t>(i, j, width, lookup, bits) &&
                                is_white<pixel_t>(srcptr[j * srcStride + i], d->thresh)) {
                                coordinates.emplace_back(i, j);
                                white_pixels.emplace_back(i, j);
                                visit<pixel_t>(i, j, width, lookup, bits);
                            }
                        }
                        else {
                            if (!visited<pixel_t>(i, j, width, lookup, bits) &&
                                is_white<pixel_t>(srcptr[j * srcStride + i], d->thresh)) {
                                coordinates.emplace_back(i, j);
                                white_pixels.emplace_back(i, j);
                                visit<pixel_t>(i, j, width, lookup, bits);
                            }
						}
                    }
                }
            }
            size_t pixels_count = white_pixels.size();
            if constexpr (!keep_small) {
                if (pixels_count >= d->length) {
                    if ((d->fade == 0) || (pixels_count - d->length > d->fade)) {
                        for (auto& pixel : white_pixels) {
                            dstptr[dstStride * pixel.second + pixel.first] = srcptr[srcStride * pixel.second + pixel.first];
                        }
                    }
                    else {
                        for (auto& pixel : white_pixels) {
                            dstptr[dstStride * pixel.second + pixel.first] = srcptr[srcStride * pixel.second + pixel.first] * (pixels_count - d->length) / d->fade;
                        }
                    }
                }
            }
            else {
                if (pixels_count <= d->length) {
                    if ((d->fade == 0) || (d->length - pixels_count > d->fade)) {
                        for (auto& pixel : white_pixels) {
                            dstptr[dstStride * pixel.second + pixel.first] = srcptr[srcStride * pixel.second + pixel.first];
                        }
                    }
                    else {
                        for (auto& pixel : white_pixels) {
                            dstptr[dstStride * pixel.second + pixel.first] = srcptr[srcStride * pixel.second + pixel.first] * (d->length - pixels_count) / d->fade;
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
        VSFrame* dst = vsapi->newVideoFrame(fi, width, height, src, core);
        int bits = d->vi->format.bitsPerSample;

        if (d->vi->format.bytesPerSample == 1) {
            if (d->mode == 1) {
                if (d->connectivity == 4)
                    process_c<true, true, uint8_t>(src, dst, bits, d, vsapi);
                else
                    process_c<false, true, uint8_t>(src, dst, bits, d, vsapi);
            } else {
                if (d->connectivity == 4)
                    process_c<true, false, uint8_t>(src, dst, bits, d, vsapi);
                else
                    process_c<false, false, uint8_t>(src, dst, bits, d, vsapi);
            }
        }
        else if (d->vi->format.bytesPerSample == 2) {
            if (d->mode == 1) {
                if (d->connectivity == 4)
                    process_c<true, true, uint16_t>(src, dst, bits, d, vsapi);
                else
                    process_c<false, true, uint16_t>(src, dst, bits, d, vsapi);
            } else {
                if (d->connectivity == 4)
                    process_c<true, false, uint16_t>(src, dst, bits, d, vsapi);
                else
                    process_c<false, false, uint16_t>(src, dst, bits, d, vsapi);
            }
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

        d->connectivity = static_cast<unsigned int>(vsapi->mapGetInt(in, "connectivity", 0, &err));
        if (err)
            d->connectivity = 8;

        d->mode = static_cast<float>(vsapi->mapGetInt(in, "mode", 0, &err));
        if (err)
            d->mode = 0;

        if (d->length <= 0 || d->thresh <= 0)
            throw std::string("length and thresh must be greater than zero.");

        if (d->fade < 0)
            throw std::string("fade cannot be negative.");

        if (d->connectivity != 4 && d->connectivity != 8)
            throw std::string("connectivity must be either 4 or 8.");

        if (d->mode < 0 || d->mode > 1)
            throw std::string("mode must be in the range [0, 1].");
    }
    catch (const std::string & error) {
        vsapi->mapSetError(out, ("TMaskCleanerMod: " + error).c_str());
        vsapi->freeNode(d->node);
        return;
    }

    VSFilterDependency deps[] = {{d->node, rpGeneral}};
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
        "mode:int:opt;",
        "clip:vnode;",
        TMCCreate, nullptr, plugin);
}
