#include <sys/param.h>
#include <sys/types.h>
#include <inttypes.h>


enum EPCodecId
{
	EP_CODEC_ID_VP8,
	EP_CODEC_ID_VP9,
	EP_CODEC_ID_HEVC
};

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
 * @param frame_rate fps
 * 
 * @return special data with encoder parameters EncoderParameters_p. Can set errno on EINVAL
 */
EncoderParameters_p encoder_create(const char *filename,
//							const char *codec_name,
							enum EPCodecId codec_id,
							int width, int height,
							uint16_t frame_rate);

/**
 * @brief 
 * 
 * Adding frames for format I/O context (params->oc)
 * 
 * @param params for encoding frame
 * @param data framedata_t* [[uint8_t*]] rgb24p frame or const char* format_frame_file_name
 * @return zero on success, an errno on failure. Set errno on EIVAL or ENOMEM
 */
int encoder_add_frame(EncoderParameters_p params, size_t frame_ind, const void *data_);

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
