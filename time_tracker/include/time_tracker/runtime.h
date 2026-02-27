#ifndef TIME_TRACKER_RUNTIME_H
#define TIME_TRACKER_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

void hide_console_window(void);
void derive_default_log_path(char* out, size_t out_len);
bool redirect_output_to_log(const char* log_path);
void log_win_error(const char* context);
int utf16_to_utf8(const wchar_t* src, char* dst, int dst_len);

#endif
