// RtspSource-SavaFile.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>
#include <string>
#include <thread>
#include <dshow.h>

using namespace std;

extern "C" {
#include "libavutil/opt.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/samplefmt.h"
#include "libavutil/time.h"
#include "libavutil/fifo.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavfilter/avfiltergraph.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavdevice/avdevice.h"
}

AVFormatContext *inputContext;
AVFormatContext *outputContext;
AVFilterContext *buffersinkCtx;
AVFilterContext *buffersrcCtx;

AVCodecContext*	outPutAudioEncContext = NULL;

int64_t audioCount = 0;
AVFilterGraph *filterGraph;

//ffmpeg初始化
void  Init() {
	av_register_all();
	//avcodec_register_all();
	avfilter_register_all();
	avformat_network_init();
	avdevice_register_all();//设备注册
	av_log_set_level(AV_LOG_ERROR);
}

//回调函数
//static int interrupt_cb(void *ctx)
//{
//	int  timeout = 3;
//	if (av_gettime() - lastReadPacktTime > timeout * 1000 * 1000)
//	{
//		return -1;
//	}
//	return 0;
//}

int OpenInput(string srcUrl) {
	//创建输入上下文
	inputContext = avformat_alloc_context();
	//设置回调
	//inputContext->interrupt_callback.callback = interrupt_cb;

	//输入格式
	AVInputFormat *ifmt = av_find_input_format("dshow");
	//输入参数
	AVDictionary *format_opts = nullptr;
	//设置参数，缓冲区大小，避免卡顿
	av_dict_set_int(&format_opts, "audio_buffer_size", 20, 0);
	//根据路径判断使用哪一个Demuxer
	int ret = avformat_open_input(&inputContext, srcUrl.c_str(), ifmt, &format_opts);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "open input file failed!\n");
		char *buf = new char(1024);
		av_strerror(ret, buf, 1024);
		cout << "error =" << buf << endl;
		return ret;
	}
	//把所有的steam信息填充好
	ret = avformat_find_stream_info(inputContext, nullptr);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Find input file stream failed!\n");
	}
	else {
		ret = avcodec_open2(inputContext->streams[0]->codec, avcodec_find_decoder(inputContext->streams[0]->codec->codec_id), nullptr);
		av_log(NULL, AV_LOG_FATAL, "open input file %s success!\n", srcUrl.c_str());
	}

	return ret;

}

int OpenOutput(string outUrl) {
	//创建输出上下文,内部调用avformat_alloc_context来分配一个AVFormatContext结构体
	int ret = avformat_alloc_output_context2(&outputContext, nullptr, "mpegts", outUrl.c_str());
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "open output context failed!\n");
		goto Error;
	}
	//avio打开，其实是avformat_open_input的逆过程
	ret = avio_open2(&outputContext->pb, outUrl.c_str(), AVIO_FLAG_WRITE, nullptr, nullptr);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "open avio failed!\n");
		goto Error;
	}
	for (int i = 0; i < inputContext->nb_streams; i++) {
		//av_find_stream_info的逆过程
		AVStream *stream = avformat_new_stream(outputContext, outPutAudioEncContext->codec);		
		stream->codec = outPutAudioEncContext;
		AVCodecContext *codecContext = inputContext->streams[0]->codec;
		if (ret < 0) {
			av_log(NULL, AV_LOG_ERROR, "copy codec context failed\n");
			goto Error;
		}

	}
	//写头信息，表示文件格式
	//av_find_stream_info的逆过程
	ret = avformat_write_header(outputContext, nullptr);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "write header failed\n");
		goto Error;
	}
	av_log(NULL, AV_LOG_FATAL, "write output file success\n");
	return ret;

Error:
	if (outputContext) {
		for (int i = 0; i < outputContext->nb_streams; i++) {
			avcodec_close(outputContext->streams[i]->codec);
		}
		avformat_close_input(&outputContext);
	}
	return ret;

}

void CloseInput() {
	if (inputContext != nullptr) {
		avformat_close_input(&inputContext);
	}
}

void CloseOutput() {
	if (outputContext != nullptr) {
		for (int i = 0; i < outputContext->nb_streams; i++) {
			AVCodecContext * avCodecContext = outputContext->streams[i]->codec;
			avcodec_close(avCodecContext);
		}
		avformat_close_input(&outputContext);
	}
}

//shared_ptr智能指针，可以活动释放该指针的麻烦
shared_ptr<AVPacket> ReadPacketFromSource() {
	shared_ptr<AVPacket> packet(static_cast<AVPacket*>(av_malloc(sizeof(AVPacket))),
		[&](AVPacket *p) {av_packet_free(&p); av_freep(&p); });
	//从输入流中读取保存到packet中
	av_init_packet(packet.get());
	//读取数据，音频一个AVPacket有多个AVFrame，而视频只有一个
	int ret = av_read_frame(inputContext, packet.get());
	if (ret >= 0) {

		return packet;
	}
	else {
		return nullptr;
	}

}

int WritePacket(shared_ptr<AVPacket> packet) {
	auto inputStream = inputContext->streams[packet->stream_index];
	auto outputStream = outputContext->streams[packet->stream_index];
	av_packet_rescale_ts(packet.get(), inputStream->time_base, outputStream->time_base);
	return av_interleaved_write_frame(outputContext, packet.get());
}

static char *dup_wchar_to_utf8(const wchar_t *w)
{
	char *s = NULL;
	int l = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
	s = (char *)av_malloc(l);
	if (s)
		WideCharToMultiByte(CP_UTF8, 0, w, -1, s, l, 0, 0);
	return s;
}

int InitAudioFilters() {
	char args[512];
	int ret;
	AVFilter *abuffersrc = avfilter_get_by_name("abuffer");
	AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
	AVFilterInOut *outputs = avfilter_inout_alloc();
	AVFilterInOut *inputs = avfilter_inout_alloc();

	auto audioDecoderContext = inputContext->streams[0]->codec;
	if (!audioDecoderContext->channel_layout) {
		audioDecoderContext->channel_layout = av_get_default_channel_layout(audioDecoderContext->channels);
	}
	
	static const enum AVSampleFormat out_sample_fmts[] = { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE };
	static const int64_t out_channel_layouts[] = { audioDecoderContext->channel_layout, -1 };
	static const int out_sample_rate[] = { audioDecoderContext->sample_rate, -1 };

	AVRational time_base = inputContext->streams[0]->time_base;
	filterGraph = avfilter_graph_alloc();
	filterGraph->nb_threads = 1;

	sprintf_s(args, sizeof(args), 
		"time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%I64x",
		time_base.num, time_base.den, audioDecoderContext->sample_rate,
		av_get_sample_fmt_name(audioDecoderContext->sample_fmt), audioDecoderContext->channel_layout);
	ret = avfilter_graph_create_filter(&buffersrcCtx, abuffersrc, "in", args, NULL, filterGraph);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
		return ret;
	}

	/* buffer audio sink: to terminate the filter chain. */
	ret = avfilter_graph_create_filter(&buffersinkCtx, abuffersink, "out", NULL, NULL, filterGraph);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
		char *errStr = new char[1024];
		av_strerror(ret, errStr, 1024);
		cout << "errStr=" << errStr << endl;
		return ret;
	}
	ret = av_opt_set_int_list(buffersinkCtx, "sample_fmts", out_sample_fmts, -1, AV_OPT_SEARCH_CHILDREN);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot set output sample formats\n");
		return ret;
	}
	ret = av_opt_set_int_list(buffersinkCtx, "channel_layouts", out_channel_layouts, -1, AV_OPT_SEARCH_CHILDREN);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
		return ret;
	}
	ret = av_opt_set_int_list(buffersinkCtx, "sample_rates", out_sample_rate, -1, AV_OPT_SEARCH_CHILDREN);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
		return ret;
	}

	/* Endpoints for the filter graph. */
	outputs->name = av_strdup("in");
	outputs->filter_ctx = buffersrcCtx;
	outputs->pad_idx = 0;
	outputs->next = NULL;

	inputs->name = av_strdup("out");
	inputs->filter_ctx = buffersinkCtx;
	inputs->pad_idx = 0;
	inputs->next = NULL;

	if ((ret = avfilter_graph_parse_ptr(filterGraph, "anull", &inputs, &outputs, nullptr)) < 0) {
		return ret;
	}
	if ((ret = avfilter_graph_config(filterGraph, NULL)) < 0) {
		return ret;
	}
	av_buffersink_set_frame_size(buffersinkCtx, 1024);
	return 0;
}

int InitAudioEncoderCodec(AVCodecContext *inputAudioCodec) {
	int ret = 0;
	//获取AAC编码器
	AVCodec * audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	outPutAudioEncContext = avcodec_alloc_context3(audioCodec);
	outPutAudioEncContext->codec = audioCodec;	//设置编码器为AAC
	outPutAudioEncContext->sample_rate = inputAudioCodec->sample_rate;//设置采样率
	outPutAudioEncContext->channel_layout = inputAudioCodec->channel_layout;//设置通道
	outPutAudioEncContext->channels = av_get_channel_layout_nb_channels(inputAudioCodec->channel_layout);

	if (outPutAudioEncContext->channel_layout == 0) {
		//声音通道个数如果为0，设置为单声道
		outPutAudioEncContext->channel_layout = AV_CH_LAYOUT_STEREO;
		outPutAudioEncContext->channels = av_get_channel_layout_nb_channels(outPutAudioEncContext->channel_layout);
	}
	outPutAudioEncContext->sample_fmt = audioCodec->sample_fmts[0];
	outPutAudioEncContext->codec_tag = 0;
	outPutAudioEncContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
	//打开解码器，audioCodec：通过
	ret = avcodec_open2(outPutAudioEncContext, audioCodec, 0);
	return ret; 
}

AVFrame* DecodeAudio(AVPacket *packet, AVFrame *srcAudioFrame) {
	AVStream *stream = inputContext->streams[0];
	AVCodecContext * codecContext = stream->codec;
	int gotFrame;
	AVFrame *filterFrame = nullptr;
	//解码
	auto length = avcodec_decode_audio4(codecContext, srcAudioFrame, &gotFrame, packet);
	if (length >= 0 && gotFrame != 0) {
		if (av_buffersrc_add_frame_flags(buffersrcCtx, srcAudioFrame, AV_BUFFERSRC_FLAG_PUSH) < 0) {
			av_log(NULL, AV_LOG_ERROR, "buffer src add frame error\n");
			return nullptr;
		}
		filterFrame = av_frame_alloc();
		int ret = av_buffersink_get_frame_flags(buffersinkCtx, filterFrame, AV_BUFFERSINK_FLAG_NO_REQUEST);
		if (ret < 0) {
			av_frame_free(&filterFrame);
			if (ret < 0) {
				char *errMsg = new char[1024];
				av_strerror(ret, errMsg, 1024);
				cout << "av_buffersink_get_frame_flags error,msg=" << errMsg << endl;
			}
			goto Error;
		}
		return filterFrame;
	}
Error:
	if (length < 0) {
		char *errMsg = new char[1024];
		av_strerror(length, errMsg, 1024);
		cout << "avcodec_decode_audio4 error,msg=" << errMsg << endl;
	}
	
	return nullptr;
}

int main()
{
	int got_output = 0;

	Init();
	auto pSrcAudioFrame = av_frame_alloc();

	//转字符格式，解决中文路径无法识别的问题
	string fileAudioInput = dup_wchar_to_utf8(L"audio=麦克风阵列 (Realtek High Definition Audio)");
	int ret = OpenInput(fileAudioInput);	
	if (ret < 0) {
		goto Error;
	}
	ret = InitAudioFilters();
	if (ret < 0) goto Error;
	ret = InitAudioEncoderCodec(inputContext->streams[0]->codec);
	if (ret < 0) goto Error;
	ret = OpenOutput("aac.ts");		//打开输出流
	if (ret < 0) goto Error;


	while (true) {
		auto packet = ReadPacketFromSource();

		//解码
		auto filterFrame = DecodeAudio(packet.get(), pSrcAudioFrame);
		if (filterFrame) {
			//定义智能指针
			std::shared_ptr<AVPacket> pkt(static_cast<AVPacket*>(av_malloc(sizeof(AVPacket))),
				[&](AVPacket *p) { av_packet_free(&p), av_freep(&p); });
			av_init_packet(pkt.get());
			pkt->data = NULL;
			pkt->size = 0;
			//编码
			ret = avcodec_encode_audio2(outPutAudioEncContext, pkt.get(), filterFrame, &got_output);
			cout << "got_output=" << got_output << endl;
			if (ret >= 0 && got_output) {
				auto streamTimeBase = outputContext->streams[pkt->stream_index]->time_base.den;
				auto codecTimeBase = outputContext->streams[pkt->stream_index]->codec->time_base.den;
				pkt->pts = pkt->dts = (1024 * streamTimeBase * audioCount) / codecTimeBase;
				audioCount++;
				if (pkt) {
					ret = WritePacket(pkt);
					if (ret >= 0) {
						cout << "WritePacket successs!" << endl;
					}
					else {
						cout << "Write Packet failed!" << endl;
					}
				}
			}

		}
	}
Error:
	CloseInput();
	CloseOutput();
	while (true) {
		this_thread::sleep_for(chrono::seconds(100));
	}
	return 0;
}