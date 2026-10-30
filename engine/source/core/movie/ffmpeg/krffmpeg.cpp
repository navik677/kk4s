extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavfilter/avfilter.h"
}

// kk4s: FFmpeg >= 4 registers codecs/formats/filters automatically. The old
// code linked filters through AVFilter::next, which was removed in FFmpeg 5.
static bool FFInitilalized = false;
void TVPInitLibAVCodec() {
	if (!FFInitilalized) {
		avformat_network_init();
		FFInitilalized = true;
	}
}
