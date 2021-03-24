#include "demuxer/demuxer_audio.h"
#include "demuxer/demuxer_video.h"
#include "remuxing/remuxing.h"
#include "decode/decode_audio.h"


int main() {
//    int ret = demuxer_audio("../resource/test.mp4", "./output.aac");
//    int ret = demuxer_video("../resource/test.mp4", "./output.h264");
//    int ret = remuxing("../resource/test.mp4", "./output.flv");
    int ret = decode_audio("output.aac", "output.pcm");


    return ret;
}

