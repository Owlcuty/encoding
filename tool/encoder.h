#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
//#define NDEBUG

#include <time.h>


#include <errno.h>

#include <libavcodec/avcodec.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavutil/imgutils.h>

#include <libswscale/swscale.h>

#include <libavformat/avformat.h>


#define STREAM_DURATION		60.0
#define STREAM_FRAME_RATE	10 /* 25 fps */
#define STREAM_PIX_FMT		AV_PIX_FMT_YUV420P /* default pix_fmt */
#define SCALE_FLAGS			0

#include "bmp.h"


#define AV_CODEC_FLAG_GLOBAL_HEADER (1 << 22)
#define CODEC_FLAG_GLOBAL_HEADER AV_CODEC_FLAG_GLOBAL_HEADER
#define AVFMT_RAWPICTURE 0x0020

#define MAIN_DEBUG_SESSION
//#define MAIN_LOOP_DEBUG_SESSION

#ifdef MAIN_DEBUG_SESSION
#define ERRPRINTF(format, ...)	fprintf(stderr, "%d::%s::%s__::__ " format "\n", __LINE__, __FILENAME__, __PRETTY_FUNCTION__, ## __VA_ARGS__)
#else
#define ERRPRINTF(format, ...) 
#endif

//#define FFVER_3_0


typedef struct dict_codec_context
{
	const AVDictionaryEntry *param;
	size_t cnt;
} dict_ccontext_t;


typedef int errno_t;

// a wrapper around a single output AVStream
typedef struct OutputStream {
	AVStream *st;
	/* pts of the next frame that will be generated */
	int64_t next_pts;
	int samples_count;
	AVFrame *frame;
	AVFrame *tmp_frame;
	float t, tincr, tincr2;
} OutputStream;

typedef struct EncoderParameters {
	AVCodec				*codec;
	AVCodecParameters 	*cparams; // neccessary ?
	AVFormatContext		*oc;
	AVDictionary		*opt;
	AVOutputFormat		*fmt;
	OutputStream		video_st;
	int			encode_video;
	int			have_video;
} Enc_params_t;


/**
 * @brief 
 * @param filename
 * @param opt
 */
errno_t set_preset(const char* filename, AVDictionary** opt);

/**
 * @brief 
 * @param data
 * @param filename
 * @param frame_ind
 */
errno_t load_frame(framedata_t** data, const char *filename, size_t frame_ind);

/**
 * @brief 
 * @param fmt_ctx
 * @param pkt
 */
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt);

/**
 * @brief 
 * @param fmt_ctx
 * @param time_base
 * @param st
 * @param pkt
 */
static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt);

/**
 * @brief 
 * @param pix_fmt
 * @param width
 * @param height
 */
static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height);

/**
 * @brief 
 * @param oc
 * @param codec
 * @param ost
 * @param opt_arg
 */
static void open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg);

/**
 * @brief 
 * @param pict
 * @param width
 * @param height
 * @param bmp
 */
static void fill_yuv_image(AVFrame *pict, int width, int height, framedata_t bmp);

/**
 * @brief 
 * @param ctx
 * @param opt
 */
void set_dict_context(const dict_ccontext_t ctx, AVDictionary **opt);

/**
 * @brief 
 * @param filename
 * @param codec_name
 * @param width
 * @param height
 * @param preset_filename
 * @return 
 */
Enc_params_t *encoder_create(const char *filename,
							 const char *codec_name,
							 int width, int height,
							 const char *preset_filename);

/**
 * @brief 
 * @param ost
 * @param oc
 * @param codec
 * @param cparams
 */
void add_stream(OutputStream *ost, AVFormatContext *oc,
					   AVCodec *codec,
					   const AVCodecParameters *cparams);

/**
 * @brief 
 * @param ost
 * @param bmp
 */
static AVFrame *get_video_frame(OutputStream *ost, framedata_t bmp);

/**
 * @brief 
 * @param oc
 * @param ost
 * @param bmp
 */
static int write_video_frame(AVFormatContext *oc, OutputStream *ost, framedata_t bmp);

/**
 * @brief 
 * @param oc
 * @param ost
 */
static void close_stream(AVFormatContext *oc, OutputStream *ost);

/**
 * @brief 
 * 
 * Adding frames for format I/O context (params->oc)
 * 
 * @param params for encoding frame
 * @param data framedata_t* [[uint8_t*]] rgb24p frame or const char* format_frame_file_name
 * @param type  0 is for `framedata_t* [[uint8_t*]] rgb24p frame`
 * 				1 is for `const char* format_frame_file_name`
 * @return zero on success, an errno on failure. Set errno on EIVAL or ENOMEM
 */
errno_t encoder_add_frame(Enc_params_t *params, size_t frame_ind, const void *data_, int type);

/**
 * @brief 
 * @param params
 */
errno_t encoder_write(Enc_params_t *params);

/**
 * @brief 
 * @param params
 */
void encoder_destruct(Enc_params_t* params);