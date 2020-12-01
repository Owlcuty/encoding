#include <libavcodec/avcodec.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavutil/imgutils.h>

#include <libswscale/swscale.h>

#include <libavformat/avformat.h>

#include "encoder.h"


#define STREAM_PIX_FMT		AV_PIX_FMT_YUV420P /* default pix_fmt */

#define AVFMT_RAWPICTURE 0x0020

#ifndef NITEMS /* SIZEOF() */
#	define NITEMS(__val)	(sizeof(__val) / sizeof(__val[0]))
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
	AVCodecContext		*ctx;
	AVFormatContext		*oc;
	AVDictionary		*opt;
	AVOutputFormat		*fmt;
	OutputStream		video_st;
	int			encode_video;
	int			have_video;
	uint16_t	frame_rate;
} Enc_params_t;

int EP_get_encode_video(Enc_params_t *params)
{
	return params->encode_video;
}

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

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
	/* rescale output packet timestamp values from codec to stream timebase */
	av_packet_rescale_ts(pkt, *time_base, st->time_base);
	pkt->stream_index = st->index;
	/* Write the compressed frame to the media file. */
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
		return ret;
	}

	/* allocate and init a re-usable frame */
	ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
	if (!ost->frame)
	{
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
			return AVERROR(errno);
		}
	}

	ret = avcodec_parameters_from_context(ost->st->codecpar, c);
	if (ret < 0)
	{
		return ret;
	}

	return 0;
}

static int fill_yuv_image(AVFrame *pict, int width, int height, framedata_t bmp)
{
	if (bmp == NULL)
	{
		errno = EINVAL;
		return AVERROR(errno);
	}
	struct SwsContext *sws_ctx = NULL;
	AVFrame *frame1 = NULL;

	sws_ctx = sws_getContext(width, height, AV_PIX_FMT_RGB24,
												width, height, AV_PIX_FMT_YUV420P,
												0, NULL, NULL, NULL);
	if (!sws_ctx)
	{
		errno = ENOMEM;
		goto err;
	}

	frame1 = alloc_picture(AV_PIX_FMT_RGB24, width, height);
	av_image_fill_arrays(frame1->data, frame1->linesize, (const uint8_t*)bmp, AV_PIX_FMT_RGB24, width, height, 1);

	int ret = av_image_alloc(pict->data, pict->linesize, pict->width, pict->height, AV_PIX_FMT_YUV420P, 32);
	if (ret < 0)
	{
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

void set_dict_context(const AVDictionaryEntry *ctx, AVDictionary **opt)
{
	for (size_t p_id = 0; p_id < NITEMS(ctx); p_id++)
	{
		av_dict_set(opt, ctx[p_id].key, ctx[p_id].value, 0);
	}
}

/* Add an output stream. */
int add_stream(Enc_params_t *params)
{
	int ret = 0;

	OutputStream *ost = &(params->video_st);
	ost->st = avformat_new_stream(params->oc, params->codec);
	if (ost->st == NULL) {
		return AVERROR(errno);
	}
	ost->st->id = params->oc->nb_streams - 1;

	ret = avcodec_parameters_from_context(ost->st->codecpar, params->ctx);
	if (ret < 0)
	{
		return ret;
	}

	switch (params->codec->type)
	{
		case AVMEDIA_TYPE_AUDIO:
			ost->st->time_base = (AVRational){ 1, ost->st->codecpar->sample_rate };

			params->ctx->sample_fmt = params->codec->sample_fmts ?
										 params->codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;

			break;

		case AVMEDIA_TYPE_VIDEO:
			ost->st->time_base = (AVRational){ 1, params->frame_rate };
			params->ctx->time_base = ost->st->time_base;


		/* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */

			params->ctx->gop_size		= 12; // emit one intra frame every twelve frames at most.
			params->ctx->pix_fmt		= STREAM_PIX_FMT;

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

	return 0;
}

Enc_params_t *encoder_create(const char *filename,
			     enum EPCodecId ep_codec_id,
			     int width, int height,
				 uint16_t frame_rate)
{
	if (filename == NULL)
	{
		errno = EINVAL;
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
			errno = EINVAL;
			return NULL;
	}

	int ret = 0;

	Enc_params_t *params = (Enc_params_t*)calloc(1, sizeof(Enc_params_t));
	if (params == NULL)
	{
		return NULL;
	}

	params->frame_rate = frame_rate;

	/* find the encoder */
	params->codec = avcodec_find_encoder(codec_id);
	if (params->codec == NULL)
	{
		errno = EINVAL;
		return NULL;
	}

	params->ctx = avcodec_alloc_context3(params->codec);

	if (!params->ctx)
	{
		errno = ENOMEM;
		return NULL;
	}

	avformat_alloc_output_context2(&(params->oc), NULL, NULL, filename);
	if (params->oc == NULL)
	{
		errno = ENOMEM;
		return NULL;
		avformat_alloc_output_context2(&(params->oc), NULL, "webm", filename);
	}
	if (params->oc == NULL)
	{
		errno = ENOMEM;
		return NULL;
	}

	AVCodec			*codec	= params->codec;
	AVCodecContext	*ctx	= params->ctx;

	switch (codec->type) {
	case AVMEDIA_TYPE_AUDIO:
		ctx->sample_fmt  = codec->sample_fmts ?
			codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
		ctx->bit_rate    = 64000;
		ctx->sample_rate = 44100;
		if (codec->supported_samplerates) {
			ctx->sample_rate = codec->supported_samplerates[0];
			for (int i = 0; codec->supported_samplerates[i]; i++) {
				if (codec->supported_samplerates[i] == 44100)
					ctx->sample_rate = 44100;
			}
		}
		ctx->channels       = av_get_channel_layout_nb_channels(ctx->channel_layout);
		ctx->channel_layout = AV_CH_LAYOUT_STEREO;
		if (codec->channel_layouts) {
			ctx->channel_layout = codec->channel_layouts[0];
			for (int i = 0; codec->channel_layouts[i]; i++) {
				if (codec->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
					ctx->channel_layout = AV_CH_LAYOUT_STEREO;
			}
		}
		ctx->channels        = av_get_channel_layout_nb_channels(ctx->channel_layout);
		break;
	case AVMEDIA_TYPE_VIDEO:
		ctx->codec_id = codec->id;
		ctx->bit_rate = 400000;
		/* Resolution must be a multiple of two. */
		ctx->width    = width;
		ctx->height   = height;

	break;
	default:
		break;
	}

	ret = av_dict_copy(&(params->opt), NULL, 0);
	if (ret < 0)
	{
		errno = ENOMEM;
		return NULL;
	}

	switch(codec->id)
	{
		case AV_CODEC_ID_VP9:
			set_dict_context(_vp9_dict, &(params->opt));
			break;
		case AV_CODEC_ID_VP8:
			set_dict_context(_vp8_dict, &(params->opt));
			break;
		default:
			errno = EINVAL;
			goto err;
			break;
	}

	params->fmt	= NULL;

	params->have_video   = 0;
	params->encode_video = 0;

	params->fmt = params->oc->oformat;
	if (!params->fmt)
	{
		params->fmt = av_guess_format("mpeg", NULL, NULL);
		params->oc->oformat = params->fmt;
	}
	if (!params->fmt)
	{
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

	if (!(params->fmt->flags & AVFMT_NOFILE))
	{
		ret = avio_open(&(params->oc->pb), filename, AVIO_FLAG_WRITE);
		if (ret < 0)
		{
			goto err;
		}
	}

	ret = avformat_write_header(params->oc, &(params->opt));
	if (ret < 0)
	{
		goto err;
	}

	return params;

err:
	return NULL;
}

static AVFrame *get_video_frame(AVCodecContext *ctx, OutputStream *ost, framedata_t bmp)
{
	/* check if we want to generate more frames */
	int ret = 0;

	ret = av_frame_make_writable(ost->frame);
	if (ret < 0)
	{
		return NULL;
	}

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
		ret = avcodec_send_frame(ctx, frame);
		if (ret < 0)
		{
			goto end;
		}
		
		while (1)
		{
			ret = avcodec_receive_packet(ctx, &pkt);
			
			if (ret)
				break;

			pkt.stream_index = 0;
			ret = write_frame(oc, &ctx->time_base, ost->st, &pkt);
			av_packet_unref(&pkt);
		}
	}
end:
		ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);
		return ret;
}

static void close_stream(OutputStream *ost)
{
	av_frame_free(&ost->frame);
	av_frame_free(&ost->tmp_frame);
}

int encoder_add_frame(Enc_params_t *params, size_t frame_ind, const void *data_)
{
	if (params == NULL)
	{
		errno = EINVAL;
		return AVERROR(errno);
	}
	if (data_ == NULL)
	{
		errno = EINVAL;
		return AVERROR(errno);
	}

	framedata_t data = (framedata_t)data_;

	params->encode_video = (int)(write_video_frame(params->ctx, params->oc, &(params->video_st), data) == 0);
	if (params->encode_video < 0)
	{
		return params->encode_video;
	}

	return 0;
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
	avcodec_free_context(&(params->ctx));

	free(params);
}
