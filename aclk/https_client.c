#include "../libnetdata/libnetdata.h"

#include "https_client.h"

#include "../mqtt_websockets/c-rbuf/include/ringbuffer.h"

enum http_parse_state {
    HTTP_PARSE_INITIAL = 0,
    HTTP_PARSE_HEADERS,
    HTTP_PARSE_CONTENT
};

static const char *http_req_type_to_str(http_req_type_t req) {
    switch (req) {
        case HTTP_REQ_GET:
            return "GET";
        case HTTP_REQ_POST:
            return "POST";
        case HTTP_REQ_CONNECT:
            return "CONNECT";
        default:
            return "unknown";
    }
}

typedef struct {
    enum http_parse_state state;
    int content_length;
    int http_code;
} http_parse_ctx;

#define HTTP_PARSE_CTX_INITIALIZER { .state = HTTP_PARSE_INITIAL, .content_length = -1, .http_code = 0 }
static inline void http_parse_ctx_clear(http_parse_ctx *ctx) {
    ctx->state = HTTP_PARSE_INITIAL;
    ctx->content_length = -1;
    ctx->http_code = 0;
}

#define POLL_TO_MS 100

#define NEED_MORE_DATA  0
#define PARSE_SUCCESS   1
#define PARSE_ERROR    -1
#define HTTP_LINE_TERM "\x0D\x0A"
#define RESP_PROTO "HTTP/1.1 "
#define HTTP_KEYVAL_SEPARATOR ": "
#define HTTP_HDR_BUFFER_SIZE 256
#define PORT_STR_MAX_BYTES 7

static void process_http_hdr(http_parse_ctx *parse_ctx, const char *key, const char *val)
{
    // currently we care only about content-length
    // but in future the way this is written
    // it can be extended
    if (!strcmp("content-length", key)) {
        parse_ctx->content_length = atoi(val);
    }
}

static int parse_http_hdr(rbuf_t buf, http_parse_ctx *parse_ctx)
{
    int idx, idx_end;
    char buf_key[HTTP_HDR_BUFFER_SIZE];
    char buf_val[HTTP_HDR_BUFFER_SIZE];
    char *ptr = buf_key;
    if (!rbuf_find_bytes(buf, HTTP_LINE_TERM, strlen(HTTP_LINE_TERM), &idx_end)) {
        error("CRLF expected");
        return 1;
    }

    char *separator = rbuf_find_bytes(buf, HTTP_KEYVAL_SEPARATOR, strlen(HTTP_KEYVAL_SEPARATOR), &idx);
    if (!separator) {
        error("Missing Key/Value separator");
        return 1;
    }
    if (idx >= HTTP_HDR_BUFFER_SIZE) {
        error("Key name is too long");
        return 1;
    }

    rbuf_pop(buf, buf_key, idx);
    buf_key[idx] = 0;

    rbuf_bump_tail(buf, strlen(HTTP_KEYVAL_SEPARATOR));
    idx_end -= strlen(HTTP_KEYVAL_SEPARATOR) + idx;
    if (idx_end >= HTTP_HDR_BUFFER_SIZE) {
        error("Value of key \"%s\" too long", buf_key);
        return 1;
    }

    rbuf_pop(buf, buf_val, idx_end);
    buf_val[idx_end] = 0;

    rbuf_bump_tail(buf, strlen(HTTP_KEYVAL_SEPARATOR));

    for (ptr = buf_key; *ptr; ptr++)
        *ptr = tolower(*ptr);

    process_http_hdr(parse_ctx, buf_key, buf_val);

    return 0;
}

static int parse_http_response(rbuf_t buf, http_parse_ctx *parse_ctx)
{
    int idx;
    char rc[4];

    do {
        if (parse_ctx->state != HTTP_PARSE_CONTENT && !rbuf_find_bytes(buf, HTTP_LINE_TERM, strlen(HTTP_LINE_TERM), &idx))
            return NEED_MORE_DATA;
        switch (parse_ctx->state) {
            case HTTP_PARSE_INITIAL:
                if (rbuf_memcmp_n(buf, RESP_PROTO, strlen(RESP_PROTO))) {
                    error("Expected response to start with \"%s\"", RESP_PROTO);
                    return PARSE_ERROR;
                }
                rbuf_bump_tail(buf, strlen(RESP_PROTO));
                if (rbuf_pop(buf, rc, 4) != 4) {
                    error("Expected HTTP status code");
                    return PARSE_ERROR;
                }
                if (rc[3] != ' ') {
                    error("Expected space after HTTP return code");
                    return PARSE_ERROR;
                }
                rc[3] = 0;
                parse_ctx->http_code = atoi(rc);
                if (parse_ctx->http_code < 100 || parse_ctx->http_code >= 600) {
                    error("HTTP code not in range 100 to 599");
                    return PARSE_ERROR;
                }

                rbuf_find_bytes(buf, HTTP_LINE_TERM, strlen(HTTP_LINE_TERM), &idx);

                rbuf_bump_tail(buf, idx + strlen(HTTP_LINE_TERM));

                parse_ctx->state = HTTP_PARSE_HEADERS;
                break;
            case HTTP_PARSE_HEADERS:
                if (!idx) {
                    parse_ctx->state = HTTP_PARSE_CONTENT;
                    rbuf_bump_tail(buf, strlen(HTTP_LINE_TERM));
                    break;
                }
                if (parse_http_hdr(buf, parse_ctx))
                    return PARSE_ERROR;
                rbuf_find_bytes(buf, HTTP_LINE_TERM, strlen(HTTP_LINE_TERM), &idx);
                rbuf_bump_tail(buf, idx + strlen(HTTP_LINE_TERM));
                break;
            case HTTP_PARSE_CONTENT:
                // replies like CONNECT etc. do not have content
                if (parse_ctx->content_length < 0)
                    return PARSE_SUCCESS;

                if (rbuf_bytes_available(buf) >= (size_t)parse_ctx->content_length)
                    return PARSE_SUCCESS;
                return NEED_MORE_DATA;
        }
    } while(1);
}

typedef struct https_req_ctx {
    https_req_t *request;

    int sock;
    rbuf_t buf_rx;

    struct pollfd poll_fd;

    SSL_CTX *ssl_ctx;
    SSL *ssl;

    size_t written;

    int self_signed_allowed;

    http_parse_ctx parse_ctx;

    time_t req_start_time;
} https_req_ctx_t;

static int https_req_check_timedout(https_req_ctx_t *ctx) {
    if (now_realtime_sec() > ctx->req_start_time + ctx->request->timeout_s) {
        error("request timed out");
        return 1;
    }
    return 0;
}

static char *_ssl_err_tos(int err)
{
    switch(err){
        case SSL_ERROR_SSL:
            return "SSL_ERROR_SSL";
        case SSL_ERROR_WANT_READ:
            return "SSL_ERROR_WANT_READ";
        case SSL_ERROR_WANT_WRITE:
            return "SSL_ERROR_WANT_WRITE";
        case SSL_ERROR_NONE:
            return "SSL_ERROR_NONE";
        case SSL_ERROR_ZERO_RETURN:
            return "SSL_ERROR_ZERO_RETURN";
        case SSL_ERROR_WANT_CONNECT:
            return "SSL_ERROR_WANT_CONNECT";
        case SSL_ERROR_WANT_ACCEPT:
            return "SSL_ERROR_WANT_ACCEPT";
    }
    return "Unknown!!!";
}

static int socket_write_all(https_req_ctx_t *ctx, char *data, size_t data_len) {
    int ret;
    ctx->written = 0;
    ctx->poll_fd.events = POLLOUT;

    do {
        ret = poll(&ctx->poll_fd, 1, POLL_TO_MS);
        if (ret < 0) {
            error("poll error");
            return 1;
        }
        if (ret == 0) {
            if (https_req_check_timedout(ctx)) {
                error("Poll timed out");
                return 2;
            }
            continue;
        }

        ret = write(ctx->sock, &data[ctx->written], data_len - ctx->written);
        if (ret > 0) {
            ctx->written += ret;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            error("Error writing to socket");
            return 3;
        }
    } while (ctx->written < data_len);

    return 0;
}

static int ssl_write_all(https_req_ctx_t *ctx, char *data, size_t data_len) {
    int ret;
    ctx->written = 0;
    ctx->poll_fd.events |= POLLOUT;

    do {
        ret = poll(&ctx->poll_fd, 1, POLL_TO_MS);
        if (ret < 0) {
            error("poll error");
            return 1;
        }
        if (ret == 0) {
            if (https_req_check_timedout(ctx)) {
                error("Poll timed out");
                return 2;
            }
            continue;
        }
        ctx->poll_fd.events = 0;

        ret = SSL_write(ctx->ssl, &data[ctx->written], data_len - ctx->written);
        if (ret > 0) {
            ctx->written += ret;
        } else {
            ret = SSL_get_error(ctx->ssl, ret);
            switch (ret) {
                case SSL_ERROR_WANT_READ:
                    ctx->poll_fd.events |= POLLIN;
                    break;
                case SSL_ERROR_WANT_WRITE:
                    ctx->poll_fd.events |= POLLOUT;
                    break;
                default:
                    error("SSL_write Err: %s", _ssl_err_tos(ret));
                    return 3;
            }
        }
    } while (ctx->written < data_len);

    return 0;
}

static inline int https_client_write_all(https_req_ctx_t *ctx, char *data, size_t data_len) {
    if (ctx->ssl_ctx)
        return ssl_write_all(ctx, data, data_len);
    return socket_write_all(ctx, data, data_len);
}

static int read_parse_response(https_req_ctx_t *ctx) {
    int ret;
    char *ptr;
    size_t size;

    ctx->poll_fd.events = POLLIN;
    do {
        ret = poll(&ctx->poll_fd, 1, POLL_TO_MS);
        if (ret < 0) {
            error("poll error");
            return 1;
        }
        if (ret == 0) {
            if (https_req_check_timedout(ctx)) {
                error("Poll timed out");
                return 2;
            }
            continue;
        }
        ctx->poll_fd.events = 0;

        ptr = rbuf_get_linear_insert_range(ctx->buf_rx, &size);

        if (ctx->ssl_ctx)
            ret = SSL_read(ctx->ssl, ptr, size);
        else
            ret = read(ctx->sock, ptr, size);

        if (ret > 0) {
            rbuf_bump_head(ctx->buf_rx, ret);
        } else {
            if (ctx->ssl_ctx) {
                ret = SSL_get_error(ctx->ssl, ret);
                switch (ret) {
                    case SSL_ERROR_WANT_READ:
                        ctx->poll_fd.events |= POLLIN;
                        break;
                    case SSL_ERROR_WANT_WRITE:
                        ctx->poll_fd.events |= POLLOUT;
                        break;
                    default:
                        error("SSL_read Err: %s", _ssl_err_tos(ret));
                        return 3;
                }
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    error("write error");
                    return 3;
                }
                ctx->poll_fd.events |= POLLIN;
            }
        }
    } while (!(ret = parse_http_response(ctx->buf_rx, &ctx->parse_ctx)));

    if (ret != PARSE_SUCCESS) {
        error("Error parsing HTTP response");
        return 1;
    }

    return 0;
}

#define TX_BUFFER_SIZE 8192
#define RX_BUFFER_SIZE (TX_BUFFER_SIZE*2)
static int handle_http_request(https_req_ctx_t *ctx) {
    BUFFER *hdr = buffer_create(TX_BUFFER_SIZE);
    int rc = 0;

    http_parse_ctx_clear(&ctx->parse_ctx);

    // Prepare data to send
    switch (ctx->request->request_type) {
        case HTTP_REQ_CONNECT:
            buffer_strcat(hdr, "CONNECT ");
            break;
        case HTTP_REQ_GET:
            buffer_strcat(hdr, "GET ");
            break;
        case HTTP_REQ_POST:
            buffer_strcat(hdr, "POST ");
            break;
        default:
            error("Unknown HTTPS request type!");
            rc = 1;
            goto err_exit;
    }

    if (ctx->request->request_type == HTTP_REQ_CONNECT) {
        buffer_strcat(hdr, ctx->request->host);
        buffer_sprintf(hdr, ":%d", ctx->request->port);
    } else {
        buffer_strcat(hdr, ctx->request->url);
    }

    buffer_strcat(hdr, " HTTP/1.1\x0D\x0A");

    //TODO Headers!
    if (ctx->request->request_type != HTTP_REQ_CONNECT) {
        buffer_sprintf(hdr, "Host: %s\x0D\x0A", ctx->request->host);
    }
    buffer_strcat(hdr, "User-Agent: Netdata/rocks newhttpclient\x0D\x0A");

    if (ctx->request->request_type == HTTP_REQ_POST && ctx->request->payload && ctx->request->payload_size) {
        buffer_sprintf(hdr, "Content-Length: %zu\x0D\x0A", ctx->request->payload_size);
    }

    buffer_strcat(hdr, "\x0D\x0A");

    // Send the request
    if (https_client_write_all(ctx, hdr->buffer, hdr->len)) {
        error("Couldn't write HTTP request header into SSL connection");
        rc = 2;
        goto err_exit;
    }

    if (ctx->request->request_type == HTTP_REQ_POST && ctx->request->payload && ctx->request->payload_size) {
        if (https_client_write_all(ctx, ctx->request->payload, ctx->request->payload_size)) {
            error("Couldn't write payload into SSL connection");
            rc = 3;
            goto err_exit;
        }
    }

    // Read The Response
    if (read_parse_response(ctx)) {
        error("Error reading or parsing response from server");
        rc = 4;
        goto err_exit;
    }

err_exit:
    buffer_free(hdr);
    return rc;
}

// TODO remove
int https_request(http_req_type_t method, char *host, int port, char *url, char *b, size_t b_size, char *payload) {
    UNUSED(method);
    UNUSED(host);
    UNUSED(port);
    UNUSED(url);
    UNUSED(b);
    UNUSED(b_size);
    UNUSED(payload);
    return 0;
}

int https_request_v2(https_req_t *request, https_req_response_t *response) {
    int rc = 0,ret;
    char connect_port_str[PORT_STR_MAX_BYTES];

    const char *connect_host = request->proxy_host ? request->proxy_host : request->host;
    int connect_port = request->proxy_host ? request->proxy_port : request->port;
    struct timeval timeout = { .tv_sec = request->timeout_s, .tv_usec = 0 };

    https_req_ctx_t *ctx = callocz(1, sizeof(https_req_ctx_t));
    ctx->req_start_time = now_realtime_sec();

    ctx->buf_rx = rbuf_create(RX_BUFFER_SIZE);
    if (!ctx->buf_rx) {
        error("Couldn't allocate buffer for RX data");
        rc = 20;
        goto exit_req_ctx;
    }

    snprintf(connect_port_str, PORT_STR_MAX_BYTES, "%d", connect_port);

    ctx->sock = connect_to_this_ip46(IPPROTO_TCP, SOCK_STREAM, connect_host, 0, connect_port_str, &timeout);
    if (ctx->sock < 0) {
        error("Error connecting TCP socket to \"%s\"", connect_host);
        rc = 30;
        goto exit_buf_rx;
    }

    if (fcntl(ctx->sock, F_SETFL, fcntl(ctx->sock, F_GETFL, 0) | O_NONBLOCK) == -1) {
        error("Error setting O_NONBLOCK to TCP socket.");
        rc = 40;
        goto exit_sock;
    }

    ctx->poll_fd.fd = ctx->sock;

    if (request->proxy_host) {
        https_req_t req = HTTPS_REQ_T_INITIALIZER;
        req.request_type = HTTP_REQ_CONNECT;
        req.timeout_s = request->timeout_s;
        req.host = request->host;
        req.port = request->port;
        req.url = request->url;
        ctx->request = &req;
        if (handle_http_request(ctx)) {
            error("Failed to CONNECT with proxy");
            rc = 45;
            goto exit_sock;
        }
        if (ctx->parse_ctx.http_code != 200) {
            error("Proxy didn't return 200 OK (got %d)", ctx->parse_ctx.http_code);
            rc = 46;
            goto exit_sock;
        }
        info("Proxy accepted CONNECT upgrade");
    }
    ctx->request = request;

    ctx->ssl_ctx = security_initialize_openssl_client();
    if (ctx->ssl_ctx==NULL) {
        error("Cannot allocate SSL context");
        rc = 50;
        goto exit_sock;
    }

    ctx->ssl = SSL_new(ctx->ssl_ctx);
    if (ctx->ssl==NULL) {
        error("Cannot allocate SSL");
        rc = 60;
        goto exit_CTX;
    }

    SSL_set_fd(ctx->ssl, ctx->sock);
    ret = SSL_connect(ctx->ssl);
    if (ret != -1 && ret != 1) {
        error("SSL could not connect");
        rc = 70;
        goto exit_SSL;
    }
    if (ret == -1) {
        // expected as underlying socket is non blocking!
        // consult SSL_connect documentation for details
        int ec = SSL_get_error(ctx->ssl, ret);
        if (ec != SSL_ERROR_WANT_READ && ec != SSL_ERROR_WANT_WRITE) {
            error("Failed to start SSL connection");
            rc = 75;
            goto exit_SSL;
        }
    }


    // TODO actual request here
    if (handle_http_request(ctx)) {
        error("Couldn't process request");
        rc = 90;
        goto exit_SSL;
    }
    response->http_code = ctx->parse_ctx.http_code;
    if (ctx->parse_ctx.content_length > 0) {
        response->payload_size = ctx->parse_ctx.content_length;
        response->payload = mallocz(response->payload_size);
        ret = rbuf_pop(ctx->buf_rx, response->payload, response->payload_size);
        if (ret != (int)response->payload_size) {
            error("Payload size doesn't match remaining data on the buffer!");
            response->payload_size = ret;
        }
    }
    info("HTTPS \"%s\" request to \"%s\" finished with HTTP code: %d", http_req_type_to_str(ctx->request->request_type), ctx->request->host, response->http_code);

exit_SSL:
    SSL_free(ctx->ssl);
exit_CTX:
    SSL_CTX_free(ctx->ssl_ctx);
exit_sock:
    close(ctx->sock);
exit_buf_rx:
    rbuf_free(ctx->buf_rx);
exit_req_ctx:
    freez(ctx);
    return rc;
}

void https_req_response_free(https_req_response_t *res) {
    freez(res->payload);
}
