#ifndef NSSM_TEXT_BUFFER_H
#define NSSM_TEXT_BUFFER_H

#include <memory>
#include <new>
#include <tchar.h>
#include <limits.h>

/* Bounded scratch space for Win32 strings. Capacity is in characters. */
template<unsigned long Capacity>
class text_buffer {
  static_assert(Capacity <= INT_MAX / sizeof(TCHAR), "Buffer exceeds the Win32 API size limit");
  std::unique_ptr<TCHAR[]> storage;
public:
  text_buffer() : storage(new (std::nothrow) TCHAR[Capacity]) {}
  TCHAR *get() const { return storage.get(); }
  static constexpr unsigned long size() { return Capacity; }
  static constexpr unsigned long bytes() { return static_cast<unsigned long>(Capacity * sizeof(TCHAR)); }
};

#endif
