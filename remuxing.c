//
// Created by fdd on 2021/3/23.
//

#include "remuxing.h"
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

int remuxing(char *src_url, char *dst_url) {
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

    // 创建输出上下文
    AVFormatContext *ofmt_ctx = NULL;
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, dst_url);
    if (!ofmt_ctx) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        return ret;
    }

    // 创建输出流
    int stream_mapping_size = fmt_ctx->nb_streams;
    int *stream_mapping = av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
    if (!stream_mapping) {
        ret = AVERROR(ENOMEM);
        return ret;
    }
    AVOutputFormat *ofmt = ofmt_ctx->oformat;
    int stream_index = 0;
    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *out_stream = NULL;
        AVStream *in_stream = fmt_ctx->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
        }

        stream_mapping[i] = stream_index++;
        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            return ret;
        }

        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            fprintf(stderr, "Failed to copy codec parameters\n");
            return -1;
        }
        out_stream->codecpar->codec_tag = 0;
    }
    av_dump_format(ofmt_ctx, 0, dst_url, 1);

    // 打开输出文件
    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, dst_url, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'", dst_url);
            return -1;
        }
    }

    // 写入头
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        return -1;
    }

    AVPacket *pkt = av_packet_alloc();
    pkt->size = 0;
    pkt->data = NULL;

    // 写入流
    while ((ret = av_read_frame(fmt_ctx, pkt)) >= 0) {
        AVStream *in_stream = NULL;
        AVStream *out_stream = NULL;

        in_stream = fmt_ctx->streams[pkt->stream_index];
        if (pkt->stream_index >= stream_mapping_size ||
            stream_mapping[pkt->stream_index] < 0) {
            av_packet_unref(pkt);
            continue;
        }

        pkt->stream_index = stream_mapping[pkt->stream_index];
        out_stream = ofmt_ctx->streams[pkt->stream_index];
        log_packet(fmt_ctx, pkt, "in");

        /* copy packet */
        pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
        pkt->pos = -1;
        log_packet(ofmt_ctx, pkt, "out");

        ret = av_interleaved_write_frame(ofmt_ctx, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
        av_packet_unref(pkt);
    }

    av_write_trailer(ofmt_ctx);

    avformat_close_input(fmt_ctx);
    av_packet_free(&pkt);

    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    av_freep(&stream_mapping);
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return -1;
    }

    return 0;
}
