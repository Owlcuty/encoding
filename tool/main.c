#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>
//#define NDEBUG


#include <errno.h>

#include <libavcodec/avcodec.h>

#include <libavutil/opt.h>
#include <libavutil/imgutils.h>

#include <bmp.h>


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

//errno_t fill_avframe(AVFrame *frame, pict_t pict)
//{
//	for (int y = 0; y < )
//}

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
		
		// fake data for test
//		for (int y = 0; y < ctx->height / 2; y++)
//		{
//			for (int x = 0; x < ctx->width / 2; x++)
//			{
//				frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
//				frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5; // Why?
//			}
//		}
		
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
			printf("Write frame %3d (size=%5d)\n", i, pkt.size);
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
			printf("Write frame %3d (size=%5d)\n", i, pkt.size);
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
	
	int ret = av_frame_get_buffer(frame, 0);
	if (ret < 0)
	{
		fprintf(stderr, "Could not allocate the video frame data\n");
		errno = ENOMEM;
		goto err;
	}
	
	encode(ctx, frame, ret, file);
	
	av_frame_free(frame);
	
	return 0;
	
	err:
		av_frame_free(frame);
		return errno;
}


int main(int argc, char **argv)
{
	pict_t *frames = load_frames("../forbmp/image%03d.bmp", 200);
	if (!frames)
	{
		fprintf(stderr, "%d:: Ban\n", __LINE__);
		return 0;
	}
//	const char  *filename	= NULL,
//				*codec_name = NULL;
	
	const char  *filename	= "output.mp4",
				*codec_name = "VP9";
	
//	if (argc <= 2)
//	{
//		fprintf(stderr, "Usage: %s <output file> <codec name>\n", argv[0]);
//		errno = EINVAL;
//		goto err;
//	}
//	filename	= argv[1];
//	codec_name	= argv[2];
	
	avcodec_register_all();
	
	const AVCodec	*codec	= NULL;
	AVCodecContext	*ctx	= NULL;
	
//	codec = avcodec_find_encoder_by_name(codec_name);
//	if (!codec)
//	{
//		fprintf(stderr, "Codec not found\n");
//		errno = EINVAL;
//		goto err;
//	}
//	
//	ctx = avcodec_alloc_context3(codec);
//	if (!ctx)
//	{
//		fprintf(stderr, "Could not allocate video codec context");
//		errno = ENOMEM;
//		goto err;
//	}
//	
//	FILE* file = fopen(filename, "wb");
//	if (!file)
//	{
//		fprintf(stderr, "Could not open %s\n", filename);
//		errno = ENOENT;
//		goto err;
//	}
//	
//	if (init_tool(file, codec, ctx, 400000, 640, 360, (AVRational){1, 25}, (AVRational){25, 1}, 10, 1, AV_PIX_FMT_YUV420P))
//	{
//		goto err;
//	}
//	
//	
//	fclose(file);
//	avcodec_free_context(&ctx);
//	
//	return 0;
//	
//err:
//	fclose(file);
//	avcodec_free_context(&ctx);
//	
//	return errno;
}
