#ifndef NSSM_EVENT_CAPTURE_H
#define NSSM_EVENT_CAPTURE_H
#include "test.h"
namespace event_test {
struct record { unsigned short type; unsigned long id; std::vector<std::basic_string<TCHAR> > strings; };
extern bool active;
extern std::vector<record> records;
void capture(unsigned short, unsigned long, unsigned short, const TCHAR * const *);
template<typename... Strings>
void log(unsigned short type, unsigned long id, Strings... insertions) {
  const TCHAR *strings[] = { NULL, insertions... };
  capture(type, id, static_cast<unsigned short>(sizeof...(insertions)), strings + 1);
}
}
void test_launch_events();
#endif
