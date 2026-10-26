#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <monkey/mk_core.h>
#include <monkey/mk_utils.h>

#define MAX_SIZE 4096

/* Byte 0 selects the string/URL/date helper to drive; the rest is its input. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *str;
    char *tmp;
    char *p;
    size_t len;
    mk_ptr_t uri;
    struct mk_list *list;

    if (size < 2 || size > MAX_SIZE) {
        return -1;
    }

    len = size - 1;
    str = malloc(len + 1);
    if (!str) {
        return -1;
    }
    memcpy(str, data + 1, len);
    str[len] = '\0';

    switch (data[0] % 7) {
    case 0:
        uri.data = str;
        uri.len = len;
        tmp = mk_utils_url_decode(uri);
        free(tmp);
        break;
    case 1:
        (void) mk_utils_gmt2utime(str);
        break;
    case 2:
        list = mk_string_split_line(str);
        if (list) {
            mk_string_split_free(list);
        }
        break;
    case 3:
        tmp = mk_string_html_escape(str);
        free(tmp);
        break;
    case 4:
        p = str;
        (void) mk_string_trim(&p);
        break;
    case 5:
        tmp = mk_string_tolower(str);
        free(tmp);
        (void) mk_string_search(str, "monkey", MK_STR_INSENSITIVE);
        (void) mk_string_search_n(str, "HTTP", MK_STR_SENSITIVE, (int) len);
        break;
    case 6:
        tmp = mk_string_copy_substr(str, 0, (int) len);
        free(tmp);
        (void) mk_string_char_search(str, '/', (int) len);
        (void) mk_string_char_search_r(str, '/', (int) len);
        break;
    }

    free(str);
    return 0;
}
