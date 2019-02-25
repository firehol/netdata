// SPDX-License-Identifier: GPL-3.0-or-later
#define NETDATA_RRD_INTERNALS

#include "rrdengine.h"

void sanity_check(void)
{
    /* Magic numbers must fit in the super-blocks */
    BUILD_BUG_ON(strlen(RRDENG_DF_MAGIC) > RRDENG_MAGIC_SZ);
    BUILD_BUG_ON(strlen(RRDENG_JF_MAGIC) > RRDENG_MAGIC_SZ);

    /* Version strings must fit in the super-blocks */
    BUILD_BUG_ON(strlen(RRDENG_DF_VER) > RRDENG_VER_SZ);
    BUILD_BUG_ON(strlen(RRDENG_JF_VER) > RRDENG_VER_SZ);

    /* Data file super-block cannot be larger than RRDENG_BLOCK_SIZE */
    BUILD_BUG_ON(RRDENG_DF_SB_PADDING_SZ < 0);

    BUILD_BUG_ON(sizeof(uuid_t) != UUID_SZ); /* check UUID size */

    /* page count must fit in 8 bits */
    BUILD_BUG_ON(MAX_PAGES_PER_EXTENT > 255);
}

/*
 * Global state of RRD Engine
 */
rrdengine_state_t rrdengine_state;
struct rrdengine_worker_config worker_config;
struct completion rrdengine_completion;
struct page_cache pg_cache;
struct transaction_commit_log commit_log;
struct rrdengine_datafile datafile;
struct rrdengine_journalfile journalfile;
uint8_t global_compress_alg = RRD_NO_COMPRESSION;
//uint8_t global_compress_alg = RRD_LZ4;

void read_extent_cb(uv_fs_t* req)
{
    struct extent_io_descriptor *xt_io_descr;
    struct rrdeng_page_cache_descr *descr;
    int i, j, ret;
    unsigned count, pos;
    void *page, *payload, *uncompressed_buf;
    uint32_t payload_length, payload_offset, offset, page_offset, uncompressed_payload_length;
    /* persistent structures */
    struct rrdeng_df_extent_header *header;
    struct rrdeng_df_extent_trailer *trailer;
    uLong crc;

    if (req->result < 0) {
        fprintf(stderr, "%s: uv_fs_read: %s\n", __func__, uv_strerror((int)req->result));
        goto cleanup;
    }
    xt_io_descr = req->data;

    header = xt_io_descr->buf;
    payload_length = header->payload_length;
    count = header->number_of_pages;

    payload_offset = sizeof(*header) + sizeof(header->descr[0]) * count;

    trailer = xt_io_descr->buf + xt_io_descr->bytes - sizeof(*trailer);
    crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, xt_io_descr->buf, xt_io_descr->bytes - sizeof(*trailer));
    ret = crc32cmp(trailer->checksum, crc);
    fprintf(stderr, "%s: Extent was read from disk. CRC32 check: %s\n", __func__, ret ? "FAILED" : "SUCCEEDED");
    if (unlikely(ret)) {
        /* TODO: handle errors */
        goto cleanup;
    }

    if (RRD_NO_COMPRESSION != header->compression_algorithm) {
        uncompressed_payload_length = 0;
        for (i = 0 ; i < count ; ++i) {
            uncompressed_payload_length += header->descr[i].page_length;
        }
        uncompressed_buf = malloc(uncompressed_payload_length);
        if (!uncompressed_buf) {
            fprintf(stderr, "malloc failed.\n");
            exit(1);
        }
        ret = LZ4_decompress_safe(xt_io_descr->buf + payload_offset, uncompressed_buf,
                                  payload_length, uncompressed_payload_length);
        fprintf(stderr, "LZ4 decompressed %d bytes to %d bytes.\n", payload_length, ret);
        /* care, we don't hold the descriptor mutex */
    }

    ret = posix_memalign(&page, RRDFILE_ALIGNMENT, RRDENG_BLOCK_SIZE);
    if (unlikely(ret)) {
        fprintf(stderr, "posix_memalign:%s\n", strerror(ret));
        exit(UV_ENOMEM);
    }
    for (i = 0 ; i < xt_io_descr->descr_count; ++i) {
        descr = xt_io_descr->descr_array[i];
        for (j = 0, page_offset = 0; j < count; ++j) {
            /* care, we don't hold the descriptor mutex */
            if (!uuid_compare(*(uuid_t *) header->descr[j].uuid, *descr->id) &&
                header->descr[j].page_length == descr->page_length &&
                header->descr[j].start_time == descr->start_time &&
                header->descr[j].end_time == descr->end_time) {
                break;
            }
            page_offset += header->descr[j].page_length;
        }
        /* care, we don't hold the descriptor mutex */
        if (RRD_NO_COMPRESSION == header->compression_algorithm) {
            (void) memcpy(page, xt_io_descr->buf + payload_offset + page_offset, descr->page_length);
        } else {
            (void) memcpy(page, uncompressed_buf + page_offset, descr->page_length);
        }
        uv_mutex_lock(&descr->mutex);
        descr->page = page;
        descr->flags |= RRD_PAGE_POPULATED;
        descr->flags &= ~RRD_PAGE_READ_PENDING;
        if (xt_io_descr->release_descr)
            pg_cache_put_unsafe(descr);
        uv_mutex_unlock(&descr->mutex);

        fprintf(stderr, "%s: Waking up waiters.\n", __func__);
        /* wake up waiters */
        uv_cond_broadcast(&descr->cond);
    }
    if (RRD_NO_COMPRESSION != header->compression_algorithm) {
        free(uncompressed_buf);
    }
    if (xt_io_descr->completion)
        complete(xt_io_descr->completion);
cleanup:
    free(xt_io_descr->buf);
    free(xt_io_descr);
    uv_fs_req_cleanup(req);
}


static void do_read_extent(struct rrdengine_worker_config* wc,
                           struct rrdeng_page_cache_descr **descr,
                           unsigned count,
                           uint8_t release_descr)
{
    int i, ret;
    unsigned size_bytes, pos;
    uint32_t payload_length;
    struct rrdeng_page_cache_descr *eligible_pages[MAX_PAGES_PER_EXTENT];
    struct extent_io_descriptor *xt_io_descr;

    pos = descr[0]->extent->offset;
    size_bytes = descr[0]->extent->size;

    xt_io_descr = malloc(sizeof(*xt_io_descr));
    if (unlikely(NULL == xt_io_descr)) {
        fprintf(stderr, "%s: malloc failed.\n", __func__);
        return;
    }
    ret = posix_memalign((void *)&xt_io_descr->buf, RRDFILE_ALIGNMENT, ALIGN_BYTES_CEILING(size_bytes));
    if (unlikely(ret)) {
        fprintf(stderr, "posix_memalign:%s\n", strerror(ret));
        free(xt_io_descr);
        return;
    }
    for (i = 0 ; i < count; ++i) {
        uv_mutex_lock(&descr[i]->mutex);
        descr[i]->flags |= RRD_PAGE_READ_PENDING;
        payload_length = descr[i]->page_length;
        uv_mutex_unlock(&descr[i]->mutex);

        xt_io_descr->descr_array[i] = descr[i];
    }
    xt_io_descr->descr_count = count;
    xt_io_descr->bytes = size_bytes;
    xt_io_descr->pos = pos;
    xt_io_descr->req.data = xt_io_descr;
    xt_io_descr->completion = NULL;
    /* xt_io_descr->descr_commit_idx_array[0] */
    xt_io_descr->release_descr = release_descr;

    xt_io_descr->iov = uv_buf_init((void *)xt_io_descr->buf, ALIGN_BYTES_CEILING(size_bytes));
    ret = uv_fs_read(wc->loop, &xt_io_descr->req, datafile.file, &xt_io_descr->iov, 1, pos, read_extent_cb);
    assert (-1 != ret);
}

static void commit_metric_data_extent(struct rrdengine_worker_config* wc, struct extent_io_descriptor *xt_io_descr)
{
    unsigned count, payload_length, descr_size, size_bytes;
    void *buf;
    /* persistent structures */
    struct rrdeng_df_extent_header *df_header;
    struct rrdeng_jf_transaction_header *jf_header;
    struct rrdeng_jf_store_metric_data *jf_metric_data;
    struct rrdeng_jf_transaction_trailer *jf_trailer;
    uLong crc;

    df_header = xt_io_descr->buf;
    count = df_header->number_of_pages;
    descr_size = sizeof(*jf_metric_data->descr) * count;
    payload_length = sizeof(*jf_metric_data) + descr_size;
    size_bytes = sizeof(*jf_header) + payload_length + sizeof(*jf_trailer);

    buf = wal_get_transaction_buffer(wc, size_bytes);

    jf_header = buf;
    jf_header->type = STORE_METRIC_DATA;
    jf_header->reserved = 0;
    jf_header->id = commit_log.transaction_id++;
    jf_header->payload_length = payload_length;

    jf_metric_data = buf + sizeof(*jf_header);
    jf_metric_data->extent_offset = xt_io_descr->pos;
    jf_metric_data->extent_size = xt_io_descr->bytes;
    jf_metric_data->number_of_pages = count;
    memcpy(jf_metric_data->descr, df_header->descr, descr_size);

    jf_trailer = buf + sizeof(*jf_header) + payload_length;
    crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, buf, sizeof(*jf_header) + payload_length);
    crc32set(jf_trailer->checksum, crc);
}

static void do_commit_transaction(struct rrdengine_worker_config* wc, uint8_t type, void *data)
{
    switch (type) {
    case STORE_METRIC_DATA:
        commit_metric_data_extent(wc, (struct extent_io_descriptor *)data);
        break;
    default:
        assert(type == STORE_METRIC_DATA);
        break;
    }
}

void flush_pages_cb(uv_fs_t* req)
{
    struct extent_io_descriptor *xt_io_descr;
    struct rrdeng_page_cache_descr *descr;
    int i;
    unsigned count, commit_idx;
    struct rrdengine_worker_config* wc = req->loop->data;

    fprintf(stderr, "%s: Extent was written to disk. Waking up waiters.\n", __func__);
    if (req->result < 0) {
        fprintf(stderr, "%s: uv_fs_write: %s\n", __func__, uv_strerror((int)req->result));
        goto cleanup;
    }
    xt_io_descr = req->data;

    count = xt_io_descr->descr_count;
    for (i = 0 ; i < count ; ++i) {
        /* care, we don't hold the descriptor mutex */
        descr = xt_io_descr->descr_array[i];

        uv_rwlock_wrlock(&pg_cache.commited_pages_rwlock);

        uv_mutex_lock(&descr->mutex);
        descr->flags &= ~(RRD_PAGE_DIRTY | RRD_PAGE_WRITE_PENDING);
        uv_mutex_unlock(&descr->mutex);

        commit_idx = xt_io_descr->descr_commit_idx_array[i];
        pg_cache.commited_pages[commit_idx] = NULL;
        --pg_cache.nr_commited_pages;
        uv_rwlock_wrunlock(&pg_cache.commited_pages_rwlock);

        /* wake up waiters */
        uv_cond_broadcast(&descr->cond);
    }
    if (xt_io_descr->completion)
        complete(xt_io_descr->completion);
    do_commit_transaction(wc, STORE_METRIC_DATA, xt_io_descr);
cleanup:
    free(xt_io_descr->buf);
    free(xt_io_descr);
    uv_fs_req_cleanup(req);
}

/*
 * completion must be NULL or valid.
 * Returns 0 when no flushing can take place.
 * Returns 1 on successful flushing initiation.
 */
static int do_flush_pages(struct rrdengine_worker_config* wc, int force, struct completion *completion)
{
    int i, ret, compressed_size, max_compressed_size;
    unsigned count, size_bytes, pos;
    uint32_t uncompressed_payload_length, payload_offset;
    struct rrdeng_page_cache_descr *descr, *eligible_pages[MAX_PAGES_PER_EXTENT];
    struct extent_io_descriptor *xt_io_descr;
    void *compressed_buf;
    unsigned descr_commit_idx_array[MAX_PAGES_PER_EXTENT];
    uint8_t compression_algorithm = global_compress_alg;
    struct extent_info *extent;
    /* persistent structures */
    struct rrdeng_df_extent_header *header;
    struct rrdeng_df_extent_trailer *trailer;
    uLong crc;

    if (force)
        fprintf(stderr, "Asynchronous flushing of extent has been forced by page pressure.\n");

    uv_rwlock_rdlock(&pg_cache.commited_pages_rwlock);
    for (i = 0, count = 0, uncompressed_payload_length = 0 ;
         i < PAGE_CACHE_MAX_COMMITED_PAGES && count != MAX_PAGES_PER_EXTENT ;
         ++i) {
        if (NULL == (descr = pg_cache.commited_pages[i]))
            continue;
        uv_mutex_lock(&descr->mutex);
        if (!(descr->flags & RRD_PAGE_WRITE_PENDING)) {
            descr->flags |= RRD_PAGE_WRITE_PENDING;
            uncompressed_payload_length += descr->page_length;
            descr_commit_idx_array[count] = i;
            eligible_pages[count++] = descr;
        }
        uv_mutex_unlock(&descr->mutex);
    }
    uv_rwlock_rdunlock(&pg_cache.commited_pages_rwlock);

    if (!count) {
        fprintf(stderr, "%s: no pages eligible for flushing.\n", __func__);
        return 0;
    }
    xt_io_descr = malloc(sizeof(*xt_io_descr));
    if (unlikely(NULL == xt_io_descr)) {
        fprintf(stderr, "%s: malloc failed.\n", __func__);
        return 0;
    }
    payload_offset = sizeof(*header) + count * sizeof(header->descr[0]);
    switch (compression_algorithm) {
    case RRD_NO_COMPRESSION:
        size_bytes = payload_offset + uncompressed_payload_length + sizeof(*trailer);
        break;
    default: /* Compress */
        assert(uncompressed_payload_length < LZ4_MAX_INPUT_SIZE);
        max_compressed_size = LZ4_compressBound(uncompressed_payload_length);
        compressed_buf = malloc(max_compressed_size);
        if (!compressed_buf) {
            printf("malloc failed.\n");
            exit(1);
        }
        size_bytes = payload_offset + MAX(uncompressed_payload_length, max_compressed_size) + sizeof(*trailer);
        break;
    }
    ret = posix_memalign((void *)&xt_io_descr->buf, RRDFILE_ALIGNMENT, ALIGN_BYTES_CEILING(size_bytes));
    if (unlikely(ret)) {
        fprintf(stderr, "posix_memalign:%s\n", strerror(ret));
        free(xt_io_descr);
        return 0;
    }
    (void) memcpy(xt_io_descr->descr_array, eligible_pages, sizeof(struct rrdeng_page_cache_descr *) * count);
    xt_io_descr->descr_count = count;

    pos = 0;
    header = xt_io_descr->buf;
    header->compression_algorithm = compression_algorithm;
    header->number_of_pages = count;
    pos += sizeof(*header);

    extent = malloc(sizeof(*extent) + count * sizeof(extent->pages[0]));
    if (unlikely(NULL == extent)) {
        fprintf(stderr, "malloc failed.\n");
        exit(UV_ENOMEM);
    }
    extent->size = size_bytes;
    extent->offset = datafile.pos;
    extent->number_of_pages = count;

    for (i = 0 ; i < count ; ++i) {
        /* This is here for performance reasons */
        xt_io_descr->descr_commit_idx_array[i] = descr_commit_idx_array[i];

        descr = xt_io_descr->descr_array[i];
        uuid_copy(*(uuid_t *)header->descr[i].uuid, *descr->id);
        header->descr[i].page_length = descr->page_length;
        header->descr[i].start_time = descr->start_time;
        header->descr[i].end_time = descr->end_time;
        pos += sizeof(header->descr[i]);
    }
    for (i = 0 ; i < count ; ++i) {
        descr = xt_io_descr->descr_array[i];
        /* care, we don't hold the descriptor mutex */
        (void) memcpy(xt_io_descr->buf + pos, descr->page, descr->page_length);
        descr->extent = extent;
        extent->pages[i] = descr;

        pos += descr->page_length;
    }
    switch (compression_algorithm) {
    case RRD_NO_COMPRESSION:
        header->payload_length = uncompressed_payload_length;
        break;
    default: /* Compress */
        compressed_size = LZ4_compress_default(xt_io_descr->buf + payload_offset, compressed_buf,
                                               uncompressed_payload_length, max_compressed_size);
        fprintf(stderr, "LZ4 compressed %"PRIu32" bytes to %d bytes.\n", uncompressed_payload_length, compressed_size);
        (void) memcpy(xt_io_descr->buf + payload_offset, compressed_buf, compressed_size);
        free(compressed_buf);
        size_bytes = payload_offset + compressed_size + sizeof(*trailer);
        header->payload_length = compressed_size;
        break;
    }
    xt_io_descr->bytes = size_bytes;
    xt_io_descr->pos = datafile.pos;
    xt_io_descr->req.data = xt_io_descr;
    xt_io_descr->completion = completion;

    trailer = xt_io_descr->buf + size_bytes - sizeof(*trailer);
    crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, xt_io_descr->buf, size_bytes - sizeof(*trailer));
    crc32set(trailer->checksum, crc);

    xt_io_descr->iov = uv_buf_init((void *)xt_io_descr->buf, ALIGN_BYTES_CEILING(size_bytes));
    ret = uv_fs_write(wc->loop, &xt_io_descr->req, datafile.file, &xt_io_descr->iov, 1, datafile.pos, flush_pages_cb);
    assert (-1 != ret);
    datafile.pos += ALIGN_BYTES_CEILING(size_bytes);

    return 1;
}

int init_rrd_files(void)
{
    int ret;

    ret = init_data_files();
    if (ret)
        return ret;

    ret = init_journal_files(datafile.pos);

    return ret;
}

void rrdeng_init_cmd_queue(struct rrdengine_worker_config* wc)
{
    wc->cmd_queue.head = wc->cmd_queue.tail = 0;
    wc->queue_size = 0;
    assert(0 == uv_cond_init(&wc->cmd_cond));
    assert(0 == uv_mutex_init(&wc->cmd_mutex));
}

void rrdeng_enq_cmd(struct rrdengine_worker_config* wc, struct rrdeng_cmd *cmd)
{
    unsigned queue_size;

    /* wait for free space in queue */
    uv_mutex_lock(&wc->cmd_mutex);
    while ((queue_size = wc->queue_size) == RRDENG_CMD_Q_MAX_SIZE) {
        uv_cond_wait(&wc->cmd_cond, &wc->cmd_mutex);
    }
    assert(queue_size < RRDENG_CMD_Q_MAX_SIZE);
    /* enqueue command */
    wc->cmd_queue.cmd_array[wc->cmd_queue.tail] = *cmd;
    wc->cmd_queue.tail = wc->cmd_queue.tail != RRDENG_CMD_Q_MAX_SIZE - 1 ?
                         wc->cmd_queue.tail + 1 : 0;
    wc->queue_size = queue_size + 1;
    uv_mutex_unlock(&wc->cmd_mutex);

    /* wake up event loop */
    assert(0 == uv_async_send(&wc->async));
}

struct rrdeng_cmd rrdeng_deq_cmd(struct rrdengine_worker_config* wc)
{
    struct rrdeng_cmd ret;
    unsigned queue_size;

    uv_mutex_lock(&wc->cmd_mutex);
    queue_size = wc->queue_size;
    if (queue_size == 0) {
        ret.opcode = RRDENG_NOOP;
    } else {
        /* dequeue command */
        ret = wc->cmd_queue.cmd_array[wc->cmd_queue.head];
        if (queue_size == 1) {
            wc->cmd_queue.head = wc->cmd_queue.tail = 0;
        } else {
            wc->cmd_queue.head = wc->cmd_queue.head != RRDENG_CMD_Q_MAX_SIZE - 1 ?
                                 wc->cmd_queue.head + 1 : 0;
        }
        wc->queue_size = queue_size - 1;

        /* wake up producers */
        uv_cond_signal(&wc->cmd_cond);
    }
    uv_mutex_unlock(&wc->cmd_mutex);

    return ret;
}

void async_cb(uv_async_t *handle)
{
    uv_stop(handle->loop);
    uv_update_time(handle->loop);
    fprintf(stderr, "%s called, active=%d.\n", __func__, uv_is_active((uv_handle_t *)handle));
}

void timer_cb(uv_timer_t* handle)
{
    struct rrdengine_worker_config* wc = handle->data;

    uv_stop(handle->loop);
    uv_update_time(handle->loop);
    fprintf(stderr, "%s: timeout reached, flushing pages to disk.\n", __func__);
    (void) do_flush_pages(wc, 0, NULL);
    fprintf(stderr, "Page Statistics: total=%u, populated=%u, commited=%u\n",
            pg_cache.pages, pg_cache.populated_pages, pg_cache.nr_commited_pages);
}

/* Flushes dirty pages when timer expires */
#define TIMER_PERIOD_MS (10000)

#define CMD_BATCH_SIZE (256)

void rrdeng_worker(void* arg)
{
    struct rrdengine_worker_config* wc = arg;
    uv_loop_t* loop;
    int shutdown, fd, error;
    enum rrdeng_opcode opcode;
    uv_timer_t timer_req;
    struct rrdeng_cmd cmd;
    unsigned current_cmd_batch_size;

    rrdeng_init_cmd_queue(wc);

    loop = wc->loop = uv_default_loop();
    loop->data = wc;

    uv_async_init(wc->loop, &wc->async, async_cb);
    wc->async.data = wc;

    /* dirty page flushing timer */
    uv_timer_init(loop, &timer_req);
    timer_req.data = wc;

    /* wake up initialization thread */
    complete(&rrdengine_completion);

    uv_timer_start(&timer_req, timer_cb, TIMER_PERIOD_MS, TIMER_PERIOD_MS);
    shutdown = 0;
    while (shutdown == 0 || uv_loop_alive(loop)) {
        uv_run(loop, UV_RUN_DEFAULT);
        current_cmd_batch_size = 0;
        /* wait for commands */
        do {
            cmd = rrdeng_deq_cmd(wc);
            opcode = cmd.opcode;

            switch (opcode) {
            case RRDENG_NOOP:
                /* the command queue was empty, do nothing */
                break;
            case RRDENG_SHUTDOWN:
                shutdown = 1;
                /*
                 * uv_async_send after uv_close does not seem to crash in linux at the moment,
                 * it is however undocumented behaviour and we need to be aware if this becomes
                 * an issue in the future.
                 */
                uv_close((uv_handle_t *)&wc->async, NULL);
                assert(0 == uv_timer_stop(&timer_req));
                uv_close((uv_handle_t *)&timer_req, NULL);
                fprintf(stderr, "Shutting down RRD engine event loop.\n");
                while (do_flush_pages(wc, 1, NULL)) {
                    ; /* Force flushing of all commited pages. */
                }
                break;
            case RRDENG_READ_PAGE:
                do_read_extent(wc, &cmd.read_page.page_cache_descr, 1, 0);
                break;
            case RRDENG_READ_EXTENT:
                do_read_extent(wc, cmd.read_extent.page_cache_descr, cmd.read_extent.page_count, 1);
                break;
            case RRDENG_COMMIT_PAGE:
                do_commit_transaction(wc, STORE_METRIC_DATA, NULL);
                break;
            case RRDENG_FLUSH_PAGES:
                (void) do_flush_pages(wc, 1, cmd.completion);
                break;
            default:
                fprintf(stderr, "default.\n");
                break;
            }
        } while ((opcode != RRDENG_NOOP) && (current_cmd_batch_size  < CMD_BATCH_SIZE));
    }
    /* cleanup operations of the event loop */
    wal_flush_transaction_buffer(wc);
    uv_run(loop, UV_RUN_DEFAULT);

    /* TODO: don't let the API block by waiting to enqueue commands */
    uv_cond_destroy(&wc->cmd_cond);
/*  uv_mutex_destroy(&wc->cmd_mutex); */
}


#define NR_PAGES (256)
static void basic_functional_test(void)
{
    int i, j, failed_validations;
    usec_t now_usec;
    uuid_t uuid[NR_PAGES];
    void *buf;
    struct rrdeng_page_cache_descr *handle[NR_PAGES];
    char uuid_str[37];
    char backup[NR_PAGES][37 * 100]; /* backup storage for page data verification */

    for (i = 0 ; i < NR_PAGES ; ++i) {
        uuid_generate(uuid[i]);
        uuid_unparse_lower(uuid[i], uuid_str);
//      fprintf(stderr, "Generated uuid[%d]=%s\n", i, uuid_str);
        buf = rrdeng_create_page(&uuid[i], &handle[i]);
        /* Each page contains 10 times its own UUID stringified */
        for (j = 0 ; j < 100 ; ++j) {
            strcpy(buf + 37 * j, uuid_str);
            strcpy(backup[i] + 37 * j, uuid_str);
        }
        rrdeng_commit_page(handle[i]);
    }
    fprintf(stderr, "\n********** CREATED %d METRIC PAGES ***********\n\n", NR_PAGES);
    failed_validations = 0;
    for (i = 0 ; i < NR_PAGES ; ++i) {
        buf = rrdeng_get_latest_page(&uuid[i], &handle[i]);
        if (NULL == buf) {
            ++failed_validations;
            fprintf(stderr, "Page %d was LOST.\n", i);
        }
        if (memcmp(backup[i], buf, 37 * 100)) {
            ++failed_validations;
            fprintf(stderr, "Page %d data comparison with backup FAILED validation.\n", i);
        }
        rrdeng_put_page(handle[i]);
    }
    fprintf(stderr, "\n********** CORRECTLY VALIDATED %d/%d METRIC PAGES ***********\n\n",
            NR_PAGES - failed_validations, NR_PAGES);

}
/* C entry point for development purposes
 * make "LDFLAGS=-errdengine_main"
 */
void rrdengine_main(void)
{
    int ret, max_size, compressed_size;
    int fd, i, j;
    long alignment;
    void *block, *buf;
    struct aiocb aio_desc, *aio_descp;
    uv_file file;
    uv_fs_t req;
    uv_buf_t iov;
    static uv_loop_t* loop;
    uv_async_t async;
    char *data = "LIBUV overwrite file contents. LIBUV overwrite file contents. LIBUV overwrite file contents. LIBUV overwrite file contents. LIBUV overwrite file contents. LIBUV overwrite file contents. LIBUV overwrite file contents. LIBUV overwrite file contents. LIBUV overwrite file contents. LIBUV overwrite file contents.\n";
    uv_work_t work_req;
    const volatile int* flag;

    ret = rrdeng_init();
    if (ret) {
        exit(ret);
    }
    basic_functional_test();

    rrdeng_exit();
    fprintf(stderr, "Hello world!\n");
    exit(0);

    fd = open(DATAFILE, O_DIRECT | O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        perror("open");
        exit(fd);
    }

    alignment = fpathconf(fd, _PC_REC_XFER_ALIGN);
    if (alignment == -1) {
        perror("fpathconf");
        exit(alignment);
    }
    printf("Alignment = %ld\n", alignment);
    if (alignment > RRDENG_BLOCK_SIZE || (RRDENG_BLOCK_SIZE % alignment != 0)) {
        printf("File alignment incompatible with RRD engine block size.\n");
    }

    ret = posix_memalign(&block, alignment, RRDENG_BLOCK_SIZE);
    if (ret) {
        printf("posix_memalign:%s\n", strerror(ret));
        exit(ret);
    }
    strcpy((char *) block, "Test file contents.\n");

    memset(&aio_desc, 0, sizeof(aio_desc));
    aio_desc.aio_fildes = fd;
    aio_desc.aio_offset = 0;
    aio_desc.aio_buf = block;
    aio_desc.aio_nbytes = RRDENG_BLOCK_SIZE;

    ret = aio_write(&aio_desc);
    if (ret == -1) {
        perror("aio_write");
        exit(ret);
    }
    ret = aio_error(&aio_desc);
    switch (ret) {
    case 0:
        printf("aio_error: request completed\n");
        break;
    case EINPROGRESS:
        printf("aio_error:%s\n", strerror(ret));
        aio_descp = &aio_desc;
        ret = aio_suspend((const struct aiocb *const *)&aio_descp, 1, NULL);
        if (ret == -1) {
            /* should handle catching signals here */
            perror("aio_suspend");
            exit(ret);
        }
        break;
    case ECANCELED:
        printf("aio_error:%s\n", strerror(ret));
        break;
    default:
        printf("aio_error:%s\n", strerror(ret));
        exit(ret);
        break;
    }
    ret = aio_return(&aio_desc);
    if (ret == -1) {
        perror("aio_return");
        exit(ret);
    }

    fd = close(fd);
    if (fd == -1) {
        perror("close");
        exit(fd);
    }
    free(block);

    exit(0);
}