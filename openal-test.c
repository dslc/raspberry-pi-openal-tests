#include <mpg123.h>
#include <AL/al.h>
#include <AL/alc.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <fcntl.h>
#include <curl/curl.h>

#define BLOCK_SIZE 2048
#define N_BUFFERS 5

#define handle_error(msg) \
    do { perror(msg); return 1;} while (0)

typedef enum {
    ST_WAIT_FORMAT_KNOWN,
    ST_WAIT_PLAY,
    ST_PLAYING,
    ST_PAUSED,
} player_state_t;

#define FLAG_FINISHED 0x01

typedef struct {
    unsigned char flags;
    mpg123_handle *mh;
} feeder_ctx_t;

typedef struct {
    player_state_t state;
    int next_buf;
    ALsizei sample_rate;
    ALCdevice *device;
    ALCcontext *ctx;
    ALenum format;
    ALuint source;
    ALuint buffers[N_BUFFERS];
    mpg123_handle *mh;
} player_ctx_t;

static void mpg123_cleanup(mpg123_handle *mh) {
    mpg123_close(mh);
    mpg123_delete(mh);
    mpg123_exit();
}

static size_t feeder_tick(char *data, size_t size, size_t nmemb, void *_feeder) {
    static size_t total = 0;
    int err;
    feeder_ctx_t *feeder = (feeder_ctx_t *)_feeder;
    size_t count = size*nmemb;

    err = mpg123_feed(feeder->mh, (unsigned char *)data, count);
    total += count;
    if (err == MPG123_ERR) {
        fprintf(stderr, "Error feeding MP3 data: %s\n", mpg123_plain_strerror(err));
        return 0;
    }

    return count;
}

static int decoder_tick(player_ctx_t *player) {
    off_t ret;
    ALenum err;
    long rate;
    int channels, encoding;
    size_t count, total = 0;
    unsigned char out[BLOCK_SIZE];
    ALint state, queued, processed;
    ALuint buf_id;

    count = 0;
    ret = MPG123_OK;
    switch (player->state) {
        case ST_WAIT_FORMAT_KNOWN:
            ret = mpg123_read(player->mh, out, sizeof(out), &count);
            if (ret == MPG123_NEW_FORMAT) {
                mpg123_getformat(player->mh, &rate, &channels, &encoding);
                printf("Format - sample-rate: %ld, channel count: %d\n", rate, channels);
                player->sample_rate = rate;
                if (!(encoding & MPG123_ENC_8 || encoding & MPG123_ENC_16)) {
                    fprintf(stderr, "Unsupported bit-depth. Aborting ...\n");
                    return 1;
                }
                switch (channels) {
                    case 1:
                        if (encoding & MPG123_ENC_16) {
                            player->format = AL_FORMAT_MONO16;
                        }
                        else if (encoding & MPG123_ENC_8) {
                            player->format = AL_FORMAT_MONO8;
                        }
                        break;
                    case 2:
                        if (encoding & MPG123_ENC_16) {
                            player->format = AL_FORMAT_STEREO16;
                        }
                        else if (encoding & MPG123_ENC_8) {
                            player->format = AL_FORMAT_STEREO8;
                        }
                        break;
                    default:
                        fprintf(stderr, "Unsupported channel count: %d. Aborting ...\n", channels);
                        return 1;
                }
                mpg123_format_none(player->mh);
                mpg123_format(player->mh, rate, channels, encoding);
                player->state = ST_WAIT_PLAY;
            }
            break;
        case ST_WAIT_PLAY:
            alGetSourcei(player->source, AL_BUFFERS_QUEUED, &queued);
            // Once the buffer queue is full, start playing.
            if (queued == N_BUFFERS) {
                printf("Starting playback ...\n");
                alSourcePlay(player->source);
                player->state = ST_PLAYING;
                break;
            }
            ret = mpg123_read(player->mh, out, sizeof(out), &count);
            if (ret == MPG123_NEED_MORE) {
                break;
            }
            alGetError();
            alBufferData(player->buffers[queued], player->format, out, (ALsizei) count, player->sample_rate);
            if ((err = alGetError()) != AL_NO_ERROR) {
                fprintf(stderr, "Error adding data to buffer %d. Aborting ...\n", queued);
                return 1;
            }
            alSourceQueueBuffers(player->source, 1, player->buffers+queued);
            if ((err = alGetError()) != AL_NO_ERROR) {
                fprintf(stderr, "Error adding buffer to queue. Aborting ...\n");
                return 1;
            }
            break;
        case ST_PLAYING:
            alGetSourcei(player->source, AL_SOURCE_STATE, &state);
            alGetSourcei(player->source, AL_BUFFERS_PROCESSED, &processed);
            if (processed == 0) {
                break;
            }
            ret = mpg123_read(player->mh, out, sizeof(out), &count);
            if (ret == MPG123_NEED_MORE || count == 0) {
                break;
            }
            alSourceUnqueueBuffers(player->source, 1, &buf_id);
            alBufferData(buf_id, player->format, out, (ALsizei) count, player->sample_rate);
            if ((err = alGetError()) != AL_NO_ERROR) {
                fprintf(stderr, "Error adding data to buffer %d. Aborting ...\n", queued);
                return 1;
            }
            alSourceQueueBuffers(player->source, 1, &buf_id);
            if ((err = alGetError()) != AL_NO_ERROR) {
                fprintf(stderr, "Error adding buffer to queue. Aborting ...\n");
                return 1;
            }
            /*if (state != AL_PLAYING) {
                printf("Finished! ...\n");
                return 1;
            }*/
            break;
        default:
            break;
    }
    total += count;
    if (ret == MPG123_ERR) {
        printf("Error decoding: %s\n", mpg123_plain_strerror(ret));
        return 1;
    }

    return 0;
}

int on_progress(void *_player, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    int rc;
    player_ctx_t *player = (player_ctx_t *)_player;

    rc = decoder_tick(player);

    return rc;
}

static int feeder_init(feeder_ctx_t *feeder, mpg123_handle *mh) {
    feeder->mh = mh;
}

static int player_init(player_ctx_t *player, mpg123_handle *mh) {
    ALCenum err;

    alGetError();

    player->state = ST_WAIT_FORMAT_KNOWN;
    player->next_buf = 0;
    player->sample_rate = 48000;
    player->format = AL_FORMAT_STEREO16;
    player->mh = mh;

    // Use the default OpenAL device.
    player->device = alcOpenDevice(NULL);
    if (player->device == NULL) {
        fprintf(stderr, "Could not open OpenAL device.\n");
        return 1;
    }

    player->ctx = alcCreateContext(player->device, NULL);
    if (player->ctx == NULL) {
        fprintf(stderr, "Could not create OpenAL context: %s\n");
        return 1;
    }
    alcMakeContextCurrent(player->ctx);

    player->source = 0;
    alGenSources(1, &player->source);
    if ((err = alGetError()) != AL_NO_ERROR) {
        fprintf(stderr, "Could not generate OpenAL source: %d\n", err);
        return 1;
    }
    alSourceRewind(player->source);
    alSourcei(player->source, AL_BUFFER, 0);

    alGenBuffers(N_BUFFERS, player->buffers);
    if ((err = alGetError()) != AL_NO_ERROR) {
        fprintf(stderr, "Could not generate OpenAL buffers: %d\n", err);
        return 1;
    }

    return 0;
}

static void player_deinit(player_ctx_t *player) {
    alDeleteBuffers(N_BUFFERS, player->buffers);
    alDeleteSources(1, &player->source);
    alcMakeContextCurrent(NULL);
    alcDestroyContext(player->ctx);
    alcCloseDevice(player->device);
}

static mpg123_handle *mpg123_init_l(mpg123_handle *mh) {
    int err = MPG123_OK;

    err = mpg123_init();
    if (err != MPG123_OK) {
        fprintf(stderr, "Decoder initialization failed: %s\n",  mpg123_plain_strerror(err));
        mpg123_cleanup(mh);
        return NULL;
    }
    mh = mpg123_new(NULL, &err);
    if (mh == NULL) {
        fprintf(stderr, "Decoder initialization failed: %s\n",  mpg123_plain_strerror(err));
        mpg123_cleanup(mh);
        return NULL;
    }
    err = mpg123_open_feed(mh);
    if (err != MPG123_OK) {
        fprintf(stderr, "Could not initialize bit stream ...\n");
        mpg123_cleanup(mh);
        return NULL;
    }

    return mh;
}

int main(int argc, char *argv[]) {
    mpg123_handle *mh = NULL;
    feeder_ctx_t feeder;
    player_ctx_t player;
    int rc;
    CURLcode res;
    CURL *curl;
    char *url;

    if (argc < 2) {
        fprintf(stderr, "You must specify a URL to stream!\n");
        return 1;
    }

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Could not initialize cURL context. Aborting ...\n");
        return 1;
    }

    url = argv[1];
    curl_easy_setopt(curl, CURLOPT_URL, (char *)url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, (void *)&player);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, feeder_tick);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&feeder);

    mh = mpg123_init_l(mh);
    if (mh == NULL) {
        fprintf(stderr, "Could not initialize mpg123 handle. Aborting ...\n");
        return 1;
    }

    rc = player_init(&player, mh);
    if (rc) {
        fprintf(stderr, "Could not initialize player. Aborting ...\n");
    }

    rc = feeder_init(&feeder, mh);
    if (rc) {
        fprintf(stderr, "Could not initialize feeder. Aborting ...\n");
    }

    printf("Trying to stream audio from %s ...\n\n", url);
    res = curl_easy_perform(curl);
    if (res == CURLE_ABORTED_BY_CALLBACK) {
        fprintf(stderr, "Request to abort by user. Exiting ...\n");
    }
    else if (res != CURLE_OK) {
        fprintf(stderr, "cURL failed: %s\n", curl_easy_strerror(res));
    }
    else {
        printf("Stream fully downloaded. Playback continuing ...\n");
    }

    do {
        rc = decoder_tick(&player);
    } while (!rc);

    curl_easy_cleanup(curl);
    player_deinit(&player);
    mpg123_cleanup(mh);

    return 0;
}
