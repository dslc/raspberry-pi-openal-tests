/*
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/soundcard.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <termios.h>
#ifdef HAVE_AV_CONFIG_H
#undef HAVE_AV_CONFIG_H
#endif
#include "libavcodec/avcodec.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/samplefmt.h"
#include <curl/curl.h>

#define AUDIO_BUF_SIZE 2500000
#define AUDIO_TEMP_SIZE 20480
#define AUDIO_REFILL_THRESH 4096
#define MAX_GAP 1000000

#define handle_error(msg) \
    do { perror(msg); return 1; } while (0)

#define FLAG_PAUSED 0x01

typedef struct {
    int flags;
    uint8_t buf[AUDIO_BUF_SIZE];
    int rd_ptr, wr_ptr;
    int written, read;
    int download_paused;
} player_ctx_t;

pthread_mutex_t buf_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_stream = PTHREAD_COND_INITIALIZER;
int done;
AVCodec *codec;
AVCodecContext *c= NULL;
AVPacket avpkt;
int dsp;
player_ctx_t player;
CURL *curl;

void *monitor_stream(void *);

void init_player(void) {
    player.flags = 0;
    player.rd_ptr = 0;
    player.wr_ptr = 0;
    player.written = 0;
    player.read = 0;
    memset(player.buf, 0, sizeof(player.buf));
}

size_t buffer_available(void) {
    if (player.wr_ptr > player.rd_ptr) {
        return player.wr_ptr - player.rd_ptr;
    }
    else if (player.wr_ptr < player.rd_ptr) {
        return player.wr_ptr + AUDIO_BUF_SIZE - player.rd_ptr;
    }
    else {
        return 0;
    }
}

size_t read_buffer(char *data, size_t count) {
    size_t count_end, gap, remaining;

    if (player.wr_ptr > player.rd_ptr) {
        gap = player.wr_ptr - player.rd_ptr;
        count = (count > gap) ? gap : count;
        memcpy(data, player.buf+player.rd_ptr, count);
        player.rd_ptr += count;
    }
    else if (player.wr_ptr == player.rd_ptr) {
        count = 0;
    }
    else  {
        count_end = AUDIO_BUF_SIZE - player.rd_ptr;
        count_end = count_end > count ? count : count_end;
        remaining = count - count_end;
        remaining = remaining > player.wr_ptr ? player.wr_ptr : remaining;
        memcpy(data, player.buf+player.rd_ptr, count_end);
        if (remaining > 0) {
            memcpy(data+count_end, player.buf, remaining);
            player.rd_ptr = remaining;
        }
        else {
            player.rd_ptr += count_end;
        }
        count = count_end + remaining;
    }
    player.read += count;
    return count;
}

int progress_callback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
    if (player.flags & FLAG_PAUSED && player.wr_ptr - player.rd_ptr < MAX_GAP) {
        // printf("Written: %d, read: %d - Resuming download ...\n", player.written, player.read);
        curl_easy_pause(curl, CURLPAUSE_CONT);
        player.flags &= ~FLAG_PAUSED;
    }
    return 0;
}

size_t write_buffer(char *data, size_t size, size_t nmemb, void *userdata) {
    size_t count = size*nmemb;
    int i;

    if (player.written - player.read > MAX_GAP) {
        // printf("Written: %d, read: %d - Pausing download ...\n", player.written, player.read);
        player.flags |= FLAG_PAUSED;
        return CURL_WRITEFUNC_PAUSE;
    }

    pthread_mutex_lock(&buf_mutex);
    for (i=0; i<count; i++) {
        player.buf[player.wr_ptr] = *(data+i);
        player.wr_ptr++;
        if (player.wr_ptr == AUDIO_BUF_SIZE) {
            player.wr_ptr = 0;
        }
    }
    player.written += count;
    pthread_mutex_unlock(&buf_mutex);
    //printf("%zd bytes written\n", count);

    return count;
}

static void start_stream(char *url)
{
    CURLcode res;
    pthread_t monitor_t;
    int s;

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Could not initialize cURL context. Aborting ...\n");
        return;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_buffer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
    curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_callback);

	av_init_packet(&avpkt);
	printf("Beginning stream of URL %s ... \n", url);
	/* find the mpeg audio decoder */
	codec = avcodec_find_decoder(AV_CODEC_ID_MP2);
	if (!codec) {
		fprintf(stderr, "codec not found\n");
		return;
	}
	c = avcodec_alloc_context3(codec);
	/* open it */
	if (avcodec_open2(c, codec, NULL) < 0) {
		fprintf(stderr, "could not open codec\n");
		return;
	}
    c->sample_fmt = AV_SAMPLE_FMT_S16;
    c->channel_layout = AV_CH_LAYOUT_STEREO;
	c->channels       = av_get_channel_layout_nb_channels(c->channel_layout);
    printf("AVCodecContext - channels: %d, format: %d\n", c->channels, c->sample_fmt);

    s = pthread_create(&monitor_t, NULL, monitor_stream, NULL);
    if (s) { handle_error("pthread_create"); }

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "cURL failed: %s\n", curl_easy_strerror(res));
        return;
    }
    done = 1;
	avcodec_close(c);
	av_free(c);
    curl_easy_cleanup(curl);
    pthread_join(monitor_t, NULL);
}

void *monitor_stream(void *arg) {
    size_t available;
    struct termios orig_term_attr, new_term_attr;
    char inbuf[AUDIO_TEMP_SIZE  + FF_INPUT_BUFFER_PADDING_SIZE];
    int ch, pause=0;
    struct timeval tm;
    fd_set rset;
    AVFrame *decoded_frame = NULL;
    int len;

    // Allow user key-press (for play/pause).
    tcgetattr(fileno(stdin), &orig_term_attr);
    new_term_attr.c_lflag &= ~(ECHO|ICANON);
	new_term_attr.c_cc[VTIME] = 0;
    new_term_attr.c_cc[VMIN] = 0;
    tcsetattr(fileno(stdin), TCSANOW, &new_term_attr);

    while (avpkt.size == 0) {
        FD_ZERO(&rset);
        FD_SET(0, &rset);
        tm.tv_sec=0;
        tm.tv_usec=200000;
        select(1, &rset, NULL, NULL, &tm);
        pthread_mutex_lock(&buf_mutex);
        avpkt.data = inbuf;
        avpkt.size = read_buffer(inbuf, AUDIO_TEMP_SIZE);
        pthread_mutex_unlock(&buf_mutex);
        // printf("%zd bytes read ...\n", avpkt.size);
    }

	while (avpkt.size > 0) {
        ch = fgetc(stdin);
        if (ch == 'p') {
            pause = ~pause;
            printf("%s ... ", pause ? "Pause" : "Resume");
        }
        else if (ch == 'q') {
            break;
        }
        if (pause) {
            FD_ZERO(&rset);
            FD_SET(0, &rset);
            tm.tv_sec=0;
            tm.tv_usec=50000;
            select(1, &rset, NULL, NULL, &tm);
            continue;
        }
        int got_frame = 0;
		if (!decoded_frame) {
			if (!(decoded_frame = av_frame_alloc())) {
				fprintf(stderr, "out of memory\n");
			}
		}
		len = avcodec_decode_audio4(c, decoded_frame, &got_frame, &avpkt);
		if (len < 0) {
			fprintf(stderr, "Error while decoding\n");
            return;
		}
		if (got_frame) {
			/* if a frame has been decoded, output it */
			int data_size = av_samples_get_buffer_size(NULL, c->channels,
					decoded_frame->nb_samples,
					c->sample_fmt, 1);
            write(dsp, decoded_frame->data[0], data_size);
		}
		avpkt.size -= len;
		avpkt.data += len;
		if (avpkt.size < AUDIO_REFILL_THRESH) {
			/* Refill the input buffer, to avoid trying to decode
			 * incomplete frames. Instead of this, one could also use
			 * a parser, or use a proper container format through
			 * libavformat. */
			memmove(inbuf, avpkt.data, avpkt.size);
			avpkt.data = inbuf;
            pthread_mutex_lock(&buf_mutex);
			len = read_buffer(avpkt.data + avpkt.size, AUDIO_TEMP_SIZE - avpkt.size);
            // printf("%zd bytes read\n", len);
            pthread_mutex_unlock(&buf_mutex);
			if (len > 0)
				avpkt.size += len;
		}
    }

    printf("Quitting playback ...\n");

	av_frame_free(&decoded_frame);
    tcsetattr(fileno(stdin), TCSANOW, &orig_term_attr);
    return NULL;
}

int main(int argc, char **argv)
{
	const char *url;
    int s;
    char *audio_dev;
    int format = AFMT_S16_LE, channels = 2, sRate = 48000;

    audio_dev = argv[1];

    dsp = open(audio_dev, O_WRONLY);
    if (dsp == -1) {
        perror("open");
        return 1;
    }

    s = ioctl(dsp, SNDCTL_DSP_SETFMT, &format);
    if (s) { handle_error("ioctl"); }
    s = ioctl(dsp, SNDCTL_DSP_CHANNELS, &channels);
    if (s) { handle_error("ioctl"); }
    s = ioctl(dsp, SNDCTL_DSP_SPEED, &sRate);
    if (s) { handle_error("ioctl"); }

    init_player();

	/* register all the codecs */
	avcodec_register_all();
	url = argv[2];
    done = 0;

    start_stream(url);

    close(dsp);

	return 0;
}

