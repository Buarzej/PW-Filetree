#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "readers_writers.h"
#include "err.h"

struct Readwrite {
    pthread_mutex_t lock;
    pthread_cond_t readers;
    pthread_cond_t writers;
    pthread_cond_t removers;
    int rcount, wcount, rwait, wwait, change;
};

Readwrite *rw_new() {
    Readwrite *rw = malloc(sizeof(Readwrite));
    if (!rw)
        return NULL;

    if (pthread_mutex_init(&rw->lock, 0) != 0)
        syserr("Mutex initalization failed");
    if (pthread_cond_init(&rw->readers, 0) != 0)
        syserr("Readers condition initalization failed");
    if (pthread_cond_init(&rw->writers, 0) != 0)
        syserr("Writers condition initalization failed");
    if (pthread_cond_init(&rw->removers, 0) != 0)
        syserr("Removers condition initalization failed");

    rw->rcount = 0;
    rw->wcount = 0;
    rw->rwait = 0;
    rw->wwait = 0;
    rw->change = 0; /* 0 = nobody has priority to enter */

    return rw;
}

void rw_free(Readwrite *rw) {
    if (pthread_mutex_destroy(&rw->lock) != 0)
        syserr("Mutex destroy failed");
    if (pthread_cond_destroy(&rw->readers) != 0)
        syserr("Readers condition destroy failed");
    if (pthread_cond_destroy(&rw->writers) != 0)
        syserr("Writers condition destroy failed");
    if (pthread_cond_destroy(&rw->removers) != 0)
        syserr("Removers condition destroy failed");

    free(rw);
}

void rw_before_read(Readwrite *rw) {
    if (pthread_mutex_lock(&rw->lock) != 0)
        syserr("Lock failed");

    while (rw->change <= 0 && rw->wcount + rw->wwait > 0) {
        rw->rwait++;
        if (pthread_cond_wait(&rw->readers, &rw->lock) != 0)
            syserr("Condition wait failed");
        rw->rwait--;
    }

    if (rw->change > 0) /* How many more readers should enter */
        rw->change--;

    rw->rcount++;
    if (rw->change > 0)
        if (pthread_cond_signal(&rw->readers) != 0)
            syserr("Condition signal failed");

    if (pthread_mutex_unlock(&rw->lock) != 0)
        syserr("Mutex unlock failed");
}

void rw_after_read(Readwrite *rw) {
    if (pthread_mutex_lock(&rw->lock) != 0)
        syserr("Lock failed");

    rw->rcount--;
    if (rw->rcount == 0 && rw->wwait > 0) {
        rw->change = -1; /* -1 = writer has now priority to enter */
        if (pthread_cond_signal(&rw->writers) != 0)
            syserr("Condition signal failed");
    } else if (rw->rcount == 0) {
        if (pthread_cond_signal(&rw->removers) != 0)
            syserr("Condition signal failed");
    }

    if (pthread_mutex_unlock(&rw->lock) != 0)
        syserr("Mutex unlock failed");
}

void rw_before_write(Readwrite *rw) {
    if (pthread_mutex_lock(&rw->lock) != 0)
        syserr("Lock failed");

    while (rw->change != -1 && rw->wcount + rw->rcount > 0) {
        rw->wwait++;
        if (pthread_cond_wait(&rw->writers, &rw->lock) != 0)
            syserr("Condition wait failed");
        rw->wwait--;
    }
    rw->change = 0; /* 0 = nobody has priority to enter */

    rw->wcount++;
    if (pthread_mutex_unlock(&rw->lock) != 0)
        syserr("Mutex unlock failed");
}

void rw_after_write(Readwrite *rw) {
    if (pthread_mutex_lock(&rw->lock) != 0)
        syserr("Lock failed");

    rw->wcount--;
    if (rw->rwait > 0) {
        rw->change = rw->rwait; /* How many readers should enter */
        if (pthread_cond_signal(&rw->readers) != 0)
            syserr("Condition signal failed");
    } else if (rw->wwait > 0) {
        rw->change = -1; /* -1 = writer has now priority to enter */
        if (pthread_cond_signal(&rw->writers) != 0)
            syserr("Condition signal failed");
    } else {
        if (pthread_cond_signal(&rw->removers) != 0)
            syserr("Condition signal failed");
    }

    if (pthread_mutex_unlock(&rw->lock) != 0)
        syserr("Mutex unlock failed");
}

void rw_before_remove(Readwrite *rw) {
    if (pthread_mutex_lock(&rw->lock) != 0)
        syserr("Lock failed");

    while (rw->rwait + rw->rcount + rw->wwait + rw->wcount > 0) {
        if (pthread_cond_wait(&rw->removers, &rw->lock) != 0)
            syserr("Condition wait failed");
    }

    if (pthread_mutex_unlock(&rw->lock) != 0)
        syserr("Mutex unlock failed");
}