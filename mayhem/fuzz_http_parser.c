#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <monkey/monkey.h>
#include <monkey/mk_http_parser.h>
#include <monkey/mk_server.h>

#define MAX_SIZE 65536

/* server->workers == 0 makes mk_http_error() a no-op, as when the parser is embedded standalone */
static struct mk_server server;

/*
 * Byte 0 picks a split point: the request is parsed first with only a prefix visible, then with
 * the whole buffer, exercising the parser's resumable (PENDING) state machine.
 *
 * The parser relies on the byte after the visible data being NUL; the in-tree caller
 * (mk_http.c) allocates +1 and terminates after every read, and so does this harness.
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    int ret;
    size_t split;
    size_t len;
    char *buf;
    char *out;
    size_t out_len;
    struct mk_http_request req;
    struct mk_http_parser parser;

    if (size < 2 || size > MAX_SIZE) {
        return -1;
    }

    len = size - 1;
    split = (data[0] * len) / 255;

    buf = malloc(len + 1);
    if (!buf) {
        return -1;
    }

    memset(&req, 0, sizeof(req));
    mk_http_parser_init(&parser);

    ret = MK_HTTP_PARSER_PENDING;
    if (split > 0 && split < len) {
        memcpy(buf, data + 1, split);
        buf[split] = '\0';
        ret = mk_http_parser(&req, &parser, buf, (int) split, &server);
    }
    if (ret == MK_HTTP_PARSER_PENDING) {
        memcpy(buf, data + 1, len);
        buf[len] = '\0';
        ret = mk_http_parser(&req, &parser, buf, (int) len, &server);
    }

    if (ret == MK_HTTP_PARSER_OK) {
        (void) mk_http_parser_content_length(&parser);
        (void) mk_http_parser_request_size(&parser, buf, len);

        if (mk_http_parser_is_content_chunked(&parser)) {
            out = NULL;
            out_len = 0;
            if (mk_http_parser_chunked_decode(&parser, buf, len, &out, &out_len) == 0) {
                free(out);
            }
        }
    }

    free(buf);
    return 0;
}
