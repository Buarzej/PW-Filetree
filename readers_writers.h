#pragma once

typedef struct Readwrite Readwrite;

Readwrite *rw_new();

void rw_free(Readwrite *rw);

void rw_before_read(Readwrite *rw);

void rw_after_read(Readwrite *rw);

void rw_before_write(Readwrite *rw);

void rw_after_write(Readwrite *rw);

void rw_before_remove(Readwrite *rw);