#include "player.hpp"
#include <cstdio>
#include <cstring>
#include "ring_buffer.hpp"

Player::Player() {
    int mpg123_err = MPG123_OK;
    ALCenum err; 
    this->m_state = ST_WAIT_FORMAT_KNOWN;
    this->m_sampleRate = 48000;
    this->m_format = AL_FORMAT_STEREO16;
    this->m_mp3Buf = std::make_unique<RingBuffer>(MP3_BUF_SIZE);

    // Initialize the mp3 decoder
    mpg123_err = mpg123_init();
    if (mpg123_err != MPG123_OK) {
        fprintf(stderr, "Decoder initialization failed: %s\n",  mpg123_plain_strerror(mpg123_err));
    }
    this->m_decoder = mpg123_new(NULL, &mpg123_err);
    if (this->m_decoder == NULL) {
        fprintf(stderr, "Decoder initialization failed: %s\n",  mpg123_plain_strerror(mpg123_err));
    }
    mpg123_err = mpg123_open_feed(this->m_decoder);
    if (mpg123_err != MPG123_OK) {
        fprintf(stderr, "Could not initialize bit stream ...\n");
    }

    // Use the default OpenAL device.
    this->m_device = alcOpenDevice(NULL);
    if (this->m_device == NULL) {
        fprintf(stderr, "Could not open OpenAL device.\n");
    }

    this->m_ctx = alcCreateContext(this->m_device, NULL);
    if (this->m_ctx == NULL) {
        fprintf(stderr, "Could not create OpenAL context: %s\n");
    }
    alcMakeContextCurrent(this->m_ctx);

    alGetError();
    this->m_source = 0;
    alGenSources(1, &this->m_source);
    if ((err = alGetError()) != AL_NO_ERROR) {
        fprintf(stderr, "Could not generate OpenAL source: %d\n", err);
    }
    alSourceRewind(this->m_source);
    alSourcei(this->m_source, AL_BUFFER, 0);

    alGenBuffers(N_BUFFERS, this->m_buffers);
    if ((err = alGetError()) != AL_NO_ERROR) {
        fprintf(stderr, "Could not generate OpenAL buffers: %d\n", err);
    }
}

Player::~Player() {
    ALint processed;
    ALuint buf_ids[N_BUFFERS];

    // De-initialize mp3 decoder
    mpg123_close(this->m_decoder);
    mpg123_delete(this->m_decoder);
    mpg123_exit();

    // De-initialize OpenAL stuff ...
    alGetSourcei(this->m_source, AL_BUFFERS_PROCESSED, &processed);
    alSourceUnqueueBuffers(this->m_source, processed, buf_ids);

    alDeleteBuffers(N_BUFFERS, this->m_buffers);
    alDeleteSources(1, &this->m_source);
    alcMakeContextCurrent(NULL);
    alcDestroyContext(this->m_ctx);
    alcCloseDevice(this->m_device);
}

int Player::tick(void) {
    off_t ret, mp3_seek_offset;
    ALenum err;
    long rate;
    int channels, encoding;
    size_t count;
    static size_t offset;
    char in[FEED_CHUNK_SIZE];
    unsigned char out[AL_BUF_SIZE];
    ALint state, queued, processed;
    ALuint buf_id;
    int mpg123_err;
    int ch;
    static int i = 0;

    RingBuffer *buf = this->getRingBuffer();
    count = 0;
    ret = MPG123_OK;
    ch = fgetc(stdin);
    if (ch == 'q') {
        printf("Exiting on request of user ...\n");
        alSourceStop(this->m_source);
        return 1;
    }
    if (ch == 'r') {
        printf("Trying to rewind ...\n");
        err = mpg123_feedseek(this->m_decoder, 0, SEEK_SET, &mp3_seek_offset);
        if (err < 0) {
            fprintf(stderr, "Could not seek to beginning: %s\n", mpg123_plain_strerror(err));
        }
        else {
            buf->seek(0);
        }
    }
    count = buf->read(in, sizeof(in));
    mpg123_err = mpg123_feed(this->m_decoder, (unsigned char *)in, count);
    if (mpg123_err == MPG123_ERR) {
        fprintf(stderr, "Error feeding MP3 data: %s\n", mpg123_plain_strerror(mpg123_err));
        return 1;
    }
    switch (this->m_state) {
        case ST_WAIT_FORMAT_KNOWN:
            ret = mpg123_read(this->m_decoder, out, sizeof(out), &count);
            if (ret == MPG123_NEW_FORMAT) {
                mpg123_getformat(this->m_decoder, &rate, &channels, &encoding);
                printf("Format - sample-rate: %ld, channel count: %d\n", rate, channels);
                this->m_sampleRate = rate;
                if (!(encoding & MPG123_ENC_8 || encoding & MPG123_ENC_16)) {
                    fprintf(stderr, "Unsupported bit-depth. Aborting ...\n");
                    return 1;
                }
                switch (channels) {
                    case 1:
                        this->m_format = encoding & MPG123_ENC_8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
                        break;
                    case 2:
                        this->m_format = encoding & MPG123_ENC_8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
                        break;
                    default:
                        fprintf(stderr, "Unsupported channel count: %d. Aborting ...\n", channels);
                        return 1;
                }
                mpg123_format_none(this->m_decoder);
                mpg123_format(this->m_decoder, rate, channels, encoding);
                this->m_state = ST_WAIT_PLAY;
                offset = 0;
            }
            break;
        case ST_WAIT_PLAY:
            alGetSourcei(this->m_source, AL_BUFFERS_QUEUED, &queued);
            // Once the buffer queue is full, start playing.
            if (queued == N_BUFFERS) {
                printf("Starting playback ...\n");
                alSourcePlay(this->m_source);
                this->m_state = ST_PLAYING;
                offset = 0;
                break;
            }
            ret = mpg123_read(this->m_decoder, out+offset, sizeof(out)-offset, &count);
            offset += count;
            if (count == 0) {
                break;
            }
            alGetError();
            alBufferData(this->m_buffers[queued], this->m_format, out, (ALsizei) offset, this->m_sampleRate);
            offset = 0;
            if ((err = alGetError()) != AL_NO_ERROR) {
                fprintf(stderr, "Error adding data to buffer %d. Aborting ...\n", queued);
                return 1;
            }
            alSourceQueueBuffers(this->m_source, 1, &this->m_buffers[queued]);
            if ((err = alGetError()) != AL_NO_ERROR) {
                fprintf(stderr, "Error adding buffer to queue. Aborting ...\n");
                return 1;
            }
            break;
        case ST_PLAYING:
            if (ch == 'p') {
                this->m_state = ST_PAUSED;
                alSourcePause(this->m_source);
                printf("Pausing\n");
                break;
            }
            alGetError();
            alGetSourcei(this->m_source, AL_SOURCE_STATE, &state);
            alGetSourcei(this->m_source, AL_BUFFERS_PROCESSED, &processed);
            if ((err = alGetError()) != AL_NO_ERROR) {
                fprintf(stderr, "Error examining buffer queue. Aborting ...\n");
                return 1;
            }
            if (processed) {
                alSourceUnqueueBuffers(this->m_source, 1, &buf_id);
                if ((err = alGetError()) != AL_NO_ERROR) {
                    fprintf(stderr, "Error removing buffer from queue. Aborting ...\n");
                    return 1;
                }
                ret = mpg123_read(this->m_decoder, out+offset, sizeof(out)-offset, &count);
                offset += count;
                if (count == 0) {
                    break;
                }
                alBufferData(buf_id, this->m_format, out, (ALsizei) offset, this->m_sampleRate);
                offset = 0;
                if ((err = alGetError()) != AL_NO_ERROR) {
                    fprintf(stderr, "Error adding data to buffer %d. Aborting ...\n", buf_id);
                    return 1;
                }
                alSourceQueueBuffers(this->m_source, 1, &buf_id);
                if ((err = alGetError()) != AL_NO_ERROR) {
                    fprintf(stderr, "Error adding buffer to queue. Aborting ...\n");
                    return 1;
                }
            }
            if (state != AL_PLAYING && state != AL_PAUSED) {
                alGetSourcei(this->m_source, AL_BUFFERS_QUEUED, &queued);
                if (queued == 0) {
                    printf("Playback finished. Exiting ...\n");
                    return 1;
                }
                alSourcePlay(this->m_source);
            }
            break;
        case ST_PAUSED:
            if (ch == 'p') {
                this->m_state = ST_PLAYING;
                alSourcePlay(this->m_source);
                printf("Resuming\n");
            }
            break;
        default:
            break;
    }
    if (ret == MPG123_ERR) {
        printf("Error decoding: %s\n", mpg123_plain_strerror(ret));
        return 1;
    }

    return 0;
}

void Player::getALBufferInfo(ALint *queued, ALint *processed, char *state) {
    ALint al_state;
    alGetSourcei(this->m_source, AL_BUFFERS_QUEUED, queued);
    alGetSourcei(this->m_source, AL_BUFFERS_PROCESSED, processed);
    alGetSourcei(this->m_source, AL_SOURCE_STATE, &al_state);
    switch (al_state) {
        case AL_PLAYING:
            strcpy(state, "Playing");
            break;
        case AL_PAUSED:
            strcpy(state, "Paused");
            break;
        case AL_STOPPED:
            strcpy(state, "Stopped");
            break;
        default:
            strcpy(state, "Unknown");
            break;
    }
}

mpg123_handle *Player::getDecoder(void) {
    return this->m_decoder;
}

RingBuffer *Player::getRingBuffer(void) {
    return this->m_mp3Buf.get();
}

