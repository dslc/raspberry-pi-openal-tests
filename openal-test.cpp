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

static size_t feeder_tick(char *data, size_t size, size_t nmemb, void *_player) {
    Player *player = (Player *)_player;
    size_t count = size*nmemb;

    memcpy(player->m_mp3Data+player->m_mp3WritePtr, data, count);
    player->m_mp3WritePtr += count;

    return count;
}

void usage(char *program_name) {
    fprintf(stderr,
            "MP3 streaming test with OpenAL\n\n"
            "Usage: %s <url>\n"
            "  <url> should be the full remote URL.\n\n\n",
            program_name);
}

int main(int argc, char *argv[]) {
    int rc;
    CURLcode res;
    CURLMcode resm;
    Player *player;
    CURL *curl;
    CURLM *curlm;
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

    player = new Player();

    url = argv[1];
    curl_easy_setopt(curl, CURLOPT_URL, (char *)url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, feeder_tick);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)player);

    curlm = curl_multi_init();
    if (!curlm) {
        fprintf(stderr, "Could not initialize curl multi handle. Aborting ...\n");
        return 1;
    }

    curl_multi_add_handle(curlm, curl);

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

    int handle_count;
    int max_fd = -1;
    bool download_finished = false;
    long suggested_timeout;
    struct timeval timeout;
    fd_set fd_rset, fd_wset, fd_eset;

    do {
        if (!download_finished) {
            FD_ZERO(&fd_rset);
            FD_ZERO(&fd_wset);
            FD_ZERO(&fd_eset);
            curl_multi_timeout(curlm, &suggested_timeout);
            if (suggested_timeout < 0) {
                suggested_timeout = 1000;
            }
            curl_multi_fdset(curlm, &fd_rset, &fd_wset, &fd_eset, &max_fd);
            if (max_fd == -1) {
                timeout.tv_sec = suggested_timeout / 1000;
                timeout.tv_usec = (suggested_timeout % 1000) * 1000;
                select(max_fd+1, &fd_rset, &fd_wset, &fd_eset, &timeout);
            }
            else {
                usleep(suggested_timeout);
            }
            resm = curl_multi_perform(curlm, &handle_count);
            if (handle_count == 0) {
                printf("Download complete.\n");
                download_finished = true;
            }
        }
        else {
            // Even at a high sample-rate (96000), the chosen buffer size should provide
            // 125 ms audio (for 16-bit stereo) - so it's okay to pause before checking
            //  the buffer queue again.
            usleep(UPDATE_QUEUE_INTERVAL);
        }
        rc = player->tick();
    } while (!rc);

    curl_multi_remove_handle(curlm, curl);
    curl_easy_cleanup(curl);
    curl_multi_cleanup(curlm);
    delete(player);
    // Restore terminal attributes.
    tcsetattr(fileno(stdin), TCSANOW, &orig_term_attr);

    return 0;
}

