cmake_minimum_required(VERSION 3.17)
project(ffmpegDemo C)

set(CMAKE_C_STANDARD 99)

include_directories(/usr/local/ffmpeg/include)
link_directories(/usr/local/ffmpeg/lib)

add_executable(ffmpegDemo main.c demuxer/demuxer_audio.c demuxer/demuxer_video.c remuxing/remuxing.c decode/decode_audio.c decode/decode_audio.h)
target_link_libraries(ffmpegDemo avutil avformat avcodec swresample)