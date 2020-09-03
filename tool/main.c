// Я сейчас заплачу. Codelite крашнулся и выплюнул мне старый свап, который вообще наполовину стёрт.............

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <assert.h>
//#define NDEBUG


#include <errno.h>

#include <libavcodec/avcodec.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>

#include <libswscale/swscale.h>

#include <libavformat/avformat.h>

#define STREAM_DURATION		10.0
#define STREAM_FRAME_RATE	10 /* 25 fps */
#define STREAM_PIX_FMT		AV_PIX_FMT_YUV420P /* default pix_fmt */
#define SCALE_FLAGS			0
#define STREAM_NB_FRAMES	((int)(STREAM_DURATION * STREAM_FRAME_RATE))

#include "bmp.h"

#define ERRPRINTF(format, ...)	fprintf(stderr, "%d::%s::%s__::__ " format "\n", __LINE__, __FILENAME__, __PRETTY_FUNCTION__, ## __VA_ARGS__)

#define AV_CODEC_FLAG_GLOBAL_HEADER (1 << 22)
#define CODEC_FLAG_GLOBAL_HEADER AV_CODEC_FLAG_GLOBAL_HEADER
#define AVFMT_RAWPICTURE 0x0020

#define MAIN_DEBUG_SESSION
//#define MAIN_LOOP_DEBUG_SESSION

#define MAIN_VIDEO_CODEC_ID AV_CODEC_ID_VP8

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



errno_t load_frame(framedata_t* data, const char *filename, size_t frame_ind)
{
	assert(filename);
	
	int width  = 0;
	int height = 0;
	
	char *cur_filename = (char*)calloc(strlen(filename) + 1, sizeof(*cur_filename));
	
	sprintf(cur_filename, filename, frame_ind);
	
	*data = load_bmp((const char*)cur_filename, &width, &height);
	if (!(*data))
	{
		ERRPRINTF("Bad loading bmp\n");
		goto err;
	}
	
	free(cur_filename);
	
	return 0;
	
err:
	free(cur_filename);
	free(data);
	
	return errno;
}

AVFrame *picture, *tmp_picture;
uint8_t *video_outbuf;
int video_outbuf_size;

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
	AVFrame *picture;
	int ret;
	picture = av_frame_alloc();
	if (!picture)
		return NULL;
	picture->format = pix_fmt;
	picture->width  = width;
	picture->height = height;
	/* allocate the buffers for the frame data */
	ret = av_frame_get_buffer(picture, 32);
	if (ret < 0) {
		ERRPRINTF("Could not allocate frame data.");
		return NULL;
	}
	return picture;
}

static void open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
	int ret;
	AVCodecContext *c = ost->st->codec;
	AVDictionary *opt = NULL;
	av_dict_copy(&opt, opt_arg, 0);
	/* open the codec */
	ret = avcodec_open2(c, codec, &opt);
	av_dict_free(&opt);
	if (ret < 0) {
		fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
		exit(1);
	}
	/* allocate and init a re-usable frame */
	ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
	if (!ost->frame) {
		fprintf(stderr, "Could not allocate video frame\n");
		exit(1);
	}
	/* If the output format is not GBRP, then a temporary GBRP
	* picture is needed too. It is then converted to the required
	* output format. */
	ost->tmp_frame = NULL;
	if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
		ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
		if (!ost->tmp_frame) {
			fprintf(stderr, "Could not allocate temporary picture\n");
			exit(1);
		}
	}
}

static void fill_yuv_image(AVFrame *pict, int width, int height, framedata_t bmp)
{
	const int in_linesize[4] = { 3 * width, 0, 0, 0 };
	
	struct SwsContext *sws_ctx = sws_getContext(width, height, AV_PIX_FMT_RGB24,
												width, height, AV_PIX_FMT_YUV420P,
												0, NULL, NULL, NULL);
	if (!sws_ctx)
	{
		ERRPRINTF("Bad sws_getContext");
	}
	
	AVFrame* frame1 = alloc_picture(AV_PIX_FMT_RGB24, width, height);
	avpicture_fill(frame1, (const uint8_t*)bmp, AV_PIX_FMT_RGB24, width, height);
	
	int num_bytes = avpicture_get_size(AV_PIX_FMT_YUV420P, width, height);
	uint8_t *frame2_buffer = (uint8_t*)av_malloc(num_bytes * sizeof(uint8_t));
	avpicture_fill((AVPicture*)pict, frame2_buffer, AV_PIX_FMT_YUV420P, width, height);
	
	sws_scale(sws_ctx, frame1->data, frame1->linesize, 0,
				height, pict->data, pict->linesize);
}

/* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *oc,
					   AVCodec **codec,
					   enum AVCodecID codec_id)
{
	AVCodecContext *c;
	int i;
	/* find the encoder */
	*codec = avcodec_find_encoder(codec_id);
	if (!(*codec)) {
		fprintf(stderr, "Could not find encoder for '%s'\n",
				avcodec_get_name(codec_id));
		exit(1);
	}
	ost->st = avformat_new_stream(oc, *codec);
	if (!ost->st) {
		fprintf(stderr, "Could not allocate stream\n");
		exit(1);
	}
	ost->st->id = oc->nb_streams - 1;
	c = ost->st->codec;
	switch ((*codec)->type) {
	case AVMEDIA_TYPE_AUDIO:
		c->sample_fmt  = (*codec)->sample_fmts ?
			(*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
		c->bit_rate    = 64000;
		c->sample_rate = 44100;
		if ((*codec)->supported_samplerates) {
			c->sample_rate = (*codec)->supported_samplerates[0];
			for (i = 0; (*codec)->supported_samplerates[i]; i++) {
				if ((*codec)->supported_samplerates[i] == 44100)
					c->sample_rate = 44100;
			}
		}
		c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
		c->channel_layout = AV_CH_LAYOUT_STEREO;
		if ((*codec)->channel_layouts) {
			c->channel_layout = (*codec)->channel_layouts[0];
			for (i = 0; (*codec)->channel_layouts[i]; i++) {
				if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
					c->channel_layout = AV_CH_LAYOUT_STEREO;
			}
		}
		c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
		ost->st->time_base = (AVRational){ 1, c->sample_rate };
		break;
	case AVMEDIA_TYPE_VIDEO:
		c->codec_id = codec_id;
		c->bit_rate = 400000;
		/* Resolution must be a multiple of two. */
		c->width    = 3840;//640;
		c->height   = 1080;//360;
		/* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
		ost->st->time_base	= (AVRational){ 1, STREAM_FRAME_RATE };
		c->time_base		= ost->st->time_base;
		c->gop_size			= 12; /* emit one intra frame every twelve frames at most */
		c->pix_fmt			= STREAM_PIX_FMT;
		
		c->thread_count		= 4;
		
		if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
			/* just for testing, we also add B frames */
			c->max_b_frames = 1;
		}
		if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
			/* Needed to avoid using macroblocks in which some coeffs overflow.
             * This does not happen with normal video, it just happens here as
             * the motion of the chroma plane does not match the luma plane. */
			c->mb_decision = 2;
		}
	break;
	default:
		break;
	}
	/* Some formats want stream headers to be separate. */
	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;
}

static AVFrame *get_video_frame(OutputStream *ost, framedata_t bmp)
{
	AVCodecContext *c = ost->st->codec;
	/* check if we want to generate more frames */
#ifndef MAIN_LOOP_DEBUG_SESSION
	if (av_compare_ts(ost->next_pts, ost->st->codec->time_base,
					  STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
		return NULL;
#endif
	fill_yuv_image(ost->frame, c->width, c->height, bmp);
	ost->frame->pts = ost->next_pts++;
	return ost->frame;
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_video_frame(AVFormatContext *oc, OutputStream *ost, framedata_t bmp)
{
	int ret;
	AVCodecContext *c;
	AVFrame *frame;
	int got_packet = 0;
	c = ost->st->codec;

	frame = get_video_frame(ost, bmp);
	
	if (oc->oformat->flags & AVFMT_RAWPICTURE) {
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
		av_packet_rescale_ts(&pkt, c->time_base, ost->st->time_base);
		ret = av_interleaved_write_frame(oc, &pkt);
	} else {
		AVPacket pkt = { 0 };
		av_init_packet(&pkt);
		/* encode the image */
		ret = avcodec_encode_video2(c, &pkt, frame, &got_packet);
		if (ret < 0) {
			fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
			exit(1);
		}
		if (got_packet) {
			ret = write_frame(oc, &c->time_base, ost->st, &pkt);
		} else {
			ret = 0;
		}
	}
	if (ret < 0) {
		fprintf(stderr, "Error while writing video frame: %s\n", av_err2str(ret));
		exit(1);
	}
	return (frame || got_packet) ? 0 : 1;
}

static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
	avcodec_close(ost->st->codec);
	av_frame_free(&ost->frame);
	av_frame_free(&ost->tmp_frame);
}

int main(int argc, char **argv)
{
	const char  *filename	= "output.webm";
	
	OutputStream video_st = { 0 };
	
	AVOutputFormat	*fmt	= NULL;
	AVFormatContext	*oc		= NULL;
	AVCodec *video_codec = NULL;
	
	int ret = 0;
	int have_video		= 0;
	int encode_video	= 0;
	AVDictionary *opt	= NULL;
	
	av_register_all();
	
	avformat_alloc_output_context2(&oc, NULL, NULL, filename);
	if (!oc)
	{
		printf("ffmpeg: Could not deduce output format file extension: using MPEG.\n");
		avformat_alloc_output_context2(&oc, NULL, "webm", filename);
	}
	if (!oc)
	{
		ERRPRINTF("ffmpeg: Could not alloc output context");
		goto err;
	}
	
	fmt = oc->oformat;
	if (!fmt)
	{
		printf("%d:: Could not deduce output format from file extension: using MPEG.");
		fmt = av_guess_format("mpeg", NULL, NULL);
		oc->oformat = fmt;
	}
	if (!fmt)
	{
		ERRPRINTF("Could not find suitable output format");
		goto err;
	}
	
	fmt->video_codec = MAIN_VIDEO_CODEC_ID;
	
	if (fmt->video_codec != AV_CODEC_ID_NONE)
	{
		add_stream(&video_st, oc, &video_codec, fmt->video_codec);
		have_video		= 1;
		encode_video	= 1;
	}
	
	if (have_video)
		open_video(oc, video_codec, &video_st, opt);
	
	av_dump_format(oc, 0, filename, 1);
	
	if (!(fmt->flags & AVFMT_NOFILE))
	{
		ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
		if (ret < 0)
		{
			ERRPRINTF("Could not open '%s' : %s", filename, av_err2str(ret));
			goto err;
		}
	}
	
	ret = avformat_write_header(oc, &opt);
	if (ret < 0)
	{
		ERRPRINTF("Error occurred when opening output file: %s", av_err2str(ret));
		goto err;
	}
	
	size_t frame_ind = 1;
	
	framedata_t* bmp = NULL;
	while (encode_video && frame_ind < STREAM_NB_FRAMES && !(load_frame(&bmp, "../forbmp1080p/image%05d.bmp", frame_ind++)))
	{
	
#ifdef MAIN_LOOP_DEBUG_SESSION
		double time = 0;
#endif
		encode_video = (int)(write_video_frame(oc, &video_st, bmp) == 0);
#ifdef MAIN_LOOP_DEBUG_SESSION
		time++;
		if ((int)time % 10 == 0)
		{
			frame_ind = 1;
		}
		printf("\rVideo duration: %.3fs", time / STREAM_FRAME_RATE);
		fflush(stdout);
#endif
		free(bmp);
	}
	
	av_write_trailer(oc);
	
	if (have_video)
		close_stream(oc, &video_st);
	
	if (!(fmt->flags & AVFMT_NOFILE))
	{
		avio_closep(&oc->pb);
	}
	
	avformat_free_context(oc);
	
	return 0;
	
err:
	avformat_free_context(oc);
	
	return -1;
}
