#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

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

#define FFVER_3_0

//#define MAIN_VIDEO_CODEC_ID AV_CODEC_ID_VP9


typedef struct dict_codec_context
{
	const AVDictionaryEntry *param;
	size_t cnt;
} dict_ccontext_t;

/* vp8 context -----------*/
const AVDictionaryEntry _vp8_dict[4] = {
	{"arnr-maxframes", "15"},
	{"deadline", "realtime"},
	{"cpu-used", "16"},
	{"crf", "4"}
};

const dict_ccontext_t _vp8_context = {
	_vp8_dict,
	4
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
//--------------------------


void set_dict_context(const dict_ccontext_t ctx, AVDictionary **opt)
{
	for (size_t p_id = 0; p_id < ctx.cnt; p_id++)
	{
		av_dict_set(opt, ctx.param[p_id].key, ctx.param[p_id].value, 0);
	}
}



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






errno_t load_frame(framedata_t** data, const char *filename, size_t frame_ind)
{
	assert(filename);

	int width  = 0;
	int height = 0;

	char *cur_filename = (char*)calloc(strlen(filename) + 1, sizeof(*cur_filename));

	if (cur_filename == NULL)
	{
		goto err;
	}

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
	free(*data);

	return AVERROR(errno);
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
ERRPRINTF("HERE!");
	ret = avcodec_open2(c, codec, &opt);
	av_dict_free(&opt);
	if (ret < 0) {
		fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
		exit(1);
	}
ERRPRINTF("HERE!");
	/* allocate and init a re-usable frame */
	ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
	if (!ost->frame) {
		fprintf(stderr, "Could not allocate video frame\n");
		exit(1);
	}
	/* If the output format is not YUV420P, then a temporary YUV420P
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

static void fill_yuv_image(AVFrame *pict, int width, int height, framedata_t bmp) // void -> errno_t
{
	struct SwsContext *sws_ctx = sws_getContext(width, height, AV_PIX_FMT_RGB24,
												width, height, AV_PIX_FMT_YUV420P,
												0, NULL, NULL, NULL);
	if (!sws_ctx)
	{
		ERRPRINTF("Bad sws_getContext");
	}

	AVFrame* frame1 = alloc_picture(AV_PIX_FMT_RGB24, width, height);
//	avpicture_fill((AVPicture*)frame1, (const uint8_t*)bmp, AV_PIX_FMT_RGB24, width, height);
	av_image_fill_arrays(frame1->data, frame1->linesize, (const uint8_t*)bmp, AV_PIX_FMT_RGB24, width, height, 1);

	int ret = av_image_alloc(pict->data, pict->linesize, pict->width, pict->height, AV_PIX_FMT_YUV420P, 32);
	if (ret < 0)
	{
		ERRPRINTF("Could not allocate raw picture buffer");
		exit(1);
	}

	sws_scale(sws_ctx, (const uint8_t * const *)frame1->data, frame1->linesize, 0,
				height, pict->data, pict->linesize);

	av_frame_free(&frame1);
}


Enc_params_t *encoder_create(const char *filename,
							 const char *codec_name,
							 int height, int width)
{
	if (filename == NULL)
	{
		errno = AVERROR(EINVAL);
		return NULL;
	}

	if (codec_name == NULL)
	{
		errno = AVERROR(EINVAL);
		return NULL;
	}

	av_register_all(); // for ffmpeg v.3.x and lower (deprecated for ffmpeg v.4+)

	int ret = 0;

	Enc_params_t *params = (Enc_params_t*)calloc(1, sizeof(Enc_params_t));

	/* find the encoder */
	params->codec = avcodec_find_encoder_by_name(codec_name);
	if (params->codec == NULL)
	{
		ERRPRINTF("Could not find encoder for '%s'", codec_name);
		return -1;
	}
	ERRPRINTF("Found encoder '%s'", avcodec_get_name(params->codec->id));

	params->cparams = avcodec_parameters_alloc();
//	*ctx = avcodec_alloc_context3(*codec);
	if (!params->cparams)
	{
		ERRPRINTF("Could not allocate codec parameters");
		return -1;
	}

	avformat_alloc_output_context2(&(params->oc), NULL, NULL, filename);
	if (params->oc == NULL)
	{
		errno = AVERROR(ENOMEM);
		return NULL;
		ERRPRINTF("ffmpeg: Could not deduce output format file extension: using MPEG.\n");
		avformat_alloc_output_context2(&(params->oc), NULL, "webm", filename);
	}
	if (params->oc == NULL)
	{
		errno = AVERROR(ENOMEM);
		ERRPRINTF("ffmpeg: Could not alloc output context");
		return NULL;
	}

	AVCodecParameters	*cp		= params->cparams;
	AVCodec				*codec	= params->codec;
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
		errno = AVERROR(ENOMEM);
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
			ERRPRINTF("Wtf man");
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

	av_register_all();

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
		add_stream(&(params->video_st), params->oc, params->codec, params->cparams);
		params->have_video = 1;
		params->encode_video = 1;
	}

	if (params->have_video)
	{
		open_video(params->oc, params->codec, &(params->video_st), params->opt);
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

err:
	return NULL;
}

/* Add an output stream. */
void add_stream(OutputStream *ost, AVFormatContext *oc,
					   AVCodec *codec,
					   const AVCodecParameters *cparams)
{
	int ret = 0;

	ost->st = avformat_new_stream(oc, codec);
	if (!ost->st) {
		fprintf(stderr, "Could not allocate stream\n");
		exit(1);
	}
	ost->st->id = oc->nb_streams - 1;
	avcodec_parameters_to_context(ost->st->codec, cparams);

	switch (codec->type)
	{
		case AVMEDIA_TYPE_AUDIO:
			ost->st->time_base = (AVRational){ 1, ost->st->codecpar->sample_rate };

			ost->st->codec->sample_fmt = codec->sample_fmts ?
										 codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;;

			break;

		case AVMEDIA_TYPE_VIDEO:
			ost->st->time_base = (AVRational){ 1, STREAM_FRAME_RATE };


		/* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */

			ost->st->codec->gop_size		= 12; // emit one intra frame every twelve frames at most.
			ost->st->codec->pix_fmt			= STREAM_PIX_FMT;

			ost->st->codec->thread_count	= 4;

			if (ost->st->codec->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
				/* just for testing, we also add B frames */
				ost->st->codec->max_b_frames = 1;
			}
			if (ost->st->codec->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
				/* Needed to avoid using macroblocks in which some coeffs overflow.
				 * This does not happen with normal video, it just happens here as
				 * the motion of the chroma plane does not match the luma plane. */
				ost->st->codec->mb_decision = 2;
			}

			break;

		default:
			break;
	}
	
	ost->st->codec->time_base = ost->st->time_base;

	/* Some formats want stream headers to be separate. */
//	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
//		c->flags |= CODEC_FLAG_GLOBAL_HEADER;
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
#ifdef FFVER_3_0
		ret = avcodec_encode_video2(c, &pkt, frame, &got_packet);
		if (ret < 0) {
			fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
			exit(1);
		}
#else
		ret = avcodec_send_frame(c, frame); // necessary to fix with ffmpeg 3.0
		if (ret < 0) {
			ERRPRINTF("Error sending frame for encoding: %s\n", av_err2str(ret));
			exit(1);
		}
		while (ret >= 0)
		{
			ret = avcodec_receive_packet(c, &pkt);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret == AVERROR(EINVAL))
			{
				return 1;
			}
			else if (ret < 0)
			{
				ERRPRINTF("Error during encoding (receive frame)");
				exit(1);
			}
			else
			{
				av_packet_unref(&pkt);
			}
		}
#endif

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
errno_t encoder_add_frame(Enc_params_t *params, size_t frame_ind, const void *data_, int type)
{
	if (params == NULL)
	{
		errno = AVERROR(EINVAL);
		return errno;
	}

	framedata_t *data = NULL;
	int ret = 0;
	switch (type)
	{
		case 0: // framedata_t* frame
			data = (framedata_t *)data_;
			break;
		case 1: // const char* filename
			ret = load_frame(&data, (const char*)data_, frame_ind);
			if (ret < 0 || data == NULL)
			{
				return ret;
			}
			break;
		default:
			errno = AVERROR(EINVAL);
			return errno;
			break;
	}

#ifdef MAIN_LOOP_DEBUG_SESSION
		double time = 0;
#endif
		params->encode_video = (int)(write_video_frame(params->oc, &(params->video_st), data) == 0);
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

errno_t encoder_write(Enc_params_t *params)
{
	
	av_write_trailer(params->oc);

	if (params->have_video)
		close_stream(params->oc, &(params->video_st));

	if (!(params->fmt->flags & AVFMT_NOFILE))
	{
		avio_closep(&(params->oc->pb));
	}

	avformat_free_context(params->oc);

	return 0;

err:
	avformat_free_context(params->oc);

	return -1;
}

void encoder_destruct(Enc_params_t* params)
{
	avcodec_parameters_free(&(params->cparams));

	free(params);
}

int main(int argc, char **argv)
{
	const char *filename = "output.webm";
	const char *codec_name = "libvpx-vp9";

	int width  = 1366;
	int height = 768;
	
	int ret = 0;

	Enc_params_t *params = encoder_create(filename, codec_name, width, height);
	if (params == NULL)
	{
	    perror("encoder_create() fail: ");
	    return errno;
	}

	framedata_t *bmp = NULL;
	int frame_ind = 1;
	
	while (params->encode_video)
	{
		ret = load_frame(&bmp, "../../forbmp/image%05d.bmp", frame_ind++);
		if (ret < 0)
		{
			perror("load_frame() fail: ");
			return errno;
		}

		ret = encoder_add_frame(params, frame_ind, bmp, 0);
		if (ret < 0)
		{
			return -1;
		}
	}

	ret = encoder_write(params);
	if (ret < 0)
	{
		return -1;
	}
	
	encoder_destruct(params);

//	ret = api_enc(filename, codec, cparams, opt, MAIN_VIDEO_CODEC_ID);

	return ret;
}
