//
// Created by fdd on 2021/3/23.
//

#include "demuxer_audio.h"
#include <libavformat/avformat.h>

int demuxer_audio(char *src_url, char *dst_url) {
    int ret = 0;

    if (!src_url || !dst_url) {
        av_log(NULL, AV_LOG_ERROR, "src or dts file is null, plz check them!\n");
        return -1;
    }

    av_register_all();

    // 打开多媒体上下文
    AVFormatContext *fmt_ctx = NULL;
    if ((ret = avformat_open_input(&fmt_ctx, src_url, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "could not open source file: %s, %d(%s)\n", src_url, ret, av_err2str(ret));
        avformat_close_input(&fmt_ctx);
        return ret;
    }

    // 检索多媒体信息
    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "failed to find stream infomation: %s, %d(%s)\n", src_url, ret, av_err2str(ret));
        avformat_close_input(&fmt_ctx);
        return ret;
    }

    // 打印多媒体信息
    av_dump_format(fmt_ctx, 0, src_url, 0);

    // 找到最佳流
    int audio_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_stream_index < 0) {
        av_log(NULL, AV_LOG_DEBUG, "Could not find %s stream in input file %s\n",
               av_get_media_type_string(AVMEDIA_TYPE_AUDIO),
               src_url);
        avformat_close_input(&fmt_ctx);
        return AVERROR(EINVAL);
    }

    // 查找输入流的编码参数
    AVStream *in_stream = fmt_ctx->streams[audio_stream_index];
    AVCodecParameters *in_codecpar = in_stream->codecpar;
    if(in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO){
        av_log(NULL, AV_LOG_ERROR, "The Codec type is invalid!\n");
        return -1;
    }

    // 创建输出流
    AVFormatContext *ofmt_ctx = avformat_alloc_context();
    AVOutputFormat *output_fmt = av_guess_format(NULL, dst_url, NULL);
    if(!output_fmt){
        av_log(NULL, AV_LOG_ERROR, "Cloud not guess file format \n");
        return -1;
    }
    ofmt_ctx->oformat = output_fmt;
    AVStream *out_stream = avformat_new_stream(ofmt_ctx, NULL);
    if(!out_stream){
        av_log(NULL, AV_LOG_ERROR, "Failed to create out stream!\n");
        return -1;
    }
    if(fmt_ctx->nb_streams<2){
        av_log(NULL, AV_LOG_ERROR, "the number of stream is too less!\n");
        return -1;
    }

    // 复制参数
    if((ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar)) < 0 ){
        av_log(NULL, AV_LOG_ERROR,
               "Failed to copy codec parameter, %d(%s)\n",
               ret, av_err2str(ret));
        return -1;
    }
    out_stream->codecpar->codec_tag = 0;

    if((ret = avio_open(&ofmt_ctx->pb, dst_url, AVIO_FLAG_WRITE)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open file %s, %d(%s)\n",
               dst_url,
               ret,
               av_err2str(ret));
        return -1;
    }

    /*dump output information*/
    av_dump_format(ofmt_ctx, 0, dst_url, 1);

    // packet初始化
    AVPacket *pkt = av_packet_alloc();
    pkt->data = NULL;
    pkt->size = 0;
//    av_new_packet(pkt, 4096);

    if (avformat_write_header(ofmt_ctx, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file");
        return -1;
    }

    // 读取数据
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == audio_stream_index) {
            pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt->dts = pkt->pts;
            pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
            pkt->pos = -1;
            pkt->stream_index = 0;
            av_interleaved_write_frame(ofmt_ctx, pkt);
        }
        av_packet_unref(pkt);
    }
    av_write_trailer(ofmt_ctx);

    avformat_close_input(&fmt_ctx);
    av_packet_free(&pkt);

    avio_close(ofmt_ctx->pb);
    return 0;
}

