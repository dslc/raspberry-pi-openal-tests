#pragma once

#include <mpg123.h>
#include <AL/al.h>
#include <AL/alc.h>

/**
 *  With this block size - at 48000 Hz, stereo, with 16-bits / sample - each buffer
 *  will provides .25 seconds of audio.
 */
#define AL_BUF_SIZE 48000
#define N_BUFFERS 5
#define MP3_BUF_SIZE 5*1024*1024

typedef enum {
    ST_WAIT_FORMAT_KNOWN,
    ST_WAIT_PLAY,
    ST_PLAYING,
    ST_PAUSED,
} PlayerState;

class Player {
    public:
        Player();
        ~Player();
        int tick(void);
        mpg123_handle *getDecoder(void);

        char m_mp3Data[MP3_BUF_SIZE];
        off_t m_mp3ReadPtr, m_mp3WritePtr;

    private:
        PlayerState m_state;
        ALsizei m_sampleRate;
        ALCdevice *m_device;
        ALCcontext *m_ctx;
        ALenum m_format;
        ALuint m_source;
        ALuint m_buffers[N_BUFFERS];
        mpg123_handle *m_decoder;
};

