/*
 * scanner.c
 * Copyright 2012 John Lindgren
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <glib.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "scanner.h"

struct _ScanRequest {
    char * filename; /* pooled */
    int flags;
    PluginHandle * decoder;
    ScanCallback callback;
    Tuple * tuple;
};

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static GQueue requests = G_QUEUE_INIT;

static pthread_t scan_threads[SCAN_THREADS];

static bool_t quit_flag = FALSE;

ScanRequest * scan_request (const char * filename, int flags,
 PluginHandle * decoder, ScanCallback callback)
{
    ScanRequest * request = g_slice_new (ScanRequest);

    request->filename = str_get (filename);
    request->flags = flags;
    request->decoder = decoder;
    request->callback = callback;
    request->tuple = 0;

    pthread_mutex_lock (& mutex);

    g_queue_push_tail (& requests, request);
    pthread_cond_signal (& cond);

    pthread_mutex_unlock (& mutex);
    return request;
}

static void * scan_worker (void * unused)
{
    pthread_mutex_lock (& mutex);

    while (! quit_flag)
    {
        ScanRequest * request = g_queue_pop_head (& requests);

        if (! request)
        {
            pthread_cond_wait (& cond, & mutex);
            continue;
        }

        pthread_mutex_unlock (& mutex);

        if (! request->decoder)
            request->decoder = file_find_decoder (request->filename, FALSE);
        if (request->decoder && (request->flags & SCAN_TUPLE))
            request->tuple = file_read_tuple (request->filename, request->decoder);

        request->callback (request);

        if (request->tuple)
            tuple_unref (request->tuple);

        str_unref (request->filename);
        g_slice_free (ScanRequest, request);

        pthread_mutex_lock (& mutex);
    }

    pthread_mutex_unlock (& mutex);
    return NULL;
}

PluginHandle * scan_request_get_decoder (ScanRequest * request)
{
    return request->decoder;
}

Tuple * scan_request_get_tuple (ScanRequest * request)
{
    Tuple * tuple = request->tuple;
    request->tuple = NULL;
    return tuple;
}

void scanner_init (void)
{
    for (int i = 0; i < SCAN_THREADS; i ++)
        pthread_create (& scan_threads[i], 0, scan_worker, NULL);
}

void scanner_cleanup (void)
{
    pthread_mutex_lock (& mutex);

    quit_flag = TRUE;
    pthread_cond_broadcast (& cond);

    pthread_mutex_unlock (& mutex);

    for (int i = 0; i < SCAN_THREADS; i ++)
        pthread_join (scan_threads[i], NULL);
}
