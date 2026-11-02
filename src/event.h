#ifndef EVENT_H
#define EVENT_H

/* Borrowed thread-local text; do not free. Overwritten by the next call. */
TCHAR *error_string(unsigned long);
/* Owned result (or NULL); release with LocalFree(). */
TCHAR *message_string(unsigned long);
void log_event_strings(unsigned short, unsigned long, unsigned short, const TCHAR * const *);
/* Counted, string-only insertions: no varargs or terminating sentinel. */
template<typename... Strings>
void log_event(unsigned short type, unsigned long id, Strings... insertions) {
  static_assert(sizeof...(insertions) <= 16, "Too many event insertions");
  const TCHAR *strings[] = { NULL, insertions... };
  log_event_strings(type, id, static_cast<unsigned short>(sizeof...(insertions)), strings + 1);
}
void print_message(FILE *, unsigned long, ...);
int popup_message(HWND, unsigned int, unsigned long, ...);

#endif
