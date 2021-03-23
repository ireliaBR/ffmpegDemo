#include "demuxer_audio.h"
#include "demuxer_video.h"
#include "remuxing.h"

int main() {
//    int ret = demuxer_audio("../test.mp4", "./output.aac");
//    int ret = demuxer_video("../test.mp4", "./output.h264");
    int ret = remuxing("../test.mp4", "./output.flv");

    return ret;
}
