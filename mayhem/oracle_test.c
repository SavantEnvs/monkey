/* Known-answer tests for monkey's HTTP parser, config reader and string/URL/date helpers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <monkey/monkey.h>
#include <monkey/mk_http_parser.h>
#include <monkey/mk_server.h>
#include <monkey/mk_utils.h>
#include <mk_core/mk_rconf.h>
#include <mk_core/mk_string.h>

static int failures;

static void check(const char *name, int ok)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        failures++;
    }
}

static int ptr_eq(mk_ptr_t p, const char *s)
{
    return p.data && p.len == strlen(s) && memcmp(p.data, s, p.len) == 0;
}

static struct mk_server server;

static int parse(const char *text, struct mk_http_request *req, struct mk_http_parser *p, char **out_buf)
{
    char *buf = strdup(text);

    memset(req, 0, sizeof(*req));
    mk_http_parser_init(p);
    *out_buf = buf;
    return mk_http_parser(req, p, buf, (int) strlen(buf), &server);
}

static void test_get(void)
{
    char *buf;
    struct mk_http_request req;
    struct mk_http_parser p;
    int ret = parse("GET /index.html?a=1&b=2 HTTP/1.1\r\n"
                    "Host: example.com:8080\r\n"
                    "Connection: keep-alive\r\n"
                    "User-Agent: oracle\r\n\r\n", &req, &p, &buf);

    check("get_ok", ret == MK_HTTP_PARSER_OK);
    check("get_method", req.method == MK_METHOD_GET);
    check("get_uri", ptr_eq(req.uri, "/index.html"));
    check("get_query", ptr_eq(req.query_string, "a=1&b=2"));
    check("get_protocol", req.protocol == MK_HTTP_PROTOCOL_11);
    check("get_host", ptr_eq(p.headers[MK_HEADER_HOST].val, "example.com"));
    check("get_host_port", p.header_host_port == 8080);
    check("get_user_agent", ptr_eq(p.headers[MK_HEADER_USER_AGENT].val, "oracle"));
    check("get_connection", p.header_connection == MK_HTTP_PARSER_CONN_KA);
    free(buf);
}

static void test_protocol_and_methods(void)
{
    char *buf;
    struct mk_http_request req;
    struct mk_http_parser p;

    check("http10_ok", parse("HEAD /x HTTP/1.0\r\nHost: a\r\n\r\n", &req, &p, &buf) == MK_HTTP_PARSER_OK);
    check("http10_protocol", req.protocol == MK_HTTP_PROTOCOL_10);
    check("head_method", req.method == MK_METHOD_HEAD);
    free(buf);

    check("delete_ok", parse("DELETE /x HTTP/1.1\r\nHost: a\r\n\r\n", &req, &p, &buf) == MK_HTTP_PARSER_OK);
    check("delete_method", req.method == MK_METHOD_DELETE);
    free(buf);
}

static void test_post_content_length(void)
{
    char *buf;
    struct mk_http_request req;
    struct mk_http_parser p;
    int ret = parse("POST /submit HTTP/1.1\r\n"
                    "Host: a\r\n"
                    "Content-Length: 5\r\n\r\n"
                    "hello", &req, &p, &buf);

    check("post_ok", ret == MK_HTTP_PARSER_OK);
    check("post_method", req.method == MK_METHOD_POST);
    check("post_content_length", mk_http_parser_content_length(&p) == 5);
    check("post_not_chunked", !mk_http_parser_is_content_chunked(&p));
    check("post_body", ptr_eq(req.data, "hello"));
    free(buf);

    ret = parse("POST /submit HTTP/1.1\r\nHost: a\r\nContent-Length: 10\r\n\r\nhello", &req, &p, &buf);
    check("post_short_body_pending", ret == MK_HTTP_PARSER_PENDING);
    free(buf);
}

static void test_chunked(void)
{
    char *buf;
    char *out = NULL;
    size_t out_len = 0;
    struct mk_http_request req;
    struct mk_http_parser p;
    int ret = parse("POST /up HTTP/1.1\r\n"
                    "Host: a\r\n"
                    "Transfer-Encoding: chunked\r\n\r\n"
                    "4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n", &req, &p, &buf);

    check("chunked_ok", ret == MK_HTTP_PARSER_OK);
    check("chunked_flag", mk_http_parser_is_content_chunked(&p) != 0);
    check("chunked_length", mk_http_parser_content_length(&p) == 9);
    ret = mk_http_parser_chunked_decode(&p, buf, strlen(buf), &out, &out_len);
    check("chunked_decode_ok", ret == 0);
    check("chunked_decode_body", out && out_len == 9 && memcmp(out, "Wikipedia", 9) == 0);
    free(out);
    free(buf);

    ret = parse("POST /up HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWi", &req, &p, &buf);
    check("chunked_partial_pending", ret == MK_HTTP_PARSER_PENDING);
    free(buf);
}

static void test_incremental(void)
{
    const char *text = "GET /a/b HTTP/1.1\r\nHost: x\r\nAccept: */*\r\n\r\n";
    char *buf = strdup(text);
    struct mk_http_request req;
    struct mk_http_parser p;
    int ret;

    memset(&req, 0, sizeof(req));
    mk_http_parser_init(&p);
    ret = mk_http_parser(&req, &p, buf, 20, &server);
    check("incremental_prefix_pending", ret == MK_HTTP_PARSER_PENDING);
    ret = mk_http_parser(&req, &p, buf, (int) strlen(text), &server);
    check("incremental_complete", ret == MK_HTTP_PARSER_OK);
    check("incremental_uri", ptr_eq(req.uri, "/a/b"));
    check("incremental_accept", ptr_eq(p.headers[MK_HEADER_ACCEPT].val, "*/*"));
    free(buf);
}

static void test_invalid(void)
{
    char *buf;
    struct mk_http_request req;
    struct mk_http_parser p;

    check("invalid_method_error", parse("X /a HTTP/1.1\r\n\r\n", &req, &p, &buf) == MK_HTTP_PARSER_ERROR);
    free(buf);
}

static void test_helpers(void)
{
    mk_ptr_t uri;
    char *s;
    char *t;
    struct mk_list *list;
    struct mk_list *head;
    int n = 0;

    uri.data = "/a%20b%2Fc";
    uri.len = strlen(uri.data);
    s = mk_utils_url_decode(uri);
    check("url_decode", s && strcmp(s, "/a b/c") == 0);
    free(s);

    uri.data = "/plain";
    uri.len = strlen(uri.data);
    check("url_decode_plain_is_null", mk_utils_url_decode(uri) == NULL);

    s = strdup("Sun, 06 Nov 1994 08:49:37 GMT");
    check("gmt2utime", mk_utils_gmt2utime(s) == 784111777);
    free(s);

    s = mk_string_html_escape("<a href=\"x\">&</a>");
    check("html_escape", s && strcmp(s, "&lt;a href=&quot;x&quot;&gt;&amp;&lt;/a&gt;") == 0);
    free(s);

    list = mk_string_split_line("one two  three");
    if (list) {
        mk_list_foreach(head, list) {
            n++;
        }
    }
    check("split_line_count", n == 3);
    if (list) {
        mk_string_split_free(list);
    }

    s = strdup("   padded \t");
    t = s;
    mk_string_trim(&t);
    check("trim", strcmp(t, "padded") == 0);
    free(s);

    s = mk_string_tolower("MoNkEy");
    check("tolower", s && strcmp(s, "monkey") == 0);
    free(s);

    check("search_insensitive", mk_string_search("Hello Monkey", "monkey", MK_STR_INSENSITIVE) == 6);
}

static void test_rconf(void)
{
    char path[] = "/tmp/monkey_oracle_XXXXXX";
    int fd = mkstemp(path);
    const char *cfg = "[SERVER]\n    Listen 2001\n    Workers 4\n    Daemon Off\n\n[VHOST]\n    Name example\n";
    struct mk_rconf *conf;
    struct mk_rconf_section *sec;
    char *v;

    if (fd < 0 || write(fd, cfg, strlen(cfg)) != (ssize_t) strlen(cfg)) {
        check("rconf_setup", 0);
        return;
    }
    close(fd);

    conf = mk_rconf_open(path);
    unlink(path);
    check("rconf_open", conf != NULL);
    if (!conf) {
        return;
    }

    sec = mk_rconf_section_get(conf, "SERVER");
    check("rconf_section", sec != NULL);
    if (sec) {
        v = mk_rconf_section_get_key(sec, "Listen", MK_RCONF_STR);
        check("rconf_str", v && strcmp(v, "2001") == 0);
        free(v);
        check("rconf_num", (long) mk_rconf_section_get_key(sec, "Workers", MK_RCONF_NUM) == 4);
        check("rconf_bool_off", (long) mk_rconf_section_get_key(sec, "Daemon", MK_RCONF_BOOL) == MK_FALSE);
    }
    sec = mk_rconf_section_get(conf, "VHOST");
    if (sec) {
        v = mk_rconf_section_get_key(sec, "Name", MK_RCONF_STR);
        check("rconf_vhost_name", v && strcmp(v, "example") == 0);
        free(v);
    }
    else {
        check("rconf_vhost", 0);
    }
    mk_rconf_free(conf);
}

int main(void)
{
    test_get();
    test_protocol_and_methods();
    test_post_content_length();
    test_chunked();
    test_incremental();
    test_invalid();
    test_helpers();
    test_rconf();

    printf("ORACLE failures=%d\n", failures);
    return failures ? 1 : 0;
}
