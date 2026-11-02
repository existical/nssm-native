#include "test.h"
#include <errno.h>

void test_numeric_settings();
void test_affinity_settings();
void test_affinity_allocation();

void test_numeric() {
  struct sample { const TCHAR *text; unsigned long value; };
  const sample valid[] = { {_T("0"), 0}, {_T("42"), 42}, {_T("4294967295"), MAXDWORD},
    {_T("0xffffffff"), MAXDWORD}, {_T("037777777777"), MAXDWORD},
    {_T("010"), 8}, {_T("+12"), 12}, {_T(" \t12"), 12} };
  for (size_t i = 0; i < _countof(valid); i++) {
    unsigned long value = 77;
    errno = ERANGE; // A prior conversion must not cause this one to fail.
    CHECK(str_number(valid[i].text, &value) == 0);
    CHECK(value == valid[i].value);
  }
  const TCHAR *invalid[] = { _T(""), _T(" "), _T("+"), _T("-"), _T("-0"), _T("-1"),
    _T(" \t-2"), _T("4294967296"), _T("999999999999999999999999999999"),
    _T("0x100000000"), _T("040000000000"), _T("0x"), _T("08"), _T("12ms"),
    _T("1,2"), _T("1-2"), _T("12 "), _T("++1"), _T("+-1"), _T("text") };
  for (size_t i = 0; i < _countof(invalid); i++) {
    unsigned long value = 77;
    CHECK(str_number(invalid[i], &value) != 0);
    CHECK(value == 77);
  }
  TCHAR range[] = _T("12-13");
  TCHAR *end = NULL;
  unsigned long value = 77;
  CHECK(str_number(range, &value, &end) == 2);
  CHECK(value == 12 && end == range + 2);
  const TCHAR *bad_prefixes[] = { _T(""), _T("-1,2"), _T("4294967296,2"), _T("+,2") };
  for (size_t i = 0; i < _countof(bad_prefixes); i++) {
    value = 77;
    CHECK(str_number(bad_prefixes[i], &value, &end) == 1);
    CHECK(value == 77);
  }
  CHECK(str_number(NULL, &value, &end) == 1 && end == NULL);
  CHECK(str_number(_T("1"), NULL) == 1);
  CHECK(str_number(_T("1"), &value, NULL) == 1);
  test_numeric_settings();
}

static void affinity_case(const TCHAR *input, bool valid, unsigned __int64 expected = 0) {
  __int64 mask = 123;
  std::basic_string<TCHAR> text(input);
  int result = affinity_string_to_mask(const_cast<TCHAR *>(text.c_str()), &mask);
  CHECK((result == 0) == valid);
  CHECK((unsigned __int64) mask == (valid ? expected : 0));
}

static void affinity_round_trip(unsigned __int64 expected) {
  TCHAR *text = NULL;
  REQUIRE(affinity_mask_to_string((__int64) expected, &text) == 0);
  if (! expected) CHECK(text == NULL);
  else {
    REQUIRE(text);
    __int64 actual = 0;
    CHECK(affinity_string_to_mask(text, &actual) == 0);
    CHECK((unsigned __int64) actual == expected);
    HeapFree(GetProcessHeap(), 0, text);
  }
}

class affinity_list {
public:
  HWND window;
  affinity_list() {
    // A real, hidden list box exercises Windows row/item-data behavior. No
    // service dialog is shown, and no service configuration is changed.
    window = CreateWindowEx(0, _T("LISTBOX"), _T("NSSM affinity regression"),
      WS_POPUP | LBS_EXTENDEDSEL | LBS_SORT, 0, 0, 200, 120, NULL, NULL, GetModuleHandle(NULL), NULL);
    REQUIRE(window);
  }
  ~affinity_list() { CHECK(DestroyWindow(window)); }
};

static void test_affinity_list() {
  affinity_list list;
  DWORD_PTR top = (DWORD_PTR) 1 << (sizeof(DWORD_PTR) * 8 - 1);
  DWORD_PTR available = top | 5;
  CHECK(populate_affinity_list(list.window, available) == 3);
  for (int row = 0; row < 3; row++) {
    LRESULT cpu = SendMessage(list.window, LB_GETITEMDATA, row, 0);
    TCHAR label[4] = {};
    CHECK(SendMessage(list.window, LB_GETTEXT, row, (LPARAM) label) != LB_ERR);
    unsigned long parsed = 100;
    CHECK(str_number(label, &parsed) == 0 && parsed == cpu);
  }
  select_affinity_list(list.window, (__int64) (top | 4));
  __int64 selected = 0;
  CHECK(affinity_from_list(list.window, &selected) == 0);
  CHECK((unsigned __int64) selected == (unsigned __int64) (top | 4));
  select_affinity_list(list.window, (__int64) available);
  CHECK(affinity_from_list(list.window, &selected) == 0 && selected == 0);
  select_affinity_list(list.window, 0);
  CHECK(affinity_from_list(list.window, &selected) != 0 && selected == 0);
  // Changing an item's data proves that rows are not treated as CPU numbers.
  CHECK(populate_affinity_list(list.window, 3) == 2);
  SendMessage(list.window, LB_SETITEMDATA, 1, 17);
  select_affinity_list(list.window, 1ULL << 17);
  CHECK(affinity_from_list(list.window, &selected) == 0 && selected == (1ULL << 17));
  SendMessage(list.window, LB_SETITEMDATA, 1, 64);
  CHECK(affinity_from_list(list.window, &selected) != 0 && selected == 0);
  SendMessage(list.window, LB_SETITEMDATA, 1, (LPARAM) -1);
  CHECK(affinity_from_list(list.window, &selected) != 0 && selected == 0);
  CHECK(populate_affinity_list(list.window, 0) == 0);
  CHECK(SendMessage(list.window, LB_GETCOUNT, 0, 0) == 0);
  CHECK(affinity_from_list(list.window, &selected) != 0);
  CHECK(populate_affinity_list(list.window, ~(DWORD_PTR) 0) == sizeof(DWORD_PTR) * 8);
  select_affinity_list(list.window, (__int64) top);
  CHECK(affinity_from_list(list.window, &selected) == 0 && (unsigned __int64) selected == top);
}

void test_affinity() {
  affinity_case(_T("0"), true, 1);
  affinity_case(_T("63"), true, 1ULL << 63);
  affinity_case(_T("0-63"), true, ~0ULL);
  affinity_case(_T("0-2,4,2-4"), true, 31);
  affinity_case(_T("0x3f"), true, 1ULL << 63);
  affinity_case(_T("0-64"), false);
  affinity_case(_T("64"), false);
  affinity_case(_T("3-1"), false);
  affinity_case(_T("-1"), false);
  affinity_case(_T("1x"), false);
  const TCHAR *invalid[] = { _T(""), _T(" "), _T(","), _T("0,"), _T("0,,1"),
    _T("0-"), _T("0--1"), _T("63-64"), _T("0-4294967295"), _T("0-4294967296"),
    _T("4294967296"), _T("0-1-2"), _T("0-2,"), _T("0, "), _T("0,foo"),
    _T("0x"), _T("08"), _T("1;2"), _T("1 2"), _T("1-2x"), _T("-0") };
  for (size_t i = 0; i < _countof(invalid); i++) affinity_case(invalid[i], false);
  affinity_case(_T(" 0, +2,0x3-04"), true, 29);
  __int64 mask = 123;
  CHECK(affinity_string_to_mask(NULL, &mask) == 0 && mask == 0);
  CHECK(affinity_string_to_mask(NULL, NULL) != 0);
  for (int count = 1; count <= 130; count++) {
    std::basic_string<TCHAR> text = _T("0");
    for (int i = 1; i < count; i++) text += _T(",0");
    affinity_case(text.c_str(), true, 1);
    affinity_case((text + _T(",")).c_str(), false);
  }
  // Every endpoint combination, including the first out-of-range endpoint.
  for (unsigned first = 0; first <= 64; first++) {
    for (unsigned last = 0; last <= 64; last++) {
      TCHAR text[32];
      _stprintf_s(text, _T("%u-%u"), first, last);
      bool valid = first <= last && last < 64;
      unsigned __int64 expected = 0;
      if (valid) for (unsigned cpu = first; cpu <= last; cpu++) expected |= 1ULL << cpu;
      affinity_case(text, valid, expected);
    }
  }
  for (unsigned i = 0; i < 64; i++) affinity_round_trip(1ULL << i);
  std::basic_string<TCHAR> all;
  for (unsigned i = 0; i < 64; i++) {
    TCHAR token[8];
    _stprintf_s(token, _T("%s%u"), i ? _T(",") : _T(""), i);
    all += token;
  }
  affinity_case(all.c_str(), true, ~0ULL);
  affinity_case((all + _T(",63")).c_str(), true, ~0ULL);
  affinity_round_trip(0);
  affinity_round_trip(~0ULL);
  affinity_round_trip(0x5555555555555555ULL);
  affinity_round_trip(0xaaaaaaaaaaaaaaaaULL);
  test_affinity_allocation();
  test_affinity_list();
  test_affinity_settings();
}
