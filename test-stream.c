
#include <mpg123.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/soundcard.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <curl/curl.h>
#include <pthread.h>
#include <termios.h>

/**
 * A simple MP3 streamer for demo purposes.
 *
 * This program uses two threads:
 *  - one for retrieving the data over the network (network_proc);
 *  - the other for the actual MP3 decoding (decode_proc).
 *
 * The mpg123 library is used for the audio decoding, libCURL for the
 * network functionality.
 *
 */

#define BLOCK_SIZE 4096

#define handle_error_ret_null(msg) \
    do { perror(msg); return NULL;} while (0)

#define handle_error(msg) \
    do { perror(msg); return 1;} while (0)

mpg123_handle *mh = NULL;
char *audio_dev;
int dsp;
CURL *curl;
int exit_loop = 0;

void mpg123_cleanup(mpg123_handle *mh) {
    mpg123_close(mh);
    mpg123_delete(mh);
    mpg123_exit();
}

size_t feed_data(char *data, size_t size, size_t nmemb, void *ignore) {
    size_t count = size*nmemb;
    int err;

    err = mpg123_feed(mh, data, count);
    if (err == MPG123_ERR) {
        fprintf(stderr, "Could not feed audio data: %s\n", mpg123_plain_strerror(err));
    }

    return count;
}

int on_progress(void *ignore, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    if (exit_loop) {
        return 1;
    }
    return 0;
}

void *network_proc(void *url) {
    size_t read_count, total_count;
    unsigned char in[BLOCK_SIZE];
    int err;
    CURLcode res;

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Could not initialize cURL context. Aborting ...\n");
        return NULL;
    }
    curl_easy_setopt(curl, CURLOPT_URL, (char *)url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, feed_data);

    printf("Trying to stream audio from %s ...\n\n", url);

    res = curl_easy_perform(curl);
    if (res == CURLE_ABORTED_BY_CALLBACK) {
        fprintf(stderr, "Request to abort by user. Exiting ...\n");
    }
    else if (res != CURLE_OK) {
        fprintf(stderr, "cURL failed: %s\n", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);

    return NULL;
}

void *decode_proc(void *arg) {
    unsigned char out[BLOCK_SIZE];
    struct termios orig_term_attr, new_term_attr;
    int ch;
    size_t decoded_count = 0;
    off_t ret;
    int res, pause = 0;
    int dsp_format = AFMT_S16_LE;
    int dsp_channels = 0;
    long dsp_srate = 0;
    int encoding = 0;
    fd_set rset;
    struct timeval tm;

    // Allow user key-press (for play/pause).
    tcgetattr(fileno(stdin), &orig_term_attr);
    memcpy(&new_term_attr, &orig_term_attr, sizeof(struct termios));
    new_term_attr.c_lflag &= ~(ECHO|ICANON);
	new_term_attr.c_cc[VTIME] = 0;
    new_term_attr.c_cc[VMIN] = 0;
    tcsetattr(fileno(stdin), TCSANOW, &new_term_attr);

    do {
        ret = mpg123_read(mh, out, sizeof(out), &decoded_count);
        if (ret == MPG123_ERR) {
            fprintf(stderr, "Decoding stream failed: %s. Aborting ...\n", mpg123_plain_strerror(ret));
            return NULL;
        }
        if (ret == MPG123_NEED_MORE) {
            tm.tv_sec = 0;
            tm.tv_usec = 50000;
            FD_ZERO(&rset);
            FD_SET(0, &rset);
            select(1, &rset, NULL, NULL, &tm);
        }
        // printf("%zd bytes decoded\n", decoded_count);
    } while (ret != MPG123_NEW_FORMAT);

    mpg123_getformat(mh, &dsp_srate, &dsp_channels, &encoding);
    mpg123_format_none(mh);
    mpg123_format(mh, dsp_srate, dsp_channels, encoding);
    printf("Loaded audio stream ... channels = %d, sample-rate = %ld Hz.\n",
            dsp_channels, dsp_srate);

    // We now have the format information. Move on to actual playback.
    // But first we need to configure the audio device with the relevant parameters.
    res = ioctl(dsp, SNDCTL_DSP_SETFMT, &dsp_format);
    if (res) { handle_error_ret_null("ioctl set format"); }
    res = ioctl(dsp, SNDCTL_DSP_CHANNELS, &dsp_channels);
    if (res) { handle_error_ret_null("ioctl set channels"); }
    res = ioctl(dsp, SNDCTL_DSP_SPEED, &dsp_srate);
    if (res) { handle_error_ret_null("ioctl set sample-rate"); }

    do {
        ch = fgetc(stdin);
        switch (ch) {
            case 'p':
                pause = ~pause;
                printf("%s ... \n", pause ? "Pause" : "Resume");
                break;
            case 'q':
                printf("Exiting ... \n");
                exit_loop = 1;
                break;
            default:
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
        ret = mpg123_read(mh, out, sizeof(out), &decoded_count);
        if (ret == MPG123_NEED_MORE) {
            continue;
        }
        write(dsp, out, decoded_count);
    } while (ret != MPG123_ERR && !exit_loop);

    tcsetattr(fileno(stdin), TCSANOW, &orig_term_attr);
    return NULL;
}

int main(int argc, char *argv[]) {
    pthread_t network_t, decode_t;
    FILE *fp;
    int err = MPG123_OK;
    int res;
    size_t buffer_size = 0;
    off_t ret;
    char *url;

    if (argc < 3) {
        fprintf(stderr,
                "MP3 streaming test\n\n"
                "Usage: %s <audio_device> <url>\n"
                "  <audio_device> could be /dev/dsp for example.\n"
                "  <url> should be the full remote URL.\n\n\n",
                argv[0]);
        return 1;
    }

    audio_dev = argv[1];
    dsp = open(audio_dev, O_WRONLY);
    if (dsp == -1) {
        fprintf(stderr, "Could not open audio device. ");
        perror("open");
        return 1;
    }

    err = mpg123_init();
    if (err != MPG123_OK) {
        fprintf(stderr, "Decoder initialization failed: %s\n",  mpg123_plain_strerror(err));
        mpg123_cleanup(mh);
        return 1;
    }
    mh = mpg123_new(NULL, &err);
    if (mh == NULL) {
        fprintf(stderr, "Decoder initialization failed: %s\n",  mpg123_plain_strerror(err));
        mpg123_cleanup(mh);
        return 1;
    }

    url = argv[2];
    err = mpg123_open_feed(mh);
    if (err != MPG123_OK) {
        fprintf(stderr, "Could not initialize bit stream ...\n");
        mpg123_cleanup(mh);
        return 1;
    }

    res = pthread_create(&network_t, NULL, network_proc, url);
    if (res) { handle_error("pthread_create"); }
    res = pthread_create(&decode_t, NULL, decode_proc, NULL);
    if (res) { handle_error("pthread_create"); }

    pthread_join(network_t, NULL);
    pthread_join(decode_t, NULL);

    mpg123_cleanup(mh);
    close(dsp);

    return 0;
}

