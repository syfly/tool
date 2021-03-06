#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <errno.h>

#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000

#define MAX_SAMPLE_RATE     48000
#define DEFAULT_SAMPLE_RATE 44100
#define CAST_SAMPLE_RATE(x) (((x) > MAX_SAMPLE_RATE) ? DEFAULT_SAMPLE_RATE : (x))
#define CAST_CHANNELS(x)    (((x) >= 2) ? 2 : 1)

DECLARE_ALIGNED(16, uint8_t, mTargetAudioBuffer)[AVCODEC_MAX_AUDIO_FRAME_SIZE * 4];

typedef struct AudioPkt {
  int size;
  uint8_t *data;
} AudioPkt;

int audioDecoder(char *url)
{
  av_register_all();
  av_log_set_level(AV_LOG_DEBUG);

  AVFormatContext *mFormatContext;
  mFormatContext = avformat_alloc_context();
  if (avformat_open_input(&mFormatContext, url, NULL, NULL) != 0)
  {
    fprintf(stderr, "avformat open input fail.\n");

    return -1;
  }

  int mTrackCount = 0;
  AVStream *mAudioStream;
  int mAudioStreamIndex = -1;

  AVStream *mVedioStream;
  int mVedioStreamIndex = -1;

  if (avformat_find_stream_info(mFormatContext, NULL) >= 0)
  {
    mTrackCount = mFormatContext->nb_streams;
    if (mTrackCount > 0)
    {
      int i = 0;
      for (i = 0; i < mTrackCount; ++i)
      {
        if (mFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
          mAudioStream = mFormatContext->streams[i];
          mAudioStreamIndex = i;
        }
        else if (mFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
          mVedioStream = mFormatContext->streams[i];
          mVedioStreamIndex = i;
        }
      }
    }
    else
    {
      //WARN("can't find any stream.");
    }
  }

  AVCodecContext *mVideoCodecContext = mVedioStream->codec;
  AVFrame *mVedioFrame = av_frame_alloc();
  AVCodec *videoCodec = avcodec_find_decoder(mVideoCodecContext->codec_id);
  if (videoCodec != NULL && avcodec_open2(mVideoCodecContext, videoCodec, NULL) != 0)
  {
    fprintf(stderr, "avcodec video open fail.\n");
  }

  SwrContext *mSwrContext;
  AVFrame *mFrame;
  int mSampleBytes;
  double mAudioSampleRate;
  double mAudioTimeBase;

  mFrame = av_frame_alloc();//avcodec_alloc_frame();

  AVCodecContext *mCodecContext = mAudioStream->codec;
  mCodecContext->debug |= FF_DEBUG_STARTCODE;

  AVCodec *codec = avcodec_find_decoder(mCodecContext->codec_id);
  if (codec != NULL && avcodec_open2(mCodecContext, codec, NULL) != 0)
  {
    fprintf(stderr, "avcodec open fail.\n");
  }

  uint64_t outChannelLayout = mAudioStream->codec->channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
  mSampleBytes = av_get_channel_layout_nb_channels(outChannelLayout) * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

  int channels = mCodecContext->channels;
  uint64_t channelLayout = mCodecContext->channel_layout;
  if (channelLayout == 0 || av_get_channel_layout_nb_channels(channelLayout) != channels)
  {
    channelLayout = av_get_default_channel_layout(channels);
  }

  mSwrContext = swr_alloc_set_opts(NULL,
                                   outChannelLayout,
                                   AV_SAMPLE_FMT_S16,
                                   CAST_SAMPLE_RATE(mCodecContext->sample_rate),
                                   channelLayout,
                                   mCodecContext->sample_fmt,
                                   mCodecContext->sample_rate,
                                   0,
                                   NULL);
  if (mSwrContext != NULL)
  {
    if (swr_init(mSwrContext) >= 0)
    {
      mAudioSampleRate = 1.0 / mAudioStream->codec->sample_rate;
      mAudioTimeBase = av_q2d(mAudioStream->time_base);
    }

    AVPacket avpacket;
    av_init_packet( &avpacket );
    AVPacket *pkt = &avpacket;

    uint8_t audioBuf[4096] = {0};
    uint8_t suffixBuf[] = {0x00, 0x00, 0x012, 0xa01, 0x9e,0x63,0x6a,0x45,0x7f,0x5f,0x10,0xdb,0x9c,0x10,0x33,
      0x33, 0x62,0xb0,0x9e,0x51,0xc6,0xcd,0x4b,0xe4};//0xfddb53c9809ebfc1216b2678ed28778a
   // 5dce4d823c85d33fdf900d3e23765a910f5dc95206e3e9065620ac1d253f0a8d219b061ff21634c0176c1e7e5e7ef25f8f8266
    // e6cf26114fa9b9100f3b43a201a12962cf9f0f943740f96f058f978029e09b91df9974c1a9c429ef5dc7efe1ea5a69221829a2
    // 60fa0a1e5cce75d2514bfcdd042e579d7fb0682e43b62738fc358926355f08866b64075e109f1152997d569ed6ff01c21709d7
    // 10e9dd10c9f0cc3cba3074417a63743b8c192890fe0c80f90bea25fe9af46c57f65279807cc62d88f7e6af14ecfac690360635
    // d379368d4378850a381f7b2f3d2986b935c484afa7ecbc061d938a0d61bd34e966f713deeeb52aaefddda187dc1fded56d7af4f099239763e1};
    int bufIndex = 0;

    int copyCount = 0;
    while (av_read_frame(mFormatContext, pkt) >= 0)
    {
      fprintf(stdout, "av_read_frame avpacket AVBufferRef *buf=%p pts=%lld dts=%lld size=%d flags=%d, \n\
          side_data=%p side_data_elems=%d pos=%lld convergence_duration=%lld index=%d mAudioStreamIndex=%d mVedioStreamIndex=%d\n",
       pkt->buf, pkt->pts, pkt->dts, pkt->size, pkt->flags, pkt->side_data, pkt->side_data_elems, pkt->pos,
      pkt->convergence_duration, pkt->stream_index, mAudioStreamIndex, mVedioStreamIndex);
      if (pkt->stream_index == mAudioStreamIndex)
      {
        if (copyCount < 2)
        {
          fprintf(stdout, "copy audio buffer size=%d\n", pkt->size);

          memcpy(audioBuf + bufIndex, pkt->data, pkt->size);
          bufIndex += pkt->size;
          copyCount++;
        }
        else
        {
          fprintf(stdout, "decode audio buffer buf size=%d\n", bufIndex);
            memcpy(audioBuf + bufIndex, suffixBuf, sizeof(suffixBuf));
            bufIndex += sizeof(suffixBuf);
            fprintf(stdout, "decode audio buffer buf size=%d\n", bufIndex);
            decodeAudioPkt(mCodecContext, mSwrContext, mFrame, mAudioStreamIndex, mSampleBytes, audioBuf, bufIndex);
            bufIndex = 0;
            copyCount = 0;
        }
      }
      else if (pkt->stream_index == mVedioStreamIndex)
      {
        // int got_frame = -1;
        // int ret = avcodec_decode_video2(mVideoCodecContext, mVedioFrame, &got_frame, &pkt);
        // fprintf(stdout, "avcodec_decode_video2 ret=%d got_frame=%d\n", ret, got_frame);
      }
    }

      av_free_packet(pkt);
  }


  swr_free(&mSwrContext);
  av_free(mFrame);
  avcodec_close(mCodecContext);
  avformat_close_input(&mFormatContext);

  return 0;
}

void decodeAudioPkt(AVCodecContext *mCodecContext, SwrContext *mSwrContext, AVFrame *mFrame,
  int mAudioStreamIndex, int mSampleBytes, uint8_t *buf, int bufSize)
{
  AVPacket avpacket;
  av_init_packet( &avpacket );

  avpacket.size = bufSize;
  avpacket.data = buf;

  int gotFrame = 0;
  dumpMem(avpacket.data, avpacket.size);
  //avcodec_get_frame_defaults(mFrame);
  AVPacket tmpPkt = avpacket;
  int size;
  while (tmpPkt.size > 0)
  {
    size = avcodec_decode_audio4(mCodecContext, mFrame, &gotFrame, &tmpPkt);

    fprintf(stdout, "avcodec decode audio size=%d gotFrame=%d\n", size, gotFrame);

    if (size > 0)
    {
      tmpPkt.size -= size;
      tmpPkt.data += size;

      if (gotFrame)
      {
        int pts = av_frame_get_best_effort_timestamp(mFrame);
        if (pts != -1)
        {
          fprintf(stdout, "frame completed. pts=%d", pts);

          uint8_t *outBuffer[] = { (uint8_t *) mTargetAudioBuffer };
          int outCount = sizeof (mTargetAudioBuffer) / mSampleBytes;

          const uint8_t **inBuffer = (const uint8_t **) mFrame->extended_data;
          int inCount = mFrame->nb_samples;

          int convRet = swr_convert(mSwrContext, outBuffer, outCount, inBuffer, inCount);

          fprintf(stdout, "swr_convert outCount=%d inCount=%d convRet=%d \n", outCount, inCount, convRet);
          //render(mTargetAudioBuffer, convRet * mSampleBytes, pts);
        }
        else
        {
          //WARN("invalid pts. ignore it.");
        }
      }
    }
    else
    {
      //WARN("packet error. skip it.");

      /* if error, we skip the frame */
      tmpPkt.size = 0;
    }
  }
}

void dumpMem(uint8_t *ptr, int size)
{
  char buf[4097] = {0};
  int printLen = 4096 > size * 2 ? size * 2 : 4096;
  int index = 0;
  char *tmp = buf;
  while (index < printLen)
  {
    sprintf(tmp + index, "%02x", *(ptr++));
    index += 2;
  }

  fprintf(stdout, "dumpMem %s\n", buf);
}

int isKeyVideoFrame(AVCodecContext *pCodecCtx, AVPacket *pkt)
{
  if (pCodecCtx->codec_type != AVMEDIA_TYPE_VIDEO)
    {
        return -1;
    }

    if (pkt->flags & AV_PKT_FLAG_KEY)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

void saveFrame(const AVFrame *pFrame, int width, int height, int iFrame);
int videoDecoder()
{
  avcodec_register_all();
  av_register_all();

  av_log_set_level(AV_LOG_DEBUG);

  AVFormatContext *pFormatCtx = avformat_alloc_context();
  AVCodecContext *pCodecCtx;
  AVCodec *pCodec;
  AVInputFormat *fileFormat;
  int ret = -1;
  int videoStream = -1;

  //"/media/xhms/测试播放文件/flv/4002_G(Flash Video)_V(Baseline@L1.3,LD)_A(LC).flv"
  if ((ret = avformat_open_input(&pFormatCtx, "http://hls.yy.com/80266156_2564927082_15012_1636380711.flv?kt=1", NULL, NULL)) != 0)
  {
      fprintf(stderr, "av_open_input_file \n");

      return -1;
  }


  fprintf(stdout, "pFormatCtx %p\n", pFormatCtx);

  if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
  {
      fprintf(stderr, "av_find_stream_info \n");

      return -1;
  }

  //av_dump_format(pFormatCtx, 0, "http://hls.yy.com/80266156_2564927082_15012_1636380711.flv?kt=1", 0);


  int i;
  for (i = 0; i < pFormatCtx->nb_streams; ++i)
  {
      if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
      {
          videoStream = i;

          break ;
      }
  }

  if (videoStream == -1)
  {
      fprintf(stderr, "Unsupported videoStream.\n");

      return -1;
  }

  fprintf(stdout, "videoStream: %d\n", videoStream);

  pCodecCtx = pFormatCtx->streams[videoStream]->codec;
  //pCodecCtx->debug = 1;
  pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
  if (pCodec == NULL)
  {
      fprintf(stderr, "Unsupported codec.\n");

      return -1;
  }

  
  //pCodecCtx->sub_charenc = charenc;//"UTF-16LE";
  fprintf(stdout, "open codec\n");
  
  if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
  {
      fprintf(stderr, "Could not open codec.\n");

      return -1;
  }

  ret = avformat_seek_file(pFormatCtx, -1, INT64_MIN, 100000, INT64_MAX, 0);

  fprintf(stdout, "aaa ret=%d\n", ret);

  int index;
    
  AVPacket packet;
  av_init_packet(&packet);

  AVPacket *savePkt = NULL;
  AVPacket *decodePkt = NULL;
  AVFrame *pFrame = av_frame_alloc();
  int got_picture = 0;
  int keyframeindex  = 0;
  for (index = 0; index < 10; )
  {

      //memset(&packet, 0, sizeof (packet));
      ret = av_read_frame(pFormatCtx, &packet);
      if (ret < 0)
      {
          fprintf(stderr, "read frame ret:%d\n", ret);

          break ;
      }

      AVPacket *pkt = &packet;
      fprintf(stdout, "packet.stream_index == videoStream:%d\n\n\n\n", packet.stream_index == videoStream);

      if (packet.stream_index == videoStream)
      {
          // if (isKeyVideoFrame(pCodecCtx, &packet) == 0 && keyframeindex++ == 3 && savePkt == NULL )
          // {
          //   savePkt = av_packet_clone(&packet);
          //   fprintf(stdout, "packet is keyframe. avpacket AVBufferRef *buf=%p pts=%lld dts=%lld data=%p size=%d flags=%d, \n\
          //     side_data=%p side_data_elems=%d pos=%lld convergence_duration=%lld index=%d videoStream=%d\n",
          //     savePkt->buf, savePkt->pts, savePkt->dts, savePkt->data, savePkt->size, savePkt->flags, savePkt->side_data, savePkt->side_data_elems, savePkt->pos,
          //     savePkt->convergence_duration, savePkt->stream_index, videoStream);
          // }
          
          // if (savePkt != NULL)
          // {
          //   decodePkt = av_packet_clone(savePkt);
          //   decodePkt->pts = packet.pts;
          //   decodePkt->dts = packet.dts;
          // }

          if (decodePkt == NULL)
          {
            fprintf(stdout, "decodePkt is NULL.\n");
            decodePkt = &packet;
          }
          else
          {
            av_free_packet(&packet);
          }

          fprintf(stdout, "keyframeindex=%d av_read_frame avpacket AVBufferRef *buf=%p pts=%lld dts=%lld data=%p size=%d flags=%d, \n\
          side_data=%p side_data_elems=%d pos=%lld convergence_duration=%lld index=%d videoStream=%d\n", keyframeindex,
            decodePkt->buf, decodePkt->pts, decodePkt->dts, decodePkt->data, decodePkt->size, decodePkt->flags, decodePkt->side_data, decodePkt->side_data_elems, decodePkt->pos,
          decodePkt->convergence_duration, decodePkt->stream_index, videoStream);

          ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, decodePkt);

          fprintf(stdout, "got_picture:%d savePkt=%p\n", got_picture, savePkt);
          if (got_picture && savePkt != NULL)
          {
            saveFrame(pFrame, pCodecCtx->width, pCodecCtx->height, index);

            index++;

          }

          av_free_packet(decodePkt);
          decodePkt = NULL;

          
      }
      
  }

  fprintf(stdout, "index=%d\n", index);

  return 0;
}

void saveFrame(const AVFrame *pFrame, int width, int height, int iFrame)
{
    FILE *pFile;
    char szFileName[32];

    sprintf(szFileName, "saveframe/frame%d.ppm", iFrame);
    pFile = fopen(szFileName, "wb");
    if (pFile == NULL)
    {
        fprintf(stderr, "pFile NULL szFileName=%s %s\n", szFileName, strerror(errno));

        return ;
    }
    fprintf(stdout, "pFrame:%0X, szFileName:%s\n", (int)pFrame, szFileName);

    fprintf(pFile, "P6\n%d %d\n255\n", width, height);
    int i;
    for (i = 0; i < height; ++i)
    {
        fwrite(pFrame->data[0] + i * pFrame->linesize[0], 1, width * 3, pFile);
    }

    fclose(pFile);
}

int audiodecodedemo(int argc, char const *argv[])
{
  /* code */
  //audioDecoder("output.mp4");
  videoDecoder();
  return 0;
}


// if (m_pAVFmtCtxt->streams[pPkt->stream_index]->codec->codec_type != AVMEDIA_TYPE_VIDEO)
//     {
//         elmType = ENUM_AV_BUFFMQ_ELM_TYPE_AUDIO;
//         return false;
//     }

//     if (pPkt->flags & AV_PKT_FLAG_KEY)
//     {
//         elmType = ENUM_AV_BUFFMQ_ELM_TYPE_VIDEO_KEY_FRAME;
//     }
//     else
//     {
//         elmType = ENUM_AV_BUFFMQ_ELM_TYPE_VIDEO_NON_KEY_FRAME;
//     }