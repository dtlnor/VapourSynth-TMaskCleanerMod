#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <stdint.h>
#include <memory>
#include <vector>
#include <VapourSynth.h>
#include <VSHelper.h>

typedef std::pair<int, int> Coordinates;

struct TMCData {
    VSNodeRef * node;
    VSVideoInfo vi;
    unsigned int length;
    unsigned int thresh;
    unsigned int fade;
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

template<typename pixel_t>
void process_c(const VSFrameRef * src, VSFrameRef * dst, int bits, const TMCData * d, const VSAPI * vsapi) {
    const pixel_t *srcptr = reinterpret_cast<const pixel_t *>(vsapi->getReadPtr(src, 0));
    pixel_t *VS_RESTRICT dstptr = reinterpret_cast<pixel_t *>(vsapi->getWritePtr(dst, 0));
    const int srcStride = vsapi->getStride(src, 0) / sizeof(pixel_t);
    const int dstStride = vsapi->getStride(dst, 0) / sizeof(pixel_t);
    memset(dstptr, 0, (dstStride * sizeof(pixel_t)) * vsapi->getFrameHeight(dst, 0));
    pixel_t *lookup = new pixel_t[d->vi.height * d->vi.width / bits];
    memset(lookup, 0, d->vi.height * (d->vi.width * sizeof(pixel_t)) / bits);

    std::vector<Coordinates> coordinates;
    std::vector<Coordinates> white_pixels;

    for (int y = 0; y < d->vi.height; ++y) {
        for (int x = 0; x < d->vi.width; ++x) {
            if (visited<pixel_t>(x, y, d->vi.width, lookup, bits) || !is_white<pixel_t>(srcptr[srcStride * y + x], d->thresh)) {
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
                int x_max = current.first == d->vi.width - 1 ? d->vi.width : current.first + 2;
                int y_min = current.second == 0 ? 0 : current.second - 1;
                int y_max = current.second == d->vi.height - 1 ? d->vi.height : current.second + 2;

                for (int j = y_min; j < y_max; ++j) {
                    for (int i = x_min; i < x_max; ++i) {
                        if (!visited<pixel_t>(i, j, d->vi.width, lookup, bits) && is_white<pixel_t>(srcptr[j * srcStride + i], d->thresh)) {
                            coordinates.emplace_back(i, j);
                            white_pixels.emplace_back(i, j);
                            visit<pixel_t>(i, j, d->vi.width, lookup, bits);
                        }
                    }
                }
            }
            size_t pixels_count = white_pixels.size();
            if (pixels_count >= d->length) {
                if ((pixels_count - d->length > d->fade) || (d->fade == 0)) {
                    for (auto &pixel : white_pixels) {
                        dstptr[dstStride * pixel.second + pixel.first] = srcptr[srcStride * pixel.second + pixel.first];
                    }
                }
                else {
                    for (auto &pixel : white_pixels) {
                        dstptr[dstStride * pixel.second + pixel.first] = srcptr[srcStride * pixel.second + pixel.first] * (pixels_count - d->length) / d->fade;
                    }
                }
            }
        }
    }
    delete[] lookup;
}

static void VS_CC TMСInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    TMCData * d = static_cast<TMCData *>(*instanceData);
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC TMСGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    TMCData * d = static_cast<TMCData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    }
    else if (activationReason == arAllFramesReady) {
#ifdef VS_TARGET_CPU_X86
        no_subnormals();
#endif
        const VSFrameRef * src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrameRef * fr[] = { nullptr, src, src };
        const int pl[] = { 0, 1, 2 };
        VSFrameRef * dst = vsapi->newVideoFrame2(d->vi.format, d->vi.width, d->vi.height, fr, pl, src, core);
        int bits = d->vi.format->bitsPerSample;
        if (d->vi.format->bytesPerSample == 1) {
            process_c<uint8_t>(src, dst, bits, d, vsapi);
        } else if(d->vi.format->bytesPerSample == 2) {
            process_c<uint16_t>(src, dst, bits, d, vsapi);
        }
        vsapi->freeFrame(src);
        return dst;
    }
    return nullptr;
}

static void VS_CC TMСFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    TMCData * d = static_cast<TMCData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

void VS_CC TMCCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<TMCData> d{ new TMCData{} };
    int err;

    d->node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d->vi = *vsapi->getVideoInfo(d->node);

    try {
        if (!isConstantFormat(&d->vi) || (d->vi.format->sampleType == stInteger && d->vi.format->bitsPerSample > 16) || (d->vi.format->sampleType == stFloat))
            throw std::string{ "TMaskCleaner: only constant format 8-16 bits integer input supported." };

        d->length = static_cast<float>(vsapi->propGetInt(in, "length", 0, &err));
        if (err)
            d->length = 5;

        d->thresh = static_cast<float>(vsapi->propGetInt(in, "thresh", 0, &err));
        if (err)
            d->thresh = 235;

        d->fade = static_cast<float>(vsapi->propGetInt(in, "fade", 0, &err));
        if (err)
            d->fade = 0;

        if (d->length <= 0 || d->thresh <= 0)
            throw std::string{ "TMaskCleaner: length and thresh must be greater than zero." };

        if (d->fade < 0)
            throw std::string{ "TMaskCleaner: fade cannot be negative." };
    }
    catch (const std::string & error) {
        vsapi->setError(out, ("TMaskCleaner: " + error).c_str());
        vsapi->freeNode(d->node);
        return;
    }
    vsapi->createFilter(in, out, "TMaskCleaner", TMСInit, TMСGetFrame, TMСFree, fmParallel, 0, d.release(), core);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.djatom.tmc", "tmc", "A really simple mask cleaning plugin for VapourSynth based on mt_hysteresis.", VAPOURSYNTH_API_VERSION, 1, plugin);

    registerFunc("TMaskCleaner",
        "clip:clip;"
        "length:int:opt;"
        "thresh:int:opt;"
        "fade:int:opt;",
        TMCCreate, nullptr, plugin);
}