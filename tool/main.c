#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <assert.h>
//#define NDEBUG


#include <errno.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>

#include <libavformat/avformat.h>

#define STREAM_DURATION		10.0
#define STREAM_FRAME_RATE	25 /* 25 fps */
#define STREAM_PIX_FMT		AV_PIX_FMT_GBRP /* default pix_fmt */
#define SCALE_FLAGS			0
#define STREAM_NB_FRAMES	((int)(STREAM_DURATION * STREAM_FRAME_RATE))

#include "bmp.h"

#define ERRPRINTF(format, ...)	fprintf(stderr, "%d::%s::%s__::__ " format "\n", __LINE__, __FILENAME__, __PRETTY_FUNCTION__, ## __VA_ARGS__)

#define AV_CODEC_FLAG_GLOBAL_HEADER (1 << 22)
#define CODEC_FLAG_GLOBAL_HEADER AV_CODEC_FLAG_GLOBAL_HEADER
#define AVFMT_RAWPICTURE 0x0020

//#define MAIN_DEBUG_SESSION
#define MAIN_LOOP_DEBUG_SESSION


typedef int errno_t;





//#if 0


// a wrapper around a single output AVStream
typedef struct OutputStream {
	AVOutputFormat *fmt;
	AVFormatContext *oc;
	AVStream *st;
	AVCodecContext *enc;
	
	/* pts of the next frame that will be generated */
	int64_t next_pts;
	int samples_count;
	
	AVFrame *frame;
	AVFrame *tmp_frame;
	
	int bitrate;
	float t, tincr, tincr2;
} OutputStream;



/////* Necessary to free return value! */
framedata_t* *load_frames(const char *filename, size_t num)
{
	assert(filename);
	
	framedata_t* *data = NULL;
	
	int width  = 0;
	int height = 0;
	
	char *cur_filename = (char*)calloc(strlen(filename) + 1, sizeof(*cur_filename));
	
	framedata_t* cur_pict = NULL;
	framedata_t* *cur_pos = data;
	
	for (int frame = 1; frame <= num; frame++)
	{
		sprintf(cur_filename, filename, frame);

		cur_pict = load_bmp((const char*)cur_filename, &width, &height);
		if (!cur_pict)
		{
			ERRPRINTF("Bad loading bmp\n");
			goto err;
		}
		
		if (frame == 1)
		{
			data = (framedata_t**)calloc(width * height * num, sizeof(*data));
			if (!data)
			{
				perror("calloc() load_frames::data failed");
				goto err;
			}
#ifdef MAIN_DEBUG_SESSION
			printf("%s::%d::%s:: For data was alloced %d bytes\n", __FILENAME__, __LINE__, __PRETTY_FUNCTION__, width * height * num);
#endif
			cur_pos = data;
		}
		
#ifdef MAIN_DEBUG_SESSION
	printf("%d::%s::%s Data [%X] -- Cur_pos [%X]. Width {%d}, Height {%d}\n", __LINE__, __FILENAME__, __PRETTY_FUNCTION__,
							data,		cur_pos,
														width,		height);
	printf("%d::%s::%s Cur_pict [%X] \n", __LINE__, __FILENAME__, __PRETTY_FUNCTION__, cur_pict);
#endif
		
		*cur_pos = cur_pict;
		cur_pos++;
	}
	
	free(cur_filename);
	
	return data;
	
err:
	free(cur_filename);
	free(cur_pict);
	
	return NULL;
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

static void open_video(AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
	int ret;
	AVCodecContext *c = ost->enc;
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
	if (c->pix_fmt != AV_PIX_FMT_GBRP) {
		ost->tmp_frame = alloc_picture(AV_PIX_FMT_GBRP, c->width, c->height);
		if (!ost->tmp_frame) {
			fprintf(stderr, "Could not allocate temporary picture\n");
			exit(1);
		}
	}
	
	ret = avcodec_parameters_from_context(ost->st->codecpar, c);
	if (ret < 0)
	{
		ERRPRINTF("Could not copy the stream parameters");
		exit(1);
	}
}


static void fill_rgb_to_gbr_image(AVFrame *pict, int width, int height, framedata_t* bmp)
{
	BYTE	R = 0,
			G = 0,
			B = 0;
	
	pict->data[0] = bmp->green;
	pict->data[1] = bmp->blue;
	pict->data[2] = bmp->red;
	
//	uint8_t *ptr = (uint8_t *)bmp;
//	
//	for (size_t y = 0; y < height; y++)
//	{
//		for (size_t x = 0; x < width; x++)
//		{
//			R = *ptr++;
//			G = *ptr++;
//			B = *ptr++;
//			pict->data[0][y * pict->linesize[0] + x] = G;
//			pict->data[1][y * pict->linesize[1] + x] = B;
//			pict->data[2][y * pict->linesize[2] + x] = R;
//			ptr++;
//		}
//	}
}


/* Add an output stream. */
static void add_stream(OutputStream *ost,
					   AVCodec **codec,
					   enum AVCodecID codec_id,
					   int width, int height)
{
	AVCodecContext *c;
	int i = 0;
	
	/* find the encoder */
	*codec = avcodec_find_encoder(codec_id);
	if (!(*codec)) {
		fprintf(stderr, "Could not find encoder for '%s'\n",
				avcodec_get_name(codec_id));
		exit(1);
	}
	
	ost->st = avformat_new_stream(ost->oc, NULL);
	if (!ost->st) {
		fprintf(stderr, "Could not allocate stream\n");
		exit(1);
	}
	ost->st->id = ost->oc->nb_streams - 1;
	c = avcodec_alloc_context3(*codec);
	if (!c)
	{
		ERRPRINTF("Could not alloc an encoding context");
	}
	ost->enc = c;
	
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
		
		c->bit_rate = ost->bitrate;
		/* Resolution must be a multiple of two. */
		c->width    = width;
		c->height   = height;
		/* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
		ost->st->time_base	= (AVRational){ 1, STREAM_FRAME_RATE };
		c->time_base		= ost->st->time_base;
		
		c->gop_size			= 12; /* emit one intra frame every twelve frames at most */
		c->pix_fmt			= STREAM_PIX_FMT;
		
		c->thread_count		= 4;
//		
//		if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
//			/* just for testing, we also add B frames */
//			c->max_b_frames = 1;
//		}
//		if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
//			/* Needed to avoid using macroblocks in which some coeffs overflow.
//             * This does not happen with normal video, it just happens here as
//             * the motion of the chroma plane does not match the luma plane. */
//			c->mb_decision = 2;
//		}
		break;
	default:
		break;
	}
	/* Some formats want stream headers to be separate. */
	if (ost->oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;
}

static AVFrame *get_video_frame(OutputStream *ost, framedata_t* bmp)
{
	AVCodecContext *c = ost->enc;
	/* check if we want to generate more frames */
#ifndef MAIN_LOOP_DEBUG_SESSION
	if (av_compare_ts(ost->next_pts, ost->st->codec->time_base,
					  STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
		return NULL;
#endif
	fill_rgb_to_gbr_image(ost->frame, c->width, c->height, bmp);
	
	ost->frame->pts = ost->next_pts++;
	
	return ost->frame;
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_video_frame(OutputStream *ost, framedata_t* bmp)
{
	int ret;
	AVCodecContext *c = ost->enc;
	AVPacket pkt = { 0 };
	
	ost->frame = get_video_frame(ost, bmp);
	
	av_init_packet(&pkt);
	
	ret = avcodec_send_frame(c, ost->frame);
	if (ret < 0)
	{
		ERRPRINTF("Error encoding video frame: %s", av_err2str(ret));
		return 1;
	}
	
	ret = avcodec_receive_packet(c, &pkt);
	if (ret == 0)
	{
		ret = write_frame(ost->oc, &c->time_base, ost->st, &pkt);
		if (ret < 0)
		{
			ERRPRINTF("Error while writing video frame: %s", av_err2str(ret));
			return 1;
		}
	}
	else if (ret == AVERROR(EINVAL)) {
		fprintf(stderr, "Error while getting video packet: %s\n", av_err2str(ret));
		return 1;
	}
	else if (ret == AVERROR_EOF) {
		fprintf(stderr, "End of stream met while adding a frame\n");
		return 1;
	}
	else if (ret == AVERROR(EAGAIN))
		return 0;
	
	return 0;
}

static void flush_stream(OutputStream *ost)
{
	int ret;
	AVPacket pkt = { 0 };
	av_init_packet(&pkt);
	
	ret = avcodec_send_frame(ost->enc, NULL);
	if (ret < 0)
	{
		ERRPRINTF("Error encoding video frame: %s", av_err2str(ret));
		return;
	}
	
	do
	{
		ret = avcodec_receive_packet(ost->enc, &pkt);
		if (ret == 0)
		{
			ret = write_frame(ost->oc, &ost->enc->time_base, ost->st, &pkt);
			if (ret < 0)
			{
				ERRPRINTF("Error while writing video frame: %s", av_err2str(ret));
				return;
			}
		}
		else if (ret == AVERROR(EINVAL)) {
			fprintf(stderr, "Error while getting video packet: %s\n", av_err2str(ret));
			return;
		}
	}
	while (ret != AVERROR_EOF) ;
}

static void close_stream(OutputStream *ost)
{
	avcodec_free_context(&ost->enc);
	av_frame_free(&ost->frame);
	av_frame_free(&ost->tmp_frame);
}


//#endif

















int main(int argc, char **argv)
{
	framedata_t* *frames = load_frames("../forbmp/image%03d.bmp", 250);
	if (!frames)
	{
		ERRPRINTF("Bad loading frames\n");
		return 0;
	}
	
	const char  *filename	= "output.mp4";
	
	OutputStream* video_st = NULL;
	
	AVCodec *video_codec = NULL;
	
	int ret = 0;
	int have_video		= 0;
	int encode_video	= 0;
	AVDictionary *opt	= NULL;
	
//	av_register_all();
	video_st = calloc(1, sizeof(*video_st));
	
	avformat_alloc_output_context2(&video_st->oc, NULL, NULL, filename);
	if (!video_st->oc)
	{
		printf("ffmpeg: Could not deduce output format file extension: using MPEG.\n");
		avformat_alloc_output_context2(&video_st->oc, NULL, "mpeg", filename);
	}
	if (!video_st->oc)
	{
		ERRPRINTF("ffmpeg: Could not alloc output context");
		goto err;
	}
	
	video_st->fmt = video_st->oc->oformat;
	if (!video_st->fmt)
	{
		ERRPRINTF("Could not deduce output format from file extension: using MPEG.");
		video_st->fmt = av_guess_format("mpeg", NULL, NULL);
		video_st->oc->oformat = video_st->fmt;
	}
	if (!video_st->fmt)
	{
		ERRPRINTF("Could not find suitable output format");
		goto err;
	}
	
	video_st->fmt->video_codec = AV_CODEC_ID_VP9;
	video_st->bitrate = 400000;
	
	if (video_st->fmt->video_codec != AV_CODEC_ID_NONE)
	{
		add_stream(video_st, &video_codec, video_st->fmt->video_codec, 640, 360);
		have_video		= 1;
		encode_video	= 1;
	}
	
	if (have_video)
		open_video(video_codec, video_st, opt);
	
	av_dump_format(video_st->oc, 0, filename, 1);
	
	if (!(video_st->fmt->flags & AVFMT_NOFILE))
	{
		ret = avio_open(&video_st->oc->pb, filename, AVIO_FLAG_WRITE);
		if (ret < 0)
		{
			ERRPRINTF("Could not open '%s' : %s", filename, av_err2str(ret));
			goto err;
		}
	}
	
	ret = avformat_write_header(video_st->oc, &opt);
	if (ret < 0)
	{
		ERRPRINTF("Error occurred when opening output file: %s", av_err2str(ret));
		goto err;
	}
	
	framedata_t* *bmp = frames;
#ifdef MAIN_LOOP_DEBUG_SESSION
	double time = 0;
#endif
	while (encode_video)
	{
		encode_video = (int)(write_video_frame(video_st, *bmp++) == NULL);
#ifdef MAIN_LOOP_DEBUG_SESSION
		time++;
		if ((int)time % 10 == 0)
		{
			bmp = frames;
		}
		printf("\rVideo duration: %.3fs", time / STREAM_FRAME_RATE);
		fflush(stdout);
#endif
	}
	
	flush_stream(video_st);
	av_write_trailer(video_st->oc);
	
	if (have_video)
		close_stream(video_st);
	
	if (!(video_st->fmt->flags & AVFMT_NOFILE))
	{
		avio_closep(&video_st->oc->pb);
	}
	
	avformat_free_context(video_st->oc);
	
	return 0;
	
err:
	avformat_free_context(video_st->oc);
	
	return -1;
}