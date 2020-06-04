#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <assert.h>
//#define NDEBUG


#include <errno.h>

#include <libavcodec/avcodec.h>
//#include <avio.h>

#include <libavutil/opt.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>

#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include "bmp.h"

#undef exit

#define STREAM_DURATION		5.0
#define STREAM_FRAME_RATE	25
#define STREAM_NB_FRAMES	((int)(STREAM_DURATION * STREAM_FRAME_RATE))
#define STREAM_PIX_FMT		AV_PIX_FMT_YUV420P

static int sws_flags = SWS_BICUBIC;


typedef int errno_t;


/* Necessary to free return value! */
pict_t *load_frames(const char *filename, size_t num)
{
	assert(filename);
	
	pict_t *data = NULL;
	
	int width  = 0;
	int height = 0;
	
	char *cur_filename = (char*)calloc(strlen(filename) + 1, sizeof(*cur_filename));
	
	pict_t cur_pict = NULL;
	pict_t *cur_pos = data;
	
	for (int frame = 1; frame <= num; frame++)
	{
		sprintf(cur_filename, filename, frame);

		cur_pict = load_bmp((const char*)cur_filename, &width, &height);
		if (!cur_pict)
		{
			fprintf(stderr, "%d::\"%s\":%s:: Bad loading bmp\n", __LINE__, __FILENAME__, __PRETTY_FUNCTION__);
			goto err;
		}
		
		if (frame == 1)
		{
			data = (pict_t*)calloc(width * height * num, sizeof(*data));
			if (!data)
			{
				perror("calloc() load_frames::data failed");
				goto err;
			}
#ifdef BMP_DEBUG_SESSION
			printf("%s::%d::%s:: For data was alloced %d bytes\n", __FILENAME__, __LINE__, __PRETTY_FUNCTION__, width * height * num);
#endif
			cur_pos = data;
		}
		
#ifdef BMP_DEBUG_SESSION
	printf("%d::%s::%s Data [%X] -- Cur_pos [%X]. Width {%d}, Height {%d}\n", __LINE__, __FILENAME__, __PRETTY_FUNCTION__,
							data,		cur_pos,
														width,		height);
	printf("%d::%s::%s Cur_pict [%X] \n", __LINE__, __FILENAME__, __PRETTY_FUNCTION__, cur_pict);
#endif
//		memcpy(cur_pos, cur_pict, width * height * sizeof(int));
		
		*cur_pos = cur_pict;
		cur_pos++;
		
//		cur_pos += width * height * sizeof(int);
//		free(cur_pict);
//		break;
	}
	
	free(cur_filename);
	
	return data;
	
err:
	free(cur_filename);
	free(cur_pict);
	
	return NULL;
}

#if 0
errno_t fill_avframe(AVFrame *frame, pict_t pict)
{
	for (int y = 0; y < )
}
#endif

AVFrame *picture, *tmp_picture;
uint8_t *video_outbuf;
int frame_count, video_outbuf_size;

static AVStream *add_video_stream(AVFormatContext *oc, enum AVCodecID codec_id)
{
	assert(oc);
	
	AVCodecContext *ctx = NULL;
	AVStream *st = NULL;
	
	st = av_new_stream(oc, 0);
	if (!st)
	{
		fprintf(stderr, "%d::%s:: Could not alloc stream\n", __LINE__, __FILENAME__);
		exit(1);
	}
	
	ctx = st->codec;
	ctx->codec_id = codec_id;
	ctx->codec_type = AVMEDIA_TYPE_VIDEO;
	
	ctx->bit_rate = 400000;
	
	ctx->width	= 640;
	ctx->height	= 360;
	
	ctx->time_base.den = STREAM_FRAME_RATE;
	ctx->time_base.num = 1;
	
	ctx->gop_size = 12;
	ctx->pix_fmt = STREAM_PIX_FMT;
	
	return st;
}

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
	AVFrame *picture;
	uint8_t *pic_buf;
	int size;
	
	picture = avcodec_alloc_frame();
	if (!picture)
		return NULL;
		
	size = avpicture_get_size(pix_fmt, width, height);
	pic_buf = av_malloc(size);
	if (!pic_buf)
	{
		av_free(picture);
		return NULL;
	}
	avpicture_fill((AVPicture *)picture, pic_buf, pix_fmt, width, height);
	
	return picture;
}

static void fill_yuv_image(AVFrame *pict, int frame_index, int width, int height)
{
	int i = frame_index;
	
	for (size_t y = 0; y < height; y++)
	{
		for (size_t x = 0; x < width; x++)
		{
			pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;
		}
	}
	
	for (size_t y = 0; y < height / 2; y++)
	{
		for (size_t x = 0; x < width / 2; x++)
		{
			pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
			pict->data[2][y * pict->linesize[2] + x] = 64  + x + i * 5;
		}
	}
}

static void write_video_frame(AVFormatContext *oc, AVStream *st)
{
	assert(oc != NULL);
	assert(st != NULL);
	
	int out_size = 0,
		ret		 = 0;
	
	AVCodecContext *ctx = NULL;
	static struct SwsContext *img_convert_ctx;
	
	ctx = st->codec;
	
	if (frame_count < STREAM_NB_FRAMES)
	{
		if (ctx->pix_fmt != AV_PIX_FMT_YUV420P)
		{
			if (img_convert_ctx == NULL)
			{
				img_convert_ctx = sws_getContext(ctx->width, ctx->height,
												 AV_PIX_FMT_YUV420P,
												 ctx->width, ctx->height,
												 ctx->pix_fmt,
												 sws_flags, NULL, NULL, NULL);
				if (img_convert_ctx == NULL)
				{
					fprintf(stderr, "%d::%s:: Cannot initialize the conversion context\n", __LINE__, __FILENAME__);
					exit(1);
				}
			}
			fill_yuv_image(tmp_picture, frame_count, ctx->width, ctx->height);
			sws_scale(img_convert_ctx, tmp_picture->data, tmp_picture->linesize,
					  0, ctx->height, picture->data, picture->linesize);
		}
		else
			fill_yuv_image(picture, frame_count, ctx->width, ctx->height);
	}
	
	if (oc->oformat->flags & AVFMT_NOFILE)
	{
		AVPacket pkt;
		av_init_packet(&pkt);
		
		pkt.flags |= AV_PKT_FLAG_KEY;
		pkt.stream_index = st->index;
		pkt.data = (uint8_t *)picture;
		pkt.size = sizeof(AVPicture);
		
		ret = av_interleaved_write_frame(oc, &pkt);
	}
	else
	{
		out_size = avcodec_encode_video(ctx, video_outbuf, video_outbuf_size, picture); // ?????????????????????????????????????????????????
	}
}


errno_t encode(AVCodecContext *ctx, AVFrame *frame, int ret, FILE* file)
{
	AVPacket pkt;
	uint8_t endcode[] = { 0, 0, 1, 0xb7 };
	
	int got_output = 0;

	int i;
	for (i = 0; i < 25; i ++)
	{
		av_init_packet(&pkt);
		pkt.data = NULL;
		pkt.size = 0;
		
		fflush(stdout); // Check without
		
		ret = av_frame_make_writable(frame);
		if (ret < 0)
		{
			return -1;
		}
		
		for (size_t y = 0; y < ctx->height; y++)
		{
			for (size_t x = 0; x < ctx->width; x++)
			{
				frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
			}
		}
		
		// fake data for test
		for (size_t y = 0; y < ctx->height / 2; y++)
		{
			for (size_t x = 0; x < ctx->width / 2; x++)
			{
				frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
				frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5; // Why?
			}
		}
		
		frame->pts = i;
		
		// encode the image
		ret = avcodec_encode_video2(ctx, &pkt, frame, &got_output);
		if (ret < 0)
		{
			fprintf(stderr, "Error encoding frame\n");
			return -1;
		}
		
		if (got_output)
		{
			printf("%d:: Write frame %3d (size = %5d)\n", __LINE__, i, pkt.size);
			fwrite(pkt.data, 1, pkt.size, file);
			av_packet_unref(&pkt);
		}
	}
	
	for (got_output = 1; got_output; i++)
	{
		fflush(stdout);
		
		ret = avcodec_encode_video2(ctx, &pkt, NULL, &got_output);
		if (ret < 0)
		{
			fprintf(stderr, "Error encodeing frame\n");
			return -1;
		}
		
		if (got_output)
		{
			printf("%d:: Write frame %3d (size = %5d)\n", __LINE__, i, pkt.size);
			fwrite(pkt.data, 1, pkt.size, file);
			av_packet_unref(&pkt);
		}
	}
	
	fwrite(endcode, 1, sizeof(endcode), file);
	
	
	return 0;
}

errno_t init_tool(FILE* file,
				  AVCodec			*codec,
				  AVCodecContext	*ctx,	int bit_rate,
											int width, int height,
											AVRational time_base, AVRational framerate,
											int gop_size, int max_b_frames, enum AVPixelFormat pix_fmt)
{
	ctx->bit_rate = bit_rate;
	
	ctx->width  = width;
	ctx->height = height;
	
	ctx->time_base = time_base;
	ctx->framerate = framerate;
	
	ctx->gop_size		= gop_size;
	ctx->max_b_frames	= max_b_frames;
	ctx->pix_fmt		= pix_fmt;
	
	if (avcodec_open2(ctx, codec, NULL) < 0)
	{
		fprintf(stderr, "Could not open codec\n");
		errno = -1;
		goto err;
	}
	
	AVFrame *frame = av_frame_alloc();
	if (!frame)
	{
		fprintf(stderr, "Could not allocate video frame\n");
		errno = ENOMEM;
		goto err;
	}
	frame->format = ctx->pix_fmt;
	frame->width  = ctx->width;
	frame->height = ctx->height;
	
	int ret = av_frame_get_buffer(frame, 32);
//	int ret = av_image_alloc(frame->data, frame->linesize, ctx->width, ctx->height, ctx->pix_fmt, 32);
	if (ret < 0)
	{
		fprintf(stderr, "Could not allocate the video frame data\n");
		errno = ENOMEM;
		goto err;
	}
	
	encode(ctx, frame, ret, file);
	
	av_frame_free(&frame);
	
	return 0;
	
	err:
		av_frame_free(&frame);
		return errno;
}


int main(int argc, char **argv)
{
	pict_t *frames = load_frames("../forbmp/image%03d.bmp", 250);
	if (!frames)
	{
		fprintf(stderr, "%d:: Ban\n", __LINE__);
		return 0;
	}
//	const char  *filename	= NULL,
//				*codec_name = NULL;
	
	const char  *filename	= "output.mp4",
				*codec_name = "libvpx-vp9";
	
#if 0
	if (argc <= 2)
	{
		fprintf(stderr, "Usage: %s <output file> <codec name>\n", argv[0]);
		errno = EINVAL;
		goto err;
	}
	filename	= argv[1];
	codec_name	= argv[2];
#endif
	
	AVFormatContext *oc;
	oc = avformat_alloc_context();
	if (!oc)
	{
		fprintf(stderr, "%d:: Memory error\n", __LINE__);
		goto err;
	}
	snprintf(oc->filename, sizeof(oc->filename), "%s", filename);
	
	AVOutputFormat *fmt;
	fmt = av_guess_format(NULL, filename, NULL);
	if (!fmt)
	{
#ifdef BMP_DEBUG_SESSION
		printf("%d:: Could not deduce output format from file extension: using MPEG.\n");
		fmt = av_guess_format("mpeg", NULL, NULL);
#endif
	}
	if (!fmt)
	{
		fprintf(stderr, "%d:: Could not find suitable output format\n", __LINE__);
		goto err;
	}
	oc->oformat = fmt;
	
	
	
	if (fmt->video_codec != AV_CODEC_ID_NONE) {
        add_stream(&video_st, oc, &video_codec, fmt->video_codec);
        have_video = 1;
        encode_video = 1;
    }
	
	avcodec_register_all();
	
	const AVCodec	*codec	= NULL;
	AVCodecContext	*ctx	= NULL;
	
	codec = avcodec_find_encoder_by_name(codec_name);
	if (!codec)
	{
		fprintf(stderr, "Codec not found\n");
		errno = EINVAL;
		goto err;
	}
	
	ctx = avcodec_alloc_context3(codec);
	if (!ctx)
	{
		fprintf(stderr, "Could not allocate video codec context");
		errno = ENOMEM;
		goto err;
	}
	
#if 0
{
	FILE* file = fopen(filename, "wb");
	if (!file)
	{
		fprintf(stderr, "Could not open %s\n", filename);
		errno = ENOENT;
		goto err;
	}
}
#endif
	
	if (init_tool(file, codec, ctx, 4, 640, 360, (AVRational){1, 25}, (AVRational){25, 1}, 10, 1, AV_PIX_FMT_YUV420P))
	{
		goto err;
	}
	
	
//	fclose(file);
	avfree(oc);
	avcodec_free_context(&ctx);
	
	return 0;
	
err:
	avfree(oc);
//	fclose(file);
	avcodec_free_context(&ctx);
	
	return errno;
}
