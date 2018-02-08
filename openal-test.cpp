#include <mpg123.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <fcntl.h>
#include <curl/curl.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>
#include "player.hpp"

#define UPDATE_QUEUE_INTERVAL 50000 // microseconds

static void mpg123_cleanup(mpg123_handle *mh) {
    mpg123_close(mh);
    mpg123_delete(mh);
    mpg123_exit();
}

static size_t feeder_tick(char *data, size_t size, size_t nmemb, void *_player) {
    static size_t total = 0;
    int err;
    Player *player = (Player *)_player;
    size_t count = size*nmemb;

    err = mpg123_feed(player->getDecoder(), (unsigned char *)data, count);
    total += count;
    if (err == MPG123_ERR) {
        fprintf(stderr, "Error feeding MP3 data: %s\n", mpg123_plain_strerror(err));
        return 0;
    }

    return count;
}

int on_progress(void *_player, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    int rc;
    Player *player = (Player *)_player;

    rc = player->tick();

    return rc;
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

void usage(char *program_name) {
    fprintf(stderr,
            "MP3 streaming test with OpenAL\n\n"
            "Usage: %s <url>\n"
            "  <url> should be the full remote URL.\n\n\n",
            program_name);
}

int main(int argc, char *argv[]) {
    mpg123_handle *mh = NULL;
    int rc;
    CURLcode res;
    Player *player;
    CURL *curl;
    char *url;
    struct termios orig_term_attr, new_term_attr;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Could not initialize cURL context. Aborting ...\n");
        return 1;
    }

    mh = mpg123_init_l(mh);
    if (mh == NULL) {
        fprintf(stderr, "Could not initialize mpg123 handle. Aborting ...\n");
        return 1;
    }

    player = new Player(mh);

    url = argv[1];
    curl_easy_setopt(curl, CURLOPT_URL, (char *)url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, (void *)player);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, feeder_tick);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)player);

    // Allow user key-press (for play/pause).
    tcgetattr(fileno(stdin), &orig_term_attr);
    memcpy(&new_term_attr, &orig_term_attr, sizeof(struct termios));
    new_term_attr.c_lflag &= ~(ECHO|ICANON);
	new_term_attr.c_cc[VTIME] = 0;
    new_term_attr.c_cc[VMIN] = 0;
    tcsetattr(fileno(stdin), TCSANOW, &new_term_attr);

    // Begin stream using cURL.
    printf("Trying to stream audio from %s ...\n\n", url);
    printf(" - Press 'p' to pause / resume.\n");
    printf(" - Press 'q' to quit.\n\n");
    res = curl_easy_perform(curl);
    if (res == CURLE_ABORTED_BY_CALLBACK) {
        fprintf(stderr, "Playback terminated. Exiting ...\n");
    }
    else if (res != CURLE_OK) {
        fprintf(stderr, "cURL failed: %s\n", curl_easy_strerror(res));
    }
    else {
        printf("Stream fully downloaded. Playback continuing ...\n");
    }

    do {
        // Even at a high sample-rate (96000), the chosen buffer size should provide
        // 125 ms audio (for 16-bit stereo) - so it's okay to pause before checking
        //  the buffer queue again.
        usleep(UPDATE_QUEUE_INTERVAL);
        rc = player->tick();
    } while (!rc);

    curl_easy_cleanup(curl);
    delete(player);
    mpg123_cleanup(mh);
    // Restore terminal attributes.
    tcsetattr(fileno(stdin), TCSANOW, &orig_term_attr);

    return 0;
}

