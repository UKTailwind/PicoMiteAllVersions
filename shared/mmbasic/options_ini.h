#ifndef MMBASIC_OPTIONS_INI_H
#define MMBASIC_OPTIONS_INI_H

#include <stddef.h>
#include <stdint.h>

typedef int (*mm_options_ini_name_predicate)(const char *name, void *ctx);
typedef int (*mm_options_ini_write_line)(void *ctx, const char *line);

int mm_options_ini_is_sparse(const char *buf);
void mm_options_ini_parse(char *buf, uint8_t *option_buf, int sparse_ini,
                          mm_options_ini_name_predicate skip_legacy,
                          void *skip_legacy_ctx);
int mm_options_ini_has_changes(const uint8_t *option_buf,
                               const uint8_t *default_option_buf,
                               mm_options_ini_name_predicate skip_write,
                               void *skip_write_ctx);
int mm_options_ini_write_changed(const uint8_t *option_buf,
                                 const uint8_t *default_option_buf,
                                 mm_options_ini_name_predicate skip_write,
                                 void *skip_write_ctx,
                                 mm_options_ini_write_line write_line,
                                 void *write_ctx);

#endif
