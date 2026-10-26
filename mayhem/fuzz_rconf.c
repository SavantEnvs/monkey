#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <mk_core/mk_rconf.h>
#include <mk_core/mk_string.h>

#define MAX_SIZE 65536

/* A config file can @INCLUDE any path on the host; keep the harness hermetic. */
static int has_include(const uint8_t *data, size_t size)
{
    size_t i;

    for (i = 0; i + 8 <= size; i++) {
        if (data[i] == '@' && strncasecmp((const char *) data + i, "@include", 8) == 0) {
            return 1;
        }
    }
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    int fd;
    char path[4096];
    const char *tmpdir;
    struct mk_rconf *conf;
    struct mk_rconf_section *section;
    struct mk_rconf_entry *entry;
    struct mk_list *shead;
    struct mk_list *ehead;
    struct mk_list *list;

    if (size == 0 || size > MAX_SIZE || has_include(data, size)) {
        return -1;
    }

    tmpdir = getenv("TMPDIR");
    snprintf(path, sizeof(path), "%s/fuzz_rconf_XXXXXX", tmpdir ? tmpdir : "/tmp");
    fd = mkstemp(path);
    if (fd < 0) {
        return -1;
    }
    if (write(fd, data, size) != (ssize_t) size) {
        close(fd);
        unlink(path);
        return -1;
    }
    close(fd);

    conf = mk_rconf_open(path);
    unlink(path);
    if (!conf) {
        return 0;
    }

    mk_list_foreach(shead, &conf->sections) {
        section = mk_list_entry(shead, struct mk_rconf_section, _head);
        mk_list_foreach(ehead, &section->entries) {
            entry = mk_list_entry(ehead, struct mk_rconf_entry, _head);
            free(mk_rconf_section_get_key(section, entry->key, MK_RCONF_STR));
            (void) mk_rconf_section_get_key(section, entry->key, MK_RCONF_NUM);
            (void) mk_rconf_section_get_key(section, entry->key, MK_RCONF_BOOL);
            list = mk_rconf_section_get_key(section, entry->key, MK_RCONF_LIST);
            if (list) {
                mk_string_split_free(list);
            }
        }
    }
    (void) mk_rconf_meta_get(conf, "SET");
    mk_rconf_free(conf);

    return 0;
}
