#pragma once

#include <sys/types.h>

class RingBuffer {

    public:
        // ctors/dtors
        RingBuffer(size_t);
        ~RingBuffer();

        size_t write(char *, size_t);
        size_t read(char *, size_t);
        off_t seek(off_t);

    private:
        off_t rd, wr;
        bool full;
        size_t size;
        char *data;

};

