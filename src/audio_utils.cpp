#include "audio_utils.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

#include <fstream>
#include <cstring>

namespace duckdb {

bool AudioUtils::LoadAudioFile(const std::string &file_path, std::vector<float> &output, std::string &error) {
	AVFormatContext *format_ctx = nullptr;
	AVCodecContext *codec_ctx = nullptr;
	SwrContext *swr_ctx = nullptr;
	AVPacket *packet = nullptr;
	AVFrame *frame = nullptr;
	int audio_stream_idx = -1;

	// Open input file
	if (avformat_open_input(&format_ctx, file_path.c_str(), nullptr, nullptr) < 0) {
		error = "Failed to open audio file: " + file_path;
		return false;
	}

	// Find stream info
	if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
		error = "Failed to find stream info";
		avformat_close_input(&format_ctx);
		return false;
	}

	// Find audio stream
	for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
		if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audio_stream_idx = i;
			break;
		}
	}

	if (audio_stream_idx < 0) {
		error = "No audio stream found in file";
		avformat_close_input(&format_ctx);
		return false;
	}

	AVCodecParameters *codecpar = format_ctx->streams[audio_stream_idx]->codecpar;

	// Find decoder
	const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
	if (!codec) {
		error = "Unsupported audio codec";
		avformat_close_input(&format_ctx);
		return false;
	}

	// Allocate codec context
	codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx) {
		error = "Failed to allocate codec context";
		avformat_close_input(&format_ctx);
		return false;
	}

	if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
		error = "Failed to copy codec parameters";
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&format_ctx);
		return false;
	}

	if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
		error = "Failed to open codec";
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&format_ctx);
		return false;
	}

	// Set up resampler for 16kHz mono float32
	AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
	AVChannelLayout in_ch_layout;

	if (codec_ctx->ch_layout.nb_channels > 0) {
		av_channel_layout_copy(&in_ch_layout, &codec_ctx->ch_layout);
	} else {
		av_channel_layout_default(&in_ch_layout,
		                          codecpar->ch_layout.nb_channels > 0 ? codecpar->ch_layout.nb_channels : 2);
	}

	swr_alloc_set_opts2(&swr_ctx, &out_ch_layout, AV_SAMPLE_FMT_FLT, WHISPER_SAMPLE_RATE, &in_ch_layout,
	                    codec_ctx->sample_fmt, codec_ctx->sample_rate, 0, nullptr);

	if (!swr_ctx || swr_init(swr_ctx) < 0) {
		error = "Failed to initialize resampler";
		av_channel_layout_uninit(&in_ch_layout);
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&format_ctx);
		return false;
	}

	av_channel_layout_uninit(&in_ch_layout);

	packet = av_packet_alloc();
	frame = av_frame_alloc();

	if (!packet || !frame) {
		error = "Failed to allocate packet/frame";
		if (packet)
			av_packet_free(&packet);
		if (frame)
			av_frame_free(&frame);
		swr_free(&swr_ctx);
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&format_ctx);
		return false;
	}

	output.clear();

	// Read and decode packets
	while (av_read_frame(format_ctx, packet) >= 0) {
		if (packet->stream_index == audio_stream_idx) {
			if (avcodec_send_packet(codec_ctx, packet) >= 0) {
				while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
					// Calculate output samples
					int64_t delay = swr_get_delay(swr_ctx, codec_ctx->sample_rate);
					int64_t out_samples = av_rescale_rnd(delay + frame->nb_samples, WHISPER_SAMPLE_RATE,
					                                     codec_ctx->sample_rate, AV_ROUND_UP);

					std::vector<float> buffer(out_samples);
					uint8_t *out_buf = reinterpret_cast<uint8_t *>(buffer.data());

					int samples_converted =
					    swr_convert(swr_ctx, &out_buf, out_samples, const_cast<const uint8_t **>(frame->extended_data),
					                frame->nb_samples);

					if (samples_converted > 0) {
						output.insert(output.end(), buffer.begin(), buffer.begin() + samples_converted);
					}
				}
			}
		}
		av_packet_unref(packet);
	}

	// Flush decoder
	avcodec_send_packet(codec_ctx, nullptr);
	while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
		int64_t delay = swr_get_delay(swr_ctx, codec_ctx->sample_rate);
		int64_t out_samples =
		    av_rescale_rnd(delay + frame->nb_samples, WHISPER_SAMPLE_RATE, codec_ctx->sample_rate, AV_ROUND_UP);

		std::vector<float> buffer(out_samples);
		uint8_t *out_buf = reinterpret_cast<uint8_t *>(buffer.data());

		int samples_converted = swr_convert(swr_ctx, &out_buf, out_samples,
		                                    const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);

		if (samples_converted > 0) {
			output.insert(output.end(), buffer.begin(), buffer.begin() + samples_converted);
		}
	}

	// Flush resampler
	int64_t delay = swr_get_delay(swr_ctx, WHISPER_SAMPLE_RATE);
	if (delay > 0) {
		std::vector<float> buffer(delay);
		uint8_t *out_buf = reinterpret_cast<uint8_t *>(buffer.data());
		int samples_converted = swr_convert(swr_ctx, &out_buf, delay, nullptr, 0);
		if (samples_converted > 0) {
			output.insert(output.end(), buffer.begin(), buffer.begin() + samples_converted);
		}
	}

	// Cleanup
	av_packet_free(&packet);
	av_frame_free(&frame);
	swr_free(&swr_ctx);
	avcodec_free_context(&codec_ctx);
	avformat_close_input(&format_ctx);

	return true;
}

bool AudioUtils::LoadAudioFromMemory(const uint8_t *data, size_t size, std::vector<float> &output, std::string &error) {
	// For memory loading, we use a custom AVIOContext
	AVFormatContext *format_ctx = nullptr;
	AVCodecContext *codec_ctx = nullptr;
	SwrContext *swr_ctx = nullptr;
	AVPacket *packet = nullptr;
	AVFrame *frame = nullptr;
	AVIOContext *avio_ctx = nullptr;
	int audio_stream_idx = -1;

	// Create buffer for AVIO
	const size_t avio_buffer_size = 4096;
	uint8_t *avio_buffer = static_cast<uint8_t *>(av_malloc(avio_buffer_size));
	if (!avio_buffer) {
		error = "Failed to allocate AVIO buffer";
		return false;
	}

	// Structure to hold buffer state
	struct BufferData {
		const uint8_t *ptr;
		size_t size;
		size_t pos;
	};

	BufferData buffer_data = {data, size, 0};

	// Custom read function
	auto read_packet = [](void *opaque, uint8_t *buf, int buf_size) -> int {
		BufferData *bd = static_cast<BufferData *>(opaque);
		size_t remaining = bd->size - bd->pos;
		if (remaining == 0)
			return AVERROR_EOF;
		size_t to_read = std::min(static_cast<size_t>(buf_size), remaining);
		memcpy(buf, bd->ptr + bd->pos, to_read);
		bd->pos += to_read;
		return static_cast<int>(to_read);
	};

	// Custom seek function
	auto seek_packet = [](void *opaque, int64_t offset, int whence) -> int64_t {
		BufferData *bd = static_cast<BufferData *>(opaque);
		int64_t new_pos;
		switch (whence) {
		case SEEK_SET:
			new_pos = offset;
			break;
		case SEEK_CUR:
			new_pos = bd->pos + offset;
			break;
		case SEEK_END:
			new_pos = bd->size + offset;
			break;
		case AVSEEK_SIZE:
			return bd->size;
		default:
			return AVERROR(EINVAL);
		}
		if (new_pos < 0 || static_cast<size_t>(new_pos) > bd->size) {
			return AVERROR(EINVAL);
		}
		bd->pos = new_pos;
		return new_pos;
	};

	avio_ctx = avio_alloc_context(avio_buffer, avio_buffer_size, 0, &buffer_data, read_packet, nullptr, seek_packet);
	if (!avio_ctx) {
		av_free(avio_buffer);
		error = "Failed to allocate AVIO context";
		return false;
	}

	format_ctx = avformat_alloc_context();
	if (!format_ctx) {
		av_free(avio_ctx->buffer);
		avio_context_free(&avio_ctx);
		error = "Failed to allocate format context";
		return false;
	}

	format_ctx->pb = avio_ctx;

	if (avformat_open_input(&format_ctx, nullptr, nullptr, nullptr) < 0) {
		error = "Failed to open audio from memory";
		av_free(avio_ctx->buffer);
		avio_context_free(&avio_ctx);
		return false;
	}

	// From here, similar to LoadAudioFile
	if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
		error = "Failed to find stream info";
		avformat_close_input(&format_ctx);
		av_free(avio_ctx->buffer);
		avio_context_free(&avio_ctx);
		return false;
	}

	for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
		if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audio_stream_idx = i;
			break;
		}
	}

	if (audio_stream_idx < 0) {
		error = "No audio stream found in data";
		avformat_close_input(&format_ctx);
		av_free(avio_ctx->buffer);
		avio_context_free(&avio_ctx);
		return false;
	}

	AVCodecParameters *codecpar = format_ctx->streams[audio_stream_idx]->codecpar;
	const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
	if (!codec) {
		error = "Unsupported audio codec";
		avformat_close_input(&format_ctx);
		av_free(avio_ctx->buffer);
		avio_context_free(&avio_ctx);
		return false;
	}

	codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx) {
		error = "Failed to allocate codec context";
		avformat_close_input(&format_ctx);
		av_free(avio_ctx->buffer);
		avio_context_free(&avio_ctx);
		return false;
	}

	if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
		error = "Failed to copy codec parameters";
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&format_ctx);
		av_free(avio_ctx->buffer);
		avio_context_free(&avio_ctx);
		return false;
	}

	if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
		error = "Failed to open codec";
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&format_ctx);
		av_free(avio_ctx->buffer);
		avio_context_free(&avio_ctx);
		return false;
	}

	AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
	AVChannelLayout in_ch_layout;

	if (codec_ctx->ch_layout.nb_channels > 0) {
		av_channel_layout_copy(&in_ch_layout, &codec_ctx->ch_layout);
	} else {
		av_channel_layout_default(&in_ch_layout,
		                          codecpar->ch_layout.nb_channels > 0 ? codecpar->ch_layout.nb_channels : 2);
	}

	swr_alloc_set_opts2(&swr_ctx, &out_ch_layout, AV_SAMPLE_FMT_FLT, WHISPER_SAMPLE_RATE, &in_ch_layout,
	                    codec_ctx->sample_fmt, codec_ctx->sample_rate, 0, nullptr);

	if (!swr_ctx || swr_init(swr_ctx) < 0) {
		error = "Failed to initialize resampler";
		av_channel_layout_uninit(&in_ch_layout);
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&format_ctx);
		av_free(avio_ctx->buffer);
		avio_context_free(&avio_ctx);
		return false;
	}

	av_channel_layout_uninit(&in_ch_layout);

	packet = av_packet_alloc();
	frame = av_frame_alloc();

	if (!packet || !frame) {
		error = "Failed to allocate packet/frame";
		if (packet)
			av_packet_free(&packet);
		if (frame)
			av_frame_free(&frame);
		swr_free(&swr_ctx);
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&format_ctx);
		av_free(avio_ctx->buffer);
		avio_context_free(&avio_ctx);
		return false;
	}

	output.clear();

	while (av_read_frame(format_ctx, packet) >= 0) {
		if (packet->stream_index == audio_stream_idx) {
			if (avcodec_send_packet(codec_ctx, packet) >= 0) {
				while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
					int64_t delay = swr_get_delay(swr_ctx, codec_ctx->sample_rate);
					int64_t out_samples = av_rescale_rnd(delay + frame->nb_samples, WHISPER_SAMPLE_RATE,
					                                     codec_ctx->sample_rate, AV_ROUND_UP);

					std::vector<float> buffer(out_samples);
					uint8_t *out_buf = reinterpret_cast<uint8_t *>(buffer.data());

					int samples_converted =
					    swr_convert(swr_ctx, &out_buf, out_samples, const_cast<const uint8_t **>(frame->extended_data),
					                frame->nb_samples);

					if (samples_converted > 0) {
						output.insert(output.end(), buffer.begin(), buffer.begin() + samples_converted);
					}
				}
			}
		}
		av_packet_unref(packet);
	}

	avcodec_send_packet(codec_ctx, nullptr);
	while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
		int64_t delay = swr_get_delay(swr_ctx, codec_ctx->sample_rate);
		int64_t out_samples =
		    av_rescale_rnd(delay + frame->nb_samples, WHISPER_SAMPLE_RATE, codec_ctx->sample_rate, AV_ROUND_UP);

		std::vector<float> buffer(out_samples);
		uint8_t *out_buf = reinterpret_cast<uint8_t *>(buffer.data());

		int samples_converted = swr_convert(swr_ctx, &out_buf, out_samples,
		                                    const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);

		if (samples_converted > 0) {
			output.insert(output.end(), buffer.begin(), buffer.begin() + samples_converted);
		}
	}

	int64_t delay = swr_get_delay(swr_ctx, WHISPER_SAMPLE_RATE);
	if (delay > 0) {
		std::vector<float> buffer(delay);
		uint8_t *out_buf = reinterpret_cast<uint8_t *>(buffer.data());
		int samples_converted = swr_convert(swr_ctx, &out_buf, delay, nullptr, 0);
		if (samples_converted > 0) {
			output.insert(output.end(), buffer.begin(), buffer.begin() + samples_converted);
		}
	}

	av_packet_free(&packet);
	av_frame_free(&frame);
	swr_free(&swr_ctx);
	avcodec_free_context(&codec_ctx);
	avformat_close_input(&format_ctx);
	av_free(avio_ctx->buffer);
	avio_context_free(&avio_ctx);

	return true;
}

bool AudioUtils::GetAudioMetadata(const std::string &file_path, AudioMetadata &metadata, std::string &error) {
	AVFormatContext *format_ctx = nullptr;

	if (avformat_open_input(&format_ctx, file_path.c_str(), nullptr, nullptr) < 0) {
		error = "Failed to open audio file: " + file_path;
		return false;
	}

	if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
		error = "Failed to find stream info";
		avformat_close_input(&format_ctx);
		return false;
	}

	int audio_stream_idx = -1;
	for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
		if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audio_stream_idx = i;
			break;
		}
	}

	if (audio_stream_idx < 0) {
		error = "No audio stream found in file";
		avformat_close_input(&format_ctx);
		return false;
	}

	AVStream *stream = format_ctx->streams[audio_stream_idx];
	AVCodecParameters *codecpar = stream->codecpar;

	metadata.duration_seconds =
	    format_ctx->duration > 0 ? static_cast<double>(format_ctx->duration) / AV_TIME_BASE : 0.0;
	metadata.sample_rate = codecpar->sample_rate;
	metadata.channels = codecpar->ch_layout.nb_channels;
	metadata.format = format_ctx->iformat->name;

	// Get file size
	std::ifstream file(file_path, std::ios::binary | std::ios::ate);
	metadata.file_size = file.is_open() ? static_cast<int64_t>(file.tellg()) : 0;

	avformat_close_input(&format_ctx);
	return true;
}

bool AudioUtils::CheckAudioFile(const std::string &file_path, std::string &error) {
	AVFormatContext *format_ctx = nullptr;

	if (avformat_open_input(&format_ctx, file_path.c_str(), nullptr, nullptr) < 0) {
		error = "Cannot open file or unsupported format";
		return false;
	}

	if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
		error = "Cannot read stream info";
		avformat_close_input(&format_ctx);
		return false;
	}

	bool has_audio = false;
	for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
		if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			AVCodecParameters *codecpar = format_ctx->streams[i]->codecpar;
			const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
			if (codec) {
				has_audio = true;
				break;
			}
		}
	}

	avformat_close_input(&format_ctx);

	if (!has_audio) {
		error = "No supported audio stream found";
		return false;
	}

	return true;
}

} // namespace duckdb
