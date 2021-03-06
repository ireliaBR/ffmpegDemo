//
// Created by fdd on 2021/3/24.
//

#include "decode_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/frame.h>
#include <libavutil/mem.h>

#include <libavcodec/avcodec.h>

#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

static void decode(AVCodecContext *dec_ctx, AVPacket *pkt, AVFrame *frame,
                   FILE *outfile);

static int get_format_from_sample_fmt(const char **fmt,
                                      enum AVSampleFormat sample_fmt);

int decode_audio(char *src_url, char *dst_url) {
    int ret = 0;
    AVPacket *pkt = av_packet_alloc();

    // 创建解码器
    AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    if (!codec) {
        av_log(NULL, AV_LOG_ERROR, "Codec not found\n");
        return -1;
    }
    AVCodecParserContext *parser = av_parser_init(codec->id);
    if (!parser) {
        av_log(NULL, AV_LOG_ERROR, "Parser not found\n");
        return -1;
    }
    AVCodecContext *c = avcodec_alloc_context3(codec);
    if (!c) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate audio codec context\n");
        return -1;
    }
    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open codec\n");
        return -1;
    }

    // 打开输入输出文件
    FILE *f = fopen(src_url, "rb");
    if (!f) {
        av_log(NULL, AV_LOG_ERROR, "Could not open %s\n", src_url);
        return -1;
    }
    FILE *outfile = fopen(dst_url, "wb");
    if (!outfile) {
        av_free(c);
        return -1;
    }

    uint8_t inbuf[AUDIO_INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE] = {0, };
    uint8_t *data = inbuf;
    size_t data_size = fread(inbuf, 1, AUDIO_INBUF_SIZE, f);
    // 解码
    AVFrame *decode_frame = NULL;
    while (data_size > 0) {
        if (!decode_frame) {
            if (!(decode_frame = av_frame_alloc())) {
                av_log(NULL, AV_LOG_ERROR, "Could not allocate audio frame\n");
                return -1;
            }
        }

        // AVCodecParser用于解析输入的数据流并把它们分成一帧一帧的压缩编码数据。比较形象的说法就是把长长的一段连续的数据“切割”成一段段的数据。核心函数是av_parser_parse2()：
        //av_parser_parse2()：解析数据获得一个Packet， 从输入的数据流中分离出一帧一帧的压缩编码数据。
        ret = av_parser_parse2(parser, c, &pkt->data, &pkt->size, data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error while parsing\n");
            return -1;
        }

        data += ret;
        data_size -= ret;

        if (pkt->size) {
            decode(c, pkt, decode_frame, outfile);
        }

        if (data_size < AUDIO_REFILL_THRESH) {
            memmove(inbuf, data, data_size);
            data = inbuf;
            size_t len = fread(data + data_size, 1, AUDIO_INBUF_SIZE - data_size, f);
            if (len > 0) {
                data_size += len;
            }
        }
        av_packet_unref(pkt);
    }

    /* flush the decoder */
    pkt->data = NULL;
    pkt->size = 0;
    decode(c, pkt, decode_frame, outfile);
    // 结束，下面是打印信息

    /* print output pcm infomations, because there have no metadata of pcm */
    enum AVSampleFormat sfmt = c->sample_fmt;

    if (av_sample_fmt_is_planar(sfmt)) {
        const char *packed = av_get_sample_fmt_name(sfmt);
        printf("Warning: the sample format the decoder produced is planar "
               "(%s). This example will output the first channel only.\n",
               packed ? packed : "?");
        sfmt = av_get_packed_sample_fmt(sfmt);
    }

    const char *fmt;
    int n_channels = c->channels;
    if ((ret = get_format_from_sample_fmt(&fmt, sfmt)) < 0)
        goto end;

    printf("Play the output audio file with the command:\n"
           "ffplay -f %s -ac %d -ar %d %s\n",
           fmt, n_channels, c->sample_rate,
           dst_url);
    end:
    fclose(outfile);
    fclose(f);

    avcodec_free_context(&c);
    av_parser_close(parser);
    av_frame_free(&decode_frame);
    av_packet_free(&pkt);
    return ret;
}

static void decode(AVCodecContext *dec_ctx, AVPacket *pkt, AVFrame *frame,
                   FILE *outfile) {
    int i, ch;
    int ret, data_size;

    /* send the packet with the compressed data to the decoder */
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error submitting the packet to the decoder\n");
        exit(1);
    }

    /* read all the output frames (in general there may be any number of them */
    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }
        data_size = av_get_bytes_per_sample(dec_ctx->sample_fmt);
        if (data_size < 0) {
            /* This should not occur, checking just for paranoia */
            fprintf(stderr, "Failed to calculate data size\n");
            exit(1);
        }
        for (i = 0; i < frame->nb_samples; i++)
            for (ch = 0; ch < dec_ctx->channels; ch++)
                fwrite(frame->data[ch] + data_size*i, 1, data_size, outfile);
    }
}

static int get_format_from_sample_fmt(const char **fmt,
                                      enum AVSampleFormat sample_fmt) {
    int i;
    struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt; const char *fmt_be, *fmt_le;
    } sample_fmt_entries[] = {
            { AV_SAMPLE_FMT_U8,  "u8",    "u8"    },
            { AV_SAMPLE_FMT_S16, "s16be", "s16le" },
            { AV_SAMPLE_FMT_S32, "s32be", "s32le" },
            { AV_SAMPLE_FMT_FLT, "f32be", "f32le" },
            { AV_SAMPLE_FMT_DBL, "f64be", "f64le" },
    };
    *fmt = NULL;

    for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt) {
            *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
            return 0;
        }
    }

    fprintf(stderr,
            "sample format %s is not supported as output format\n",
            av_get_sample_fmt_name(sample_fmt));
    return -1;
}