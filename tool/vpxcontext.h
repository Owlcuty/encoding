#if 0

#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>

#include "libavcodec/avcodec.h"
//#include "internal.h"
#include "libavutil/avassert.h"
//#include "libvpx.h"
//#include "profiles.h"
#include "libavutil/base64.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"


typedef struct VPxEncoderContext
{
	vpx_codec_ctx_t		encoder;
	vpx_image_t			rawimg;
	
	// Not YUVA420P
	vpx_codec_ctx_t		encoder_alpha; // ---
	vpx_image_t			rawimg_alpha;  // ---
	uint8_t				is_alpha; // 0
	
	vpx_fixed_buf_t		twopass_stats;
	int cpu_used;
	/**
	 * VP8 specific flags
	 */
	int flags;
	
	int auto_alt_ref;
	
	int arnr_max_frames;
	int arnr_strength;
	int arnr_type;
	
	int tune;
	
	int lag_in_frames;
	int error_resilient;
	int crf;
	int static_thresh;
	int max_intra_rate;
	int rc_undershoot_pct;
	int rc_overshoot_pct;
	
	// VP9-only
	int lossless;
	int tile_columns;
	int tile_rows;
	int frame_parallel;
	int aq_mode;
	int drop_threshold;
	int noise_sensitivity;
	int vpx_cs;
	float level;
	int row_mt;
	int tune_content;
	int corpus_complexity;
} VPxContext;

const VPxContext vp9_context = {
	.cpu_used = 4,
	.arnr_max_frames = 10,
	.lag_in_frames = 10
};

const VPxContext vp8_context = {
	.cpu_used = 4,
	.arnr_max_frames = 10,
	.lag_in_frames = 10
};


#endif