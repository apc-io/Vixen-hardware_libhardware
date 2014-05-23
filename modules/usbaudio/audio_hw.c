/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "usb_audio_hw"
/*#define LOG_NDEBUG 0*/

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <tinyalsa/asoundlib.h>

#include <audio_utils/resampler.h> //add 

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <cutils/properties.h>

//#define ARRAY_SIZE(s) (sizeof(s)/sizeof((s)[0]))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
/* number of frames per short period (low latency) */
#define SHORT_PERIOD_SIZE  1024 //1024 modify 2013-11-14
/* number of frames per long period (low power) */
#define LONG_PERIOD_SIZE 1024

/* number of periods for low power playback */
#define PLAYBACK_LONG_PERIOD_COUNT 2
/* number of pseudo periods for low latency playback */
#define PLAYBACK_SHORT_PERIOD_COUNT 2
/* number of periods for capture */
#define CAPTURE_PERIOD_COUNT 2

/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 5000

#define RESAMPLER_BUFFER_FRAMES (SHORT_PERIOD_SIZE * 2)
#define RESAMPLER_BUFFER_SIZE (4 * RESAMPLER_BUFFER_FRAMES/*1024*2*/)

//add 2013-11-13

struct pcm_config pcm_config = {  
    .channels = 1, //2 stereo ok! //default 2
    .rate = 16000, //44100 32000 //44100 -->32000 ok! 441000 --->16000 ok!great!
    .period_size = 1024,  //1024
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
};

struct audio_device {
    struct audio_hw_device hw_device;

    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    int card;
    int device;
    bool standby;
};

struct stream_out {
    struct audio_stream_out stream;

    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    struct pcm *pcm;
    bool standby;
    
	 struct resampler_itfe *resampler;
	 char *buffer; //add 2013-11-13
	 
    struct audio_device *dev;
};

/**
 * NOTE: when multiple mutexes have to be acquired, always respect the
 * following order: hw device > out stream
 */

/* Helper functions */

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct stream_out *out)
{
	ALOGD("<<<<%s", __func__);
    struct audio_device *adev = out->dev;
    int i;

    if ((adev->card < 0) || (adev->device < 0))
        return -EINVAL;

    out->pcm = pcm_open(adev->card, adev->device, PCM_OUT, &pcm_config);

    if (out->pcm && !pcm_is_ready(out->pcm)) {
        ALOGE("pcm_open() failed: %s", pcm_get_error(out->pcm));
        pcm_close(out->pcm);
        return -ENOMEM;
    }
    
    out->resampler->reset(out->resampler); //add  2013-11-13

    return 0;
}

/* API functions */

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
	return 44100;
	//return 32000; 
	//return pcm_config.rate; //32000;//44100;
    //return pcm_config.rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    return pcm_config.period_size *
           audio_stream_frame_size((struct audio_stream *)stream);
}

static uint32_t out_get_channels(const struct audio_stream *stream)
{
	return AUDIO_CHANNEL_OUT_STEREO; //tell audioflinger stereo, well hw mono 2013-11-15
	if (pcm_config.channels == 2)
		return AUDIO_CHANNEL_OUT_STEREO;
	else
		return AUDIO_CHANNEL_OUT_MONO;
  //   return  pcm_config.channels; //this will make duplicat out fail to open !!! 
    //AUDIO_CHANNEL_OUT_STEREO;    
    //return AUDIO_CHANNEL_OUT_MONO; //add 2013-11-14
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);

    if (!out->standby) {
        pcm_close(out->pcm);
        out->pcm = NULL;
        out->standby = true;
    }

    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);

    return 0;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    struct str_parms *parms;
    char value[32];
    int ret;
    int routing = 0;

    parms = str_parms_create_str(kvpairs);
    pthread_mutex_lock(&adev->lock);

    ret = str_parms_get_str(parms, "card", value, sizeof(value));
    if (ret >= 0)
        adev->card = atoi(value);

    ret = str_parms_get_str(parms, "device", value, sizeof(value));
    if (ret >= 0)
        adev->device = atoi(value);

    pthread_mutex_unlock(&adev->lock);
    str_parms_destroy(parms);

    return 0;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    return (pcm_config.period_size * pcm_config.period_count * 1000) /
            out_get_sample_rate(&stream->common);
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    return -ENOSYS;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
	//ALOGD("<<<<%s", __func__);
    int ret;
    struct stream_out *out = (struct stream_out *)stream;
	
	void *buf = NULL;
	size_t frame_size = audio_stream_frame_size(&out->stream.common);//ok? channel*format
	size_t in_frames = bytes / frame_size;// number of frame
	size_t out_frames = RESAMPLER_BUFFER_SIZE/*8k?*/ / frame_size;
	bool force_input_standby = false;
	
	int16_t *channel_buf = NULL;//add 2013-11-15
	int16_t *tmp_buf = NULL;
	int ci = 0;
	int count = 0;
	
    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        ret = start_output_stream(out);
        if (ret != 0) {
            goto err;
        }
        out->standby = false;
    }
    
    
    /* only use resampler if required */
    if (pcm_config.rate!= 44100) {
        out->resampler->resample_from_input(out->resampler,
                                            (int16_t *)buffer,
                                            &in_frames,
                                            (int16_t *)out->buffer,
                                            &out_frames);
        buf = out->buffer;
    } else {
        out_frames = in_frames;
        buf = (void *)buffer;
    } //add 2013-11-13
	
	//add for test channle data 2013-11-15
    if (pcm_config.channels == 1) {
	//ALOGD("<<< mono channel");
	channel_buf = (int16_t *)buf;
	tmp_buf = (int16_t *)buf;
	count = out_frames*frame_size/(sizeof(int16_t));
	for (ci=0; ci<count; ci++){
		//ALOGD("0x%x  ", channel_buf[ci]);
		channel_buf[ci] = tmp_buf[2*ci];
	}
	pcm_write(out->pcm, (void *)channel_buf, out_frames * frame_size /2); //add 2013-11-14	
    }	
	//**************************************
   // pcm_write(out->pcm, (void *)buffer, bytes);
   else { 
	//ALOGD("<<< stereo channel");
	pcm_write(out->pcm, (void *)buf, out_frames * frame_size); //add 2013-11-14
   }	

    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);

    return bytes;

err:
    pthread_mutex_unlock(&out->lock);

    if (ret != 0) {
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
               out_get_sample_rate(&stream->common));
    }

    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    return -EINVAL;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out)
{
	ALOGD("<<<<%s just in sr %d, channel 0x%x", __func__, config->sample_rate, config->channel_mask);
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_out *out;
    int ret;
	
	
	
    out = (struct stream_out *)calloc(1, sizeof(struct stream_out));
    if (!out)
        return -ENOMEM;
	
	//add 2013-11-15 for detect usb audio,hw param, sample rate&channel
	int channel_fd = -1;
    //#define STREAM0 "/proc/asound/card1/stream0"
	//char STREAM0[] = "/proc/asound/card1/stream0";
	char *STREAM0 = "/proc/asound/card1/stream0";
	char rd_buf[1024] = {0};
	int rd_len = 0;
	char *tmp_p = NULL;
	int usb_channel = 2;
	
	int card = 1, device = 0; //!!!!!
	int sample_rates[] = {44100, 48000, 32000, 22050, 16000, 11025, 8000 };
	int i = 0;
	
	char buf[PROPERTY_VALUE_MAX] = {0};
	property_get("wmt.usb.audio.card", buf, "1");
	ALOGD("<<<<%s prop %c", __func__, buf[0]);
	if (strcmp(buf, "1") == 0)
	{
		STREAM0 = "/proc/asound/card1/stream0";
		card = 1;
	}
	
	if (strcmp(buf, "2") == 0)
	{
		STREAM0 = "/proc/asound/card2/stream0";
		card = 2;
	}
	ALOGD(" %s --- stream0 %s", __func__, STREAM0);
	
	if (1) {
			
			channel_fd = open(STREAM0, O_RDONLY);
			if (channel_fd < 0)
			{
				ALOGW(" open %s error :%s", STREAM0, strerror(errno));
				close(channel_fd);
				
				goto CHA_END;
			}
			rd_len = read(channel_fd, rd_buf, sizeof(rd_buf));
			if (rd_len < 0)
			{
				ALOGW("read %s error: %s", STREAM0, strerror(errno));
				close(channel_fd);
				
				goto CHA_END;
			}
			ALOGD("read content :%s", rd_buf);
			tmp_p = strstr(rd_buf, "Playback:");
			if (!tmp_p)
			{
				ALOGW("can't find Playback: string");
				goto CHA_END;
				close(channel_fd);
				
			}
			ALOGD("Playback string: %s", tmp_p);
	
			tmp_p = strstr(tmp_p, "Channels:");
	
			if (!tmp_p)
			{
				ALOGW("can't find Channels: string");
				close(channel_fd);
				goto CHA_END;
				
			}
			ALOGD("channel string: %s", tmp_p);
			sscanf(tmp_p, "Channels: %d", &usb_channel);
			ALOGD("<<<<<<<<<< usb channel %d", usb_channel);
			pcm_config.channels = usb_channel;			
			close(channel_fd);
			
		}
	CHA_END :
		//try a proper sample rate
	for (i = 0; i < ARRAY_SIZE(sample_rates); i++) {
		pcm_config.rate = sample_rates[i];
		//ALOGD("sample array %d", ARRAY_SIZE(sample_rates));
		
		out->pcm = pcm_open(card, device, PCM_OUT, &pcm_config);
             if (!pcm_is_ready(out->pcm)) {
                 ALOGW("cannot open pcm_in driver: %s; samplerate: %d", pcm_get_error(out->pcm), sample_rates[i]);
                 pcm_close(out->pcm);
               
                 continue;
             }
             else {
		 ALOGD("%s find proper sample rate %d", __func__, pcm_config.rate);
		 pcm_close(out->pcm);
                 break;
             }
        }
	
	
	
	if (i == ARRAY_SIZE(sample_rates))
	{
		ALOGE("%s can't find proper sample rate!", __func__);
	}
	//add end 2013-11-15
	
	 ret = create_resampler(/*DEFAULT_OUT_SAMPLING_RATE*/44100,
                           /*FULL_POWER_SAMPLING_RATE*/pcm_config.rate, 
                           2, /*channel*/
                           RESAMPLER_QUALITY_DEFAULT,
                           NULL,
                           &out->resampler); //add 2013-11-13
	 out->buffer = malloc(RESAMPLER_BUFFER_SIZE); /* todo: allow for reallocing */ //add 2013-11-15
	 
    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;

    out->dev = adev;

    config->format = out_get_format(&out->stream.common);
    config->channel_mask = out_get_channels(&out->stream.common);
    config->sample_rate = out_get_sample_rate(&out->stream.common);

    out->standby = true;

    adev->card = -1;
    adev->device = -1;
	
	ALOGD("<<<<%s just out sr %d, pcmconfig sr %d, channel 0x%x", __func__, config->sample_rate, pcm_config.rate, config->channel_mask);
	
    *stream_out = &out->stream;
    return 0;

err_open:
    free(out);
    *stream_out = NULL;
    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
	ALOGD("<<<<%s", __func__);
    out_standby(&stream->common);
    free(stream);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    return 0;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    return -ENOSYS;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    return -ENOSYS;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    return -ENOSYS;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    return 0;
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in)
{
    return -ENOSYS;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                   struct audio_stream_in *stream)
{
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct audio_device *adev = (struct audio_device *)device;

    free(device);
    return 0;
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    struct audio_device *adev;
    int ret;
	ALOGD("<<<<%s", __func__);
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    adev = calloc(1, sizeof(struct audio_device));
    if (!adev)
        return -ENOMEM;

    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->hw_device.common.module = (struct hw_module_t *) module;
    adev->hw_device.common.close = adev_close;

    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream = adev_open_output_stream;
    adev->hw_device.close_output_stream = adev_close_output_stream;
    adev->hw_device.open_input_stream = adev_open_input_stream;
    adev->hw_device.close_input_stream = adev_close_input_stream;
    adev->hw_device.dump = adev_dump;

    *device = &adev->hw_device.common;

    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "USB audio HW HAL",
        .author = "The Android Open Source Project",
        .methods = &hal_module_methods,
    },
};
