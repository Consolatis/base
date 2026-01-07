#pragma once

#include <stdio.h>

// FIXME: log() clashes with <math.h>, convert calls to base_log() and remove log()

#define log(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__);
#define base_log(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__);
