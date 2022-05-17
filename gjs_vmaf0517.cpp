#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libvmaf/libvmaf.h"
#include "libvmaf/model.h"
#include "libvmaf/picture.h"
//#include "libvmaf/cli_parse.h"

extern "C"{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/pixdesc.h"
#include "libavutil/error.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
}

#define bool int
#define true 1
#define false 0
#define WIDTH 1280
#define HEIGHT 720
#define CLI_SETTINGS_STATIC_ARRAY_LEN 32

static int pix_fmt_map(char *fmt)
{
    if (fmt) {
        if (!strcmp(fmt, "yuv420p"))
            return VMAF_PIX_FMT_YUV420P;
        if (!strcmp(fmt, "yuv422p"))
            return VMAF_PIX_FMT_YUV422P;
        if (!strcmp(fmt, "yuv444p"))
            return VMAF_PIX_FMT_YUV444P;
        if (!strcmp(fmt, "yuv420p10le"))
            return VMAF_PIX_FMT_YUV420P;
        if (!strcmp(fmt, "yuv420p12le"))
            return VMAF_PIX_FMT_YUV420P;
        if (!strcmp(fmt, "yuv420p16le"))
            return VMAF_PIX_FMT_YUV420P;
        if (!strcmp(fmt, "yuv422p10le"))
            return VMAF_PIX_FMT_YUV422P;
        if (!strcmp(fmt, "yuv444p10le"))
            return VMAF_PIX_FMT_YUV444P;
    }

    return VMAF_PIX_FMT_UNKNOWN;

}
typedef struct {
    const char *name;
    VmafFeatureDictionary *opts_dict;
    void *buf;
} CLIFeatureConfig;

typedef struct {
    const char *path;
    const char *version;
    VmafModelConfig cfg;
    struct {
        const char *name;
        VmafFeatureDictionary *opts_dict;
    } feature_overload[CLI_SETTINGS_STATIC_ARRAY_LEN];
    unsigned overload_cnt;
    void *buf;
} CLIModelConfig;

typedef struct {
    char *path_ref, *path_dist;
    unsigned frame_cnt;
    unsigned width, height;
    enum VmafPixelFormat pix_fmt;
    unsigned bitdepth;
    bool use_yuv;
    char *output_path;
    enum VmafOutputFormat output_fmt;
    CLIModelConfig model_config[CLI_SETTINGS_STATIC_ARRAY_LEN];
    unsigned model_cnt;
    CLIFeatureConfig feature_cfg[CLI_SETTINGS_STATIC_ARRAY_LEN];
    unsigned feature_cnt;
    enum VmafLogLevel log_level;
    unsigned subsample;
    unsigned thread_cnt;
    bool no_prediction;
    bool quiet;
    unsigned cpumask;
} CLISettings;

void cli_free(CLISettings *settings)
{
    for (unsigned i = 0; i < settings->model_cnt; i++)
        free(settings->model_config[i].buf);
    for (unsigned i = 0; i < settings->feature_cnt; i++)
        free(settings->feature_cfg[i].buf);
}
static int copy_picture_data(AVFrame *src, VmafPicture *dst, unsigned bpc)
{
    //int err = vmaf_picture_alloc(dst, pix_fmt_map((AVPixelFormat)src->format), bpc, src->width, src->height);
    int err = vmaf_picture_alloc(dst, VMAF_PIX_FMT_YUV420P, bpc, src->width, src->height);
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

class LIBVMAFContext
{
public:
    const char *model_path;
    char *output_path;
    enum VmafOutputFormat output_fmt;
    int enable_transform;
    int phone_model;
    int frame_cnt;
    int bitdepth;
    int thread_cnt;
    CLIModelConfig model_config[CLI_SETTINGS_STATIC_ARRAY_LEN];
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
    unsigned bpc;
};
void gjs_vmaf_init(LIBVMAFContext *ctx) {
    ctx->frame_cnt = 10;
    ctx->bitdepth = 8;
    ctx->output_path = "out.txt";
    ctx->output_fmt = VMAF_OUTPUT_FORMAT_JSON;
    ctx->model_config[0].path = "/home/gjs/code/vmaf/model/vmaf_4k_v0.6.1.json";
    ctx->model_config[0].cfg.name = "vmaf";
    ctx->model_cnt = 1;
    ctx->thread_cnt = 1;
    int err = 0;

    VmafConfiguration cfg = {
        .log_level = VMAF_LOG_LEVEL_INFO,
        .n_threads = ctx->thread_cnt,// 0
        .n_subsample = ctx->n_subsample, // 0
        //.cpumask = c.cpumask, // 0
    };
    //VmafContext *vmaf;
    err = vmaf_init(&ctx->vmaf, cfg);

    //VmafModel **model;
    const size_t model_sz = sizeof(*ctx->model) * ctx->model_cnt;
    ctx->model = (VmafModel**)malloc(model_sz);
    memset(ctx->model, 0, model_sz);

    int i = 0;
    err = vmaf_model_load_from_path(&ctx->model[i], &ctx->model_config[i].cfg, ctx->model_config[i].path);
    err = vmaf_use_features_from_model(ctx->vmaf, ctx->model[i]);
}
void gjs_vmaf_uninit(LIBVMAFContext *ctx, int picture_index) {
    int err = 0;
    int i = 0;
    double vmaf_score;
    err |= vmaf_read_pictures(ctx->vmaf, NULL, NULL, 0);
    int index = 1;
    //err = vmaf_score_at_index(vmaf, model[i], &vmaf_score, index);
    
    err = vmaf_score_pooled(ctx->vmaf, ctx->model[i], VMAF_POOL_METHOD_MEAN, &vmaf_score, 0, picture_index - 2);
    printf("vamf score: %lf\n", vmaf_score);
    vmaf_write_output(ctx->vmaf, ctx->output_path, ctx->output_fmt);

    vmaf_model_destroy(ctx->model[i]);
    free(ctx->model);
    vmaf_close(ctx->vmaf);
}
void gjs_main2() {
    int err = 0;
    int i = 0;
    LIBVMAFContext *ctx = new LIBVMAFContext();
    gjs_vmaf_init(ctx);
    unsigned picture_index;    
    
    int bpc = ctx->bitdepth;
    for (picture_index = 0 ;; picture_index++) {
        if (ctx->frame_cnt && picture_index >= ctx->frame_cnt)
            break;
        VmafPicture pic_ref, pic_dist;
        AVFrame *ref = construct_dummy_frame(1);
        AVFrame *dist = construct_dummy_frame(2);

        err = copy_picture_data(ref, &pic_ref, bpc);
        err = copy_picture_data(dist, &pic_dist, bpc);
        err = vmaf_read_pictures(ctx->vmaf, &pic_ref, &pic_dist, picture_index);
    } 

    gjs_vmaf_uninit(ctx, picture_index);
    return;

}
void gjs_main() {
    CLISettings c;
    //c.path_ref = "/home/gjs/content/1.yuv";
    //c.path_dist = "/home/gjs/content/2.yuv";
    c.frame_cnt = 10;
    //c.width = 1280;
    //c.height = 720;
    //c.pix_fmt = VMAF_PIX_FMT_YUV420P;
    c.bitdepth = 8;
    //c.use_yuv = true;
    c.output_path = "out.txt";
    c.output_fmt = VMAF_OUTPUT_FORMAT_JSON;
    c.model_config[0].path = "/home/gjs/code/vmaf/model/vmaf_4k_v0.6.1.json";
    c.model_config[0].cfg.name = "vmaf";
    c.model_cnt = 1;
    c.thread_cnt = 1;

    int err = 0;

    VmafConfiguration cfg = {
        .log_level = VMAF_LOG_LEVEL_INFO,
        .n_threads = c.thread_cnt,// 0
        .n_subsample = c.subsample, // 0
        //.cpumask = c.cpumask, // 0
    };
    VmafContext *vmaf;
    err = vmaf_init(&vmaf, cfg);

    VmafModel **model;
    const size_t model_sz = sizeof(*model) * c.model_cnt;
    model = (VmafModel**)malloc(model_sz);
    memset(model, 0, model_sz);

    int i = 0;
    err = vmaf_model_load_from_path(&model[i], &c.model_config[i].cfg, c.model_config[i].path);
    err = vmaf_use_features_from_model(vmaf, model[i]);

    unsigned picture_index;    
    
    int bpc = c.bitdepth;
    for (picture_index = 0 ;; picture_index++) {
        if (c.frame_cnt && picture_index >= c.frame_cnt)
            break;
        VmafPicture pic_ref, pic_dist;
        AVFrame *ref = construct_dummy_frame(1);
        AVFrame *dist = construct_dummy_frame(2);

        err = copy_picture_data(ref, &pic_ref, bpc);
        err = copy_picture_data(dist, &pic_dist, bpc);
        err = vmaf_read_pictures(vmaf, &pic_ref, &pic_dist, picture_index);
    } 

    double vmaf_score;
    err |= vmaf_read_pictures(vmaf, NULL, NULL, 0);
    int index = 1;
    //err = vmaf_score_at_index(vmaf, model[i], &vmaf_score, index);
    
    err = vmaf_score_pooled(vmaf, model[i], VMAF_POOL_METHOD_MEAN, &vmaf_score, 0, picture_index - 2);
    printf("vamf score: %lf\n", vmaf_score);
    vmaf_write_output(vmaf, c.output_path, c.output_fmt);

    vmaf_model_destroy(model[i]);
    free(model);
    vmaf_close(vmaf);
    //cli_free(&c);
    return;
}
int main() {
    gjs_main2();
    return 0;
}
// g++ -g gjs_vmaf0517.cpp -lvmaf -lavutil

