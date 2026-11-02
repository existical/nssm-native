#ifndef EVENT_H
#define EVENT_H

/* Borrowed thread-local text; do not free. Overwritten by the next call. */
TCHAR *error_string(unsigned long);
/* Owned result (or NULL); release with LocalFree(). */
TCHAR *message_string(unsigned long);
void log_event(unsigned short, unsigned long, ...);
void print_message(FILE *, unsigned long, ...);
int popup_message(HWND, unsigned int, unsigned long, ...);

#endif
