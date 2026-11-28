// Test-only observation of the decoder boundary in unchanged GTK builds.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/resource.h>
#include <time.h>

static atomic_ulong Count, Nanoseconds;
static pthread_once_t Initialize = PTHREAD_ONCE_INIT;
static GdkTexture* (*Decode)(GBytes*, GError**);

static void Resolve(void) {
    Decode = dlsym(RTLD_NEXT, "gdk_texture_new_from_bytes");
}

static unsigned long Now(void) {
    struct timespec Value;
    clock_gettime(CLOCK_MONOTONIC, &Value);
    return (unsigned long)Value.tv_sec * 1000000000UL + Value.tv_nsec;
}

GdkTexture* gdk_texture_new_from_bytes(GBytes* Bytes, GError** Error) {
    pthread_once(&Initialize, Resolve);
    if (!Decode) return NULL;
    const unsigned long Started = Now();
    GdkTexture* Value = Decode(Bytes, Error);
    atomic_fetch_add_explicit(&Count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&Nanoseconds, Now() - Started, memory_order_relaxed);
    return Value;
}

__attribute__((destructor)) static void Report(void) {
    struct rusage Usage = {0};
    getrusage(RUSAGE_SELF, &Usage);
    const double CpuMs = (Usage.ru_utime.tv_sec + Usage.ru_stime.tv_sec) * 1000.0 +
        (Usage.ru_utime.tv_usec + Usage.ru_stime.tv_usec) / 1000.0;
    fprintf(stderr, "[LUI:ImageProbe] decodes=%lu decoderNs=%lu peakRssKiB=%ld cpuMs=%.3f\n",
        atomic_load(&Count), atomic_load(&Nanoseconds), Usage.ru_maxrss, CpuMs);
}
