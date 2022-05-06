// g++ vmaf.cpp  -lvmaf -lavutil
#include<iostream>
#include "libvmaf/libvmaf.h"
#include "libvmaf/model.h"
extern "C"{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/pixdesc.h"
#include "libavutil/error.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
}
using namespace std;

#define WIDTH 176
#define HEIGHT 144

class LIBVMAFContext
{
    public:
    //const AVClass *class;
    //FFFrameSync fs;
    char *model_path;
    char *log_path;
    char *log_fmt;
    int enable_transform;
    int phone_model;
    int psnr;
    int ssim;
    int ms_ssim;
    char *pool;
    unsigned int n_threads;
    int n_subsample;
    int enable_conf_interval;
    char *model_cfg;
    char *feature_cfg;
    VmafContext *vmaf;
    VmafModel **model;
    unsigned model_cnt;
    unsigned frame_cnt;
    unsigned bpc;
};
void get_parameters(LIBVMAFContext *ctx)
{
    //ctx->model_path = (char*)"/home/gjs/code/vmaf/vmaf/model/vmaf_4k_v0.6.1.json";
    ctx->model_path = (char*)"/home/gjs/code/vmaf/model/vmaf_v0.6.1.json";
    ctx->log_fmt = (char*)"xml";
    ctx->log_path = (char*)"out.txt";
    ctx->n_threads = 0;
    ctx->psnr = 0;
    ctx->frame_cnt = 0;
    ctx->bpc = 8;
    ctx->model_cnt = 1;
    //s->model = av_calloc(s->model_cnt, sizeof(*s->model));
    ctx->model = (VmafModel**)av_calloc(ctx->model_cnt, sizeof(*ctx->model));
    memset(ctx->model, 0, sizeof(*ctx->model));
}
int init(LIBVMAFContext *ctx)
{
    get_parameters(ctx);
    int ret = 0;
    VmafConfiguration cfg = {
        .log_level = VMAF_LOG_LEVEL_INFO,
        .n_threads = ctx->n_threads,
        .n_subsample = 0, // c.subsample,
        .cpumask = 0, //c.cpumask,
    };
    VmafContext *vmaf;
    ret = vmaf_init(&ctx->vmaf, cfg);

    VmafModelConfig model_cfg = {
        .name = "vmaf",
        .flags = VMAF_MODEL_FLAGS_DEFAULT,
    };
    ret = vmaf_model_load_from_path(&ctx->model[0], &model_cfg, ctx->model_path);

    ret = vmaf_use_features_from_model(ctx->vmaf, ctx->model[0]);
    //printf("!! %d\n", ctx->model->n_features);

    if (ctx->psnr) {
        VmafFeatureDictionary *d = NULL;
        vmaf_feature_dictionary_set(&d, "enable_chroma", "false");
        ret = vmaf_use_feature(vmaf, "psnr", d);
    }
    return ret;
}
static enum VmafOutputFormat log_fmt_map(const char *log_fmt)
{
    if (log_fmt) {
        if (av_stristr(log_fmt, "xml"))
            return VMAF_OUTPUT_FORMAT_XML;
        if (av_stristr(log_fmt, "json"))
            return VMAF_OUTPUT_FORMAT_JSON;
        if (av_stristr(log_fmt, "csv"))
            return VMAF_OUTPUT_FORMAT_CSV;
        if (av_stristr(log_fmt, "sub"))
            return VMAF_OUTPUT_FORMAT_SUB;
    }

    return VMAF_OUTPUT_FORMAT_XML;
}
static enum VmafPoolingMethod pool_method_map(const char *pool_method)
{
    if (pool_method) {
        if (av_stristr(pool_method, "min"))
            return VMAF_POOL_METHOD_MIN;
        if (av_stristr(pool_method, "mean"))
            return VMAF_POOL_METHOD_MEAN;
        if (av_stristr(pool_method, "harmonic_mean"))
            return VMAF_POOL_METHOD_HARMONIC_MEAN;
    }

    return VMAF_POOL_METHOD_MEAN;
}
int uninit(LIBVMAFContext *ctx)
{
    int ret = 0;
    ret = vmaf_read_pictures(ctx->vmaf, NULL, NULL, 0);
////
/*
for (unsigned i = 0; i < ctx->model->n_features; i++) {
    printf("model->feature[%d].name = %s \n", i, ctx->model->feature[i].name);
}
*/
////
    double vmaf_score;
    //int vmaf_score_at_index(VmafContext *vmaf, VmafModel *model, double *score, unsigned index);
    unsigned index = 1;
    ret = vmaf_score_at_index(ctx->vmaf, ctx->model[0], &vmaf_score, index);
    cout << vmaf_score << endl;
    //ret = vmaf_score_pooled(ctx->vmaf, ctx->model, pool_method_map(ctx->pool),
    //                        &vmaf_score, 0, ctx->frame_cnt - 1);
    //ret = vmaf_score_pooled(ctx->vmaf, ctx->model, VMAF_POOL_METHOD_MEAN, &vmaf_score, 0, ctx->frame_cnt - 1);
    if (ctx->log_path && !ret)
        vmaf_write_output(ctx->vmaf, ctx->log_path, log_fmt_map(ctx->log_fmt));

    vmaf_model_destroy(ctx->model[0]);
    av_free(ctx->model[0]);

    ret = vmaf_close(ctx->vmaf);
    return ret;
}
static enum VmafPixelFormat pix_fmt_map(enum AVPixelFormat av_pix_fmt)
{
    switch (av_pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV420P10LE:
    case AV_PIX_FMT_YUV420P12LE:
    case AV_PIX_FMT_YUV420P16LE:
        return VMAF_PIX_FMT_YUV420P;
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV422P10LE:
    case AV_PIX_FMT_YUV422P12LE:
    case AV_PIX_FMT_YUV422P16LE:
        return VMAF_PIX_FMT_YUV422P;
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUV444P10LE:
    case AV_PIX_FMT_YUV444P12LE:
    case AV_PIX_FMT_YUV444P16LE:
        return VMAF_PIX_FMT_YUV444P;
    default:
        return VMAF_PIX_FMT_UNKNOWN;
    }
}

static int copy_picture_data(AVFrame *src, VmafPicture *dst, unsigned bpc)
{
    int err = vmaf_picture_alloc(dst, pix_fmt_map((AVPixelFormat)src->format), bpc,
                                 src->width, src->height);
    if (err)
        return AVERROR(ENOMEM);

    for (unsigned i = 0; i < 3; i++) {
        uint8_t *src_data = src->data[i];
        uint8_t *dst_data = (uint8_t *)dst->data[i];
        for (unsigned j = 0; j < dst->h[i]; j++) {
            memcpy(dst_data, src_data, sizeof(*dst_data) * dst->w[i]);
            src_data += src->linesize[i];
            dst_data += dst->stride[i];
        }
    }

    return 0;
}
AVFrame * construct_dummy_frame(int i) {
    int ret = 0;
    int x = 0, y = 0;
    AVFrame *frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width  = WIDTH;
    frame->height = HEIGHT;
    ret = av_frame_get_buffer(frame, 0);
    ret = av_frame_make_writable(frame);

    /* prepare a dummy image */
    /* Y */
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
        }
    }
    /* Cb and Cr */
    for (y = 0; y < HEIGHT/2; y++) {
        for (x = 0; x < WIDTH/2; x++) {
            frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
            frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
        }
    }
    frame->pts = i;
    return frame;
}
int read_picture(LIBVMAFContext *ctx, AVFrame *ref, AVFrame *dist)
{
    VmafPicture pic_ref, pic_dist;
    //AVFrame *ref, *dist;
    int err = 0;

    err = copy_picture_data(ref, &pic_ref, ctx->bpc);
    if (err) {
        return AVERROR(ENOMEM);
    }

    err = copy_picture_data(dist, &pic_dist, ctx->bpc);
    if (err) {
        vmaf_picture_unref(&pic_ref);
        return AVERROR(ENOMEM);
    }

    err = vmaf_read_pictures(ctx->vmaf, &pic_ref, &pic_dist, ctx->frame_cnt++);
    //err = vmaf_read_pictures(ctx->vmaf, &pic_ref, &pic_dist, 3);
    if (err) {
        return AVERROR(EINVAL);
    }

    return 0;
}
int main()
{
    int ret = 0;
    LIBVMAFContext *ctx = new LIBVMAFContext();
    ret = init(ctx);
    AVFrame *ref = construct_dummy_frame(1);
    AVFrame *dist = construct_dummy_frame(2);
    for(int i = 0; i < 5; ++i) {
        read_picture(ctx, ref, dist);
        ctx->frame_cnt++;
    }
    av_frame_free(&ref);
    av_frame_free(&dist);
    ret = uninit(ctx);
    delete ctx;
    return 0;
}
