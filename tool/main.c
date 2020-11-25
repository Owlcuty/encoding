#include "encoder.h"

#include "bmp.h"


int main(int argc, char **argv)
{
	const char *filename = "output.webm";

	const int codec_id = EP_CODEC_ID_VP9;

	int width  = 3840;
	int height = 1080;

	int ret = 0;

	EncoderParameters_p params = encoder_create(filename, codec_id, width, height);
	if (params == NULL)
	{
		perror("encoder_create() fail: ");
		return errno;
	}

	framedata_t bmp = NULL;
	int frame_ind = 1;

	while (EP_get_encode_video(params))
	{
		printf("HERE!!!\n");
		ret = load_frame(&bmp, "../forbmp1080p/image%05d.bmp", frame_ind++);
		if (ret < 0)
		{
			perror("load_frame() fail: ");
			return errno;
		}
		if (bmp == NULL)
		{
			perror("wtf: ");
			return errno;
		}

		ret = encoder_add_frame(params, frame_ind, bmp, 0);
		if (ret < 0)
		{
			return -1;
		}

		free(bmp);
		printf("Still HERE!!!\n");
	}

	ret = encoder_write(params);
	if (ret < 0)
	{
		encoder_destruct(params);
		return -1;
	}
	
	encoder_destruct(params);

	return ret;
}
