#include "ring_buffer.hpp"
#include <cstring>
#include <cstdio>
#include <stdlib.h>

RingBuffer::RingBuffer(size_t size) {
    this->data = (char *)malloc(size);
    if (this->data == NULL) {
        fprintf(stderr, "Could not allocate space for buffer!\n");
        exit(1);
    }
    this->size = size;
    this->rd = 0;
    this->wr = 0;
    this->full = false;
}

RingBuffer::~RingBuffer() {
    free(this->data);
}

size_t RingBuffer::write(char *src, size_t len) {
    size_t count_end = 0, count_start = 0;

    if (this->rd > this->wr) {
        count_end = len > (this->rd - this->wr) ? this->rd - this->wr : len;
        memcpy(this->data+this->wr, src, count_end);
        this->wr += count_end;
    }
    else if (this->rd < this->wr || (this->rd == this->wr && !this->full)) {
        count_end = len > (this->size - this->wr) ? (this->size - this->wr) : len;
        count_start = len > count_end ? len - count_end : 0;
        memcpy(this->data+this->wr, src, count_end);
        memcpy(this->data, src+count_end, count_start);
        this->wr = (this->wr + count_end + count_start) % this->size;
    }
    if (count_end + count_start > 0) {
        if (this->rd == this->wr) {
            this->full = true;
        }
        else {
            this->full = false;
        }
    }

    return count_end + count_start;
}

size_t RingBuffer::read(char *dest, size_t len) {
    size_t count_end = 0, count_start = 0;

    if (this->rd < this->wr) {
        count_end = len > (this->wr - this->rd) ? (this->wr - this->rd) : len;
        memcpy(dest, this->data+this->rd, count_end);
        this->rd += count_end;
    }
    else if (this->rd > this->wr || (this->rd == this->wr && this->full)) {
        len = len > (this->size - this->rd + this->wr) ? (this->size - this->rd + this->wr) : len;
        count_end = len > (this->size - this->rd) ? (this->size - this->rd) : len;
        count_start = len > count_end ? len - count_end : 0;
        memcpy(dest, this->data+this->rd, count_end);
        memcpy(dest+count_end, this->data, count_start);
        this->rd = (this->rd + count_end + count_start) % this->size;
    }

    if (count_end + count_start > 0) {
        this->full = false;
    }

    return count_end + count_start;
}

off_t RingBuffer::seek(off_t offset) {
    if (offset > this->size-1) {
        this->rd = this->size-1;
    }
    else {
        this->rd = offset;
    }

    return this->rd;
}


