#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
//#define NDEBUG

#include <time.h>


#include <errno.h>


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


enum EPCodecId
{
	EP_CODEC_ID_VP8,
	EP_CODEC_ID_VP9
};

//#define FFVER_3_0

typedef uint8_t *framedata_t;

// Private data. To get encode_video use EP_get_encode_video()
typedef struct EncoderParameters *EncoderParameters_p;

int EP_get_encode_video(EncoderParameters_p params);

/**
 * @brief Allocate a new EncoderParameters_p and set its fields to default values.
 * The returned struct must be freed with
 * encoder_destruct().
 * 
 * @param filename name of output media file
 * //@param codec_name name of the requested encoder
 * @param codec_id 
 * @param width of output media file
 * @param height of output media file
 * 
 * @return special data with encoder parameters EncoderParameters_p. Can set errno on EINVAL
 */
EncoderParameters_p encoder_create(const char *filename,
//							const char *codec_name,
							enum EPCodecId codec_id,
							int width, int height);

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
int encoder_add_frame(EncoderParameters_p params, size_t frame_ind, const void *data_, int type);

/**
 * @brief Write the stream trailer to an output media file, free the
 * file private data and close stream
 * @param params struct with codec parameters
 * @return 0 on success, an AVERROR < 0 on failure. Can set errno on EINVAL
 */
int encoder_write(EncoderParameters_p params);

/**
 * @brief free [[EncoderParameters_p]] struct with codec parameters
 * @param params struct to freed
 */
void encoder_destruct(EncoderParameters_p params);
