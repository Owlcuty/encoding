#include "encoder.h"

#include <libavcodec/avcodec.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavutil/imgutils.h>

#include <libswscale/swscale.h>

#include <libavformat/avformat.h>

#include "bmp.h"

#define STREAM_DURATION		60.0
#define STREAM_FRAME_RATE	10 /* 25 fps */
#define STREAM_PIX_FMT		AV_PIX_FMT_YUV420P /* default pix_fmt */
#define SCALE_FLAGS			0

#define AV_CODEC_FLAG_GLOBAL_HEADER (1 << 22)
#define CODEC_FLAG_GLOBAL_HEADER AV_CODEC_FLAG_GLOBAL_HEADER
#define AVFMT_RAWPICTURE 0x0020

#define ENC_DEBUG_SESSION
//#define MAIN_LOOP_DEBUG_SESSION

#ifndef __FILENAME__
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#ifndef ERRPRINTF
#ifdef ENC_DEBUG_SESSION
#define ERRPRINTF(format, ...)	fprintf(stderr, "%d::%s::%s__::__ " format "\n", __LINE__, __FILENAME__, __PRETTY_FUNCTION__, ## __VA_ARGS__)
#else
#define ERRPRINTF(format, ...) 
#endif
#endif

const int EP_CODEC_ID_VP8_ = AV_CODEC_ID_VP8;
const int EP_CODEC_ID_VP9_ = AV_CODEC_ID_VP9;

// a wrapper around a single output AVStream
typedef struct OutputStream {
	AVStream *st;
	/* pts of the next frame that will be generated */
	int64_t next_pts;
	AVFrame *frame;
	AVFrame *tmp_frame;
} OutputStream;

/**
 * @class EncoderParameters
 * @date 09/11/20
 * @file encoder.h
 * @brief General encoder parameters struct
 */
typedef struct EncoderParameters {
	AVCodec			*codec;
	AVCodecParameters 	*cparams;
	AVCodecContext		*ctx;
	AVFormatContext		*oc;
	AVDictionary		*opt;
	AVOutputFormat		*fmt;
	OutputStream		video_st;
	int			encode_video;
	int			have_video;
} Enc_params_t;

int EP_get_encode_video(Enc_params_t *params)
{
	return params->encode_video;
}


typedef struct dict_codec_context
{
	const AVDictionaryEntry *param;
	size_t cnt;
} dict_ccontext_t;

/* vp8 context -----------*/
const AVDictionaryEntry _vp8_dict[] = {
	{"deadline", "realtime"},
	{"cpu-used", "16"},
	{"lag-in-frames", "16"},
	{"vprofile", "0"},
	{"qmax", "63"},
	{"qmin", "0"},
	{"b", "768k"},
	{"g", "120"},

	{"maxrate", "1.5M"},
	{"minrate", "40k"},
	{"auto-alt-ref", "1"},
	{"arnr-maxframes", "7"},
	{"arnr-strength", "5"},
	{"arnr-type", "centered"}
};

const dict_ccontext_t _vp8_context = {
	_vp8_dict,
	14
};

//--------------------------

/* vp9 context -----------*/
const AVDictionaryEntry _vp9_dict[] = {
	{"deadline", "realtime"},
	{"cpu-used", "8"},
	{"lag-in-frames", "16"},
	{"vprofile", "0"},
	{"qmax", "63"},
	{"qmin", "0"},
	{"b", "768k"},
	{"g", "120"},

	{"maxrate", "1.5M"},
	{"minrate", "40k"},
	{"auto-alt-ref", "1"},
	{"arnr-maxframes", "15"},
	{"arnr-strength", "5"},
	{"arnr-type", "centered"}
};

const dict_ccontext_t _vp9_context = {
	_vp9_dict,
	14
};

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
	AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
	printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
			av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
			av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
			av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
			pkt->stream_index);
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
	/* rescale output packet timestamp values from codec to stream timebase */
	av_packet_rescale_ts(pkt, *time_base, st->time_base);
	pkt->stream_index = st->index;
	/* Write the compressed frame to the media file. */
#ifndef MAIN_LOOP_DEBUG_SESSION
	log_packet(fmt_ctx, pkt);
#endif
	return av_interleaved_write_frame(fmt_ctx, pkt);
}

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
	AVFrame *picture = NULL;
	picture = av_frame_alloc();

	if (picture == NULL)
	{
		errno = ENOMEM;
		return NULL;
	}

	picture->format = pix_fmt;
	picture->width  = width;
	picture->height = height;
	
	int ret = 0;
	/* allocate the buffers for the frame data */
	ret = av_frame_get_buffer(picture, 32);
	if (ret < 0)
	{
		errno = ret;
		ERRPRINTF("Could not allocate frame data.");
		return NULL;
	}
	return picture;
}

static int open_video(Enc_params_t *params)
{
	int ret = 0;
	AVCodecContext *c = params->ctx;
	OutputStream *ost = &(params->video_st);
	AVDictionary *opt = NULL;
	av_dict_copy(&opt, params->opt, 0);
	/* open the codec */
	ret = avcodec_open2(c, params->codec, &opt);
	av_dict_free(&opt);
	if (ret < 0) {
		ERRPRINTF("Could not open video codec: %s", av_err2str(ret));
		return ret;
	}

	/* allocate and init a re-usable frame */
	ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
	if (!ost->frame)
	{
		ERRPRINTF("Could not allocate video frame");
		return AVERROR(errno);
	}

	/* If the output format is not YUV420P, then a temporary YUV420P
	* picture is needed too. It is then converted to the required
	* output format. */
	ost->tmp_frame = NULL;
	if (c->pix_fmt != AV_PIX_FMT_YUV420P)
	{
		ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
		if (!ost->tmp_frame)
		{
			ERRPRINTF("Could not allocate temporary picture");
			return AVERROR(errno);
		}
	}

	return 0;
}

static int fill_yuv_image(AVFrame *pict, int width, int height, framedata_t bmp)
{
	struct SwsContext *sws_ctx = NULL;
	AVFrame *frame1 = NULL;

	sws_ctx = sws_getContext(width, height, AV_PIX_FMT_RGB24,
												width, height, AV_PIX_FMT_YUV420P,
												0, NULL, NULL, NULL);
	if (!sws_ctx)
	{
		ERRPRINTF("Bad sws_getContext");
		errno = ENOMEM;
		goto err;
	}

	frame1 = alloc_picture(AV_PIX_FMT_RGB24, width, height);
	av_image_fill_arrays(frame1->data, frame1->linesize, (const uint8_t*)bmp, AV_PIX_FMT_RGB24, width, height, 1);

	int ret = av_image_alloc(pict->data, pict->linesize, pict->width, pict->height, AV_PIX_FMT_YUV420P, 32);
	if (ret < 0)
	{
		ERRPRINTF("Could not allocate raw picture buffer");
		errno = ENOMEM;
		goto err;
	}

	sws_scale(sws_ctx, (const uint8_t * const *)frame1->data, frame1->linesize, 0,
				height, pict->data, pict->linesize);

	av_frame_free(&frame1);

	return 0;

	err:
		av_frame_free(&frame1);

		return AVERROR(errno);
}

void set_dict_context(const dict_ccontext_t ctx, AVDictionary **opt)
{
	for (size_t p_id = 0; p_id < ctx.cnt; p_id++)
	{
		av_dict_set(opt, ctx.param[p_id].key, ctx.param[p_id].value, 0);
	}
}

/* Add an output stream. */
int add_stream(Enc_params_t *params)
{
	int ret = 0;

	OutputStream *ost = &(params->video_st);
	ost->st = avformat_new_stream(params->oc, params->codec);
	if (ost->st == NULL) {
		ERRPRINTF("Could not allocate stream");
		return AVERROR(errno);
	}
	ost->st->id = params->oc->nb_streams - 1;
	ret = avcodec_parameters_to_context(params->ctx, params->cparams);
	if (ret < 0)
	{
		return ret;
	}

	switch (params->codec->type)
	{
		case AVMEDIA_TYPE_AUDIO:
			ost->st->time_base = (AVRational){ 1, ost->st->codecpar->sample_rate };

			params->ctx->sample_fmt = params->codec->sample_fmts ?
										 params->codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;;

			break;

		case AVMEDIA_TYPE_VIDEO:
			ost->st->time_base = (AVRational){ 1, STREAM_FRAME_RATE };


		/* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */

			params->ctx->gop_size		= 12; // emit one intra frame every twelve frames at most.
			params->ctx->pix_fmt			= STREAM_PIX_FMT;

			params->ctx->thread_count	= 4;

			if (params->ctx->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
				/* just for testing, we also add B frames */
				params->ctx->max_b_frames = 1;
			}
			if (params->ctx->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
				/* Needed to avoid using macroblocks in which some coeffs overflow.
				 * This does not happen with normal video, it just happens here as
				 * the motion of the chroma plane does not match the luma plane. */
				params->ctx->mb_decision = 2;
			}

			break;

		default:
			break;
	}

	params->ctx->time_base = ost->st->time_base;

	return 0;
}

Enc_params_t *encoder_create(const char *filename,
			     enum EPCodecId ep_codec_id,
			     int width, int height)
{
	if (filename == NULL)
	{
		errno = EINVAL;
		ERRPRINTF("Bad filename");
		return NULL;
	}

	if (ep_codec_id == 0)
	{
		errno = EINVAL;
		ERRPRINTF("Bad codec_id");
		return NULL;
	}

	enum AVCodecID codec_id = 0;
	switch (ep_codec_id)
	{
		case EP_CODEC_ID_VP8:
			codec_id = AV_CODEC_ID_VP8;
			break;
		case EP_CODEC_ID_VP9:
			codec_id = AV_CODEC_ID_VP9;
			break;
		default:
			ERRPRINTF("Not available codec id");
			errno = EINVAL;
			return NULL;
	}

	int ret = 0;

	Enc_params_t *params = (Enc_params_t*)calloc(1, sizeof(Enc_params_t));
	if (params == NULL)
	{
		return NULL;
	}

	/* find the encoder */
	params->codec = avcodec_find_encoder(codec_id);
	if (params->codec == NULL)
	{
		errno = EINVAL;
		ERRPRINTF("Could not find encoder for '%s'", avcodec_get_name(codec_id));
		return NULL;
	}

	params->cparams = avcodec_parameters_alloc();
	if (!params->cparams)
	{
		errno = ENOMEM;
		ERRPRINTF("Could not allocate codec parameters");
		return NULL;
	}

	params->ctx = avcodec_alloc_context3(params->codec);

	if (!params->ctx)
	{
		errno = ENOMEM;
		ERRPRINTF("Could not allocate codec context");
		return NULL;
	}

	avformat_alloc_output_context2(&(params->oc), NULL, NULL, filename);
	if (params->oc == NULL)
	{
		errno = ENOMEM;
		return NULL;
		ERRPRINTF("ffmpeg: Could not deduce output format file extension: using MPEG.\n");
		avformat_alloc_output_context2(&(params->oc), NULL, "webm", filename);
	}
	if (params->oc == NULL)
	{
		errno = ENOMEM;
		ERRPRINTF("ffmpeg: Could not alloc output context");
		return NULL;
	}

	AVCodecParameters	*cp	= params->cparams;
	AVCodec			*codec	= params->codec;
//	AVFormatContext		*oc		= params->oc;

	switch (codec->type) {
	case AVMEDIA_TYPE_AUDIO:
		// ?
		/*
		*ctx->sample_fmt  = (*codec)->sample_fmts ?
			(*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
		*/
		// ?
		cp->bit_rate    = 64000;
		cp->sample_rate = 44100;
		if (codec->supported_samplerates) {
			cp->sample_rate = codec->supported_samplerates[0];
			for (int i = 0; codec->supported_samplerates[i]; i++) {
				if (codec->supported_samplerates[i] == 44100)
					cp->sample_rate = 44100;
			}
		}
		cp->channels       = av_get_channel_layout_nb_channels(cp->channel_layout);
		cp->channel_layout = AV_CH_LAYOUT_STEREO;
		if (codec->channel_layouts) {
			cp->channel_layout = codec->channel_layouts[0];
			for (int i = 0; codec->channel_layouts[i]; i++) {
				if (codec->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
					cp->channel_layout = AV_CH_LAYOUT_STEREO;
			}
		}
		cp->channels        = av_get_channel_layout_nb_channels(cp->channel_layout);
		break;
	case AVMEDIA_TYPE_VIDEO:
		cp->codec_id = codec->id;
		cp->bit_rate = 400000;
		/* Resolution must be a multiple of two. */
		cp->width    = width; //1366;//640;
		cp->height   = height; //768;//360;

	break;
	default:
		break;
	}
	//		ost->st->time_base = (AVRational){ 1, c->sample_rate };

	ret = av_dict_copy(&(params->opt), NULL, 0);
	if (ret < 0)
	{
		errno = ENOMEM;
		ERRPRINTF("Bad alloc dict");
		return NULL;
	}

	switch(codec->id)
	{
		case AV_CODEC_ID_VP9:
			set_dict_context(_vp9_context, &(params->opt));
			break;
		case AV_CODEC_ID_VP8:
			set_dict_context(_vp8_context, &(params->opt));
			break;
		default:
			errno = EINVAL;
			ERRPRINTF("Bad codec_id");
			goto err;
			break;
	}


#define JUST_FOR_TEST_VP9
#ifdef JUST_FOR_TEST_VP9
	dict_ccontext_t ctx = _vp9_context;
	for (size_t p_id = 0; p_id < 14; p_id++)
	{
		AVDictionaryEntry* tempvar = NULL;
		
		if ((tempvar = av_dict_get(params->opt, ctx.param[p_id].key, NULL, 0)) == NULL)
		{
			ERRPRINTF("Can't find %s", ctx.param[p_id].key);
		}
		else
		{
			ERRPRINTF("%s -> %s", tempvar->key, tempvar->value);
		}
		
	}
#endif

	params->fmt	= NULL;

	params->have_video   = 0;
	params->encode_video = 0;

#ifdef FFVER_3_0
	av_register_all();
#endif

	params->fmt = params->oc->oformat;
	if (!params->fmt)
	{
		printf("%d:: Could not deduce output format from file extension: using MPEG.", __LINE__);
		params->fmt = av_guess_format("mpeg", NULL, NULL);
		params->oc->oformat = params->fmt;
	}
	if (!params->fmt)
	{
		ERRPRINTF("Could not find suitable output format");
		goto err;
	}

	params->fmt->video_codec = params->codec->id;

	if (params->fmt->video_codec != AV_CODEC_ID_NONE)
	{
		ret = add_stream(params);
		if (ret < 0)
		{
			goto err;
		}
		params->have_video = 1;
		params->encode_video = 1;
	}

	if (params->have_video)
	{
		ret = open_video(params);
		if (ret < 0)
		{
			goto err;
		}
	}

	av_dump_format(params->oc, 0, filename, 1);

	if (!(params->fmt->flags & AVFMT_NOFILE))
	{
		ret = avio_open(&(params->oc->pb), filename, AVIO_FLAG_WRITE);
		if (ret < 0)
		{
			ERRPRINTF("Could not open '%s' : %s", filename, av_err2str(ret));
			goto err;
		}
	}

	ret = avformat_write_header(params->oc, &(params->opt));
	if (ret < 0)
	{
		ERRPRINTF("Error occurred when opening output file: %s", av_err2str(ret));
		goto err;
	}

	return params;

err:
	return NULL;
}

static AVFrame *get_video_frame(AVCodecContext *ctx, OutputStream *ost, framedata_t bmp)
{
	/* check if we want to generate more frames */
#ifndef MAIN_LOOP_DEBUG_SESSION
	if (av_compare_ts(ost->next_pts, ctx->time_base,
			STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
		return NULL;
#endif
	int ret = 0;
	ret = fill_yuv_image(ost->frame, ctx->width, ctx->height, bmp);
	if (ret < 0)
	{
		return NULL;
	}
	ost->frame->pts = ost->next_pts++;
	return ost->frame;
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_video_frame(AVCodecContext *ctx, AVFormatContext *oc, OutputStream *ost, framedata_t bmp)
{
	int ret = 0;
	AVFrame *frame = NULL;
	int got_packet = 0;

	frame = get_video_frame(ctx, ost, bmp);
	if (frame == NULL)
	{
		return AVERROR(errno);
	}
	
	if (oc->oformat->flags & AVFMT_RAWPICTURE)
	{
		/* a hack to avoid data copy with some raw video muxers */
		AVPacket pkt;
		av_init_packet(&pkt);
		if (!frame)
			return 1;
		pkt.flags        |= AV_PKT_FLAG_KEY;
		pkt.stream_index  = ost->st->index;
		pkt.data          = (uint8_t *)frame;
		pkt.size          = sizeof(AVPicture);
		pkt.pts = pkt.dts = frame->pts;
		av_packet_rescale_ts(&pkt, ctx->time_base, ost->st->time_base);
		ret = av_interleaved_write_frame(oc, &pkt);
	}
	else
	{
		AVPacket pkt = { 0 };
		av_init_packet(&pkt);

		/* encode the image */
#ifdef FFVER_3_0
		ret = avcodec_encode_video2(ctx, &pkt, frame, &got_packet);
		if (ret < 0)
		{
			ERRPRINTF("Error encoding video frame: %s", av_err2str(ret));
			return ret;
		}
#else
		ret = avcodec_send_frame(ctx, frame); // necessary to fix with ffmpeg 3.0
		if (ret < 0)
		{
			ERRPRINTF("Error sending frame for encoding: %s", av_err2str(ret));
			return ret;
		}
		while (ret >= 0)
		{
			ret = avcodec_receive_packet(ctx, &pkt);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret == AVERROR(EINVAL))
			{
				return 1;
			}
			else if (ret < 0)
			{
				ERRPRINTF("Error during encoding (receive frame)");
				return ret;
			}
			else
			{
				av_packet_unref(&pkt);
			}
		}
#endif

		if (got_packet)
		{
			ret = write_frame(oc, &ctx->time_base, ost->st, &pkt);
		}
		else
		{
			ret = 0;
		}
	}
	if (ret < 0)
	{
		ERRPRINTF("Error while writing video frame: %s", av_err2str(ret));
		return ret;
	}
	return (frame || got_packet) ? 0 : 1;
}

static void close_stream(OutputStream *ost)
{
	av_frame_free(&ost->frame);
	av_frame_free(&ost->tmp_frame);
}

int encoder_add_frame(Enc_params_t *params, size_t frame_ind, const void *data_, int type)
{
	if (params == NULL)
	{
		errno = EINVAL;
		return AVERROR(errno);
	}

	framedata_t data = NULL;
	int ret = 0;
	switch (type)
	{
		case 0: // framedata_t* frame
			data = (framedata_t)data_;
			break;
		case 1: // const char* filename
			ret = load_frame(&data, (const char*)data_, frame_ind);
			if (ret < 0 || data == NULL)
			{
				return ret;
			}
			break;
		default:
			errno = EINVAL;
			return AVERROR(errno);
			break;
	}

#ifdef MAIN_LOOP_DEBUG_SESSION
		double time = 0;
#endif
		params->encode_video = (int)(write_video_frame(params->ctx, params->oc, &(params->video_st), data) == 0);
		if (params->encode_video < 0)
		{
			return params->encode_video;
		}
#ifdef MAIN_LOOP_DEBUG_SESSION
		time++;
		if ((int)time % 10 == 0)
		{
			frame_ind = 1;
		}
		printf("\rVideo duration: %.3fs", time / STREAM_FRAME_RATE);
		fflush(stdout);
#endif

	return 0;

	//time_t finish = time(NULL);
	//ERRPRINTF("%lf sec for %zu frames", difftime(finish, start), frame_ind);
}

int encoder_write(Enc_params_t *params)
{
	if (params == NULL)
	{
		errno = EINVAL;
		return AVERROR(errno);
	}

	int ret = 0;

	ret = av_write_trailer(params->oc);
	if (ret < 0)
	{
		goto err;
	}

	if (params->have_video)
	{
		avcodec_close(params->ctx);
		close_stream(&(params->video_st));
	}

	if (!(params->fmt->flags & AVFMT_NOFILE))
	{
		ret = avio_closep(&(params->oc->pb));
		if (ret < 0)
		{
			goto err;
		}
	}

	avformat_free_context(params->oc);

	return ret;

err:
	avformat_free_context(params->oc);

	return ret;
}

void encoder_destruct(Enc_params_t* params)
{
	avcodec_parameters_free(&(params->cparams));

	free(params);
}
