#include "test.h"

// Exercise the real registry implementation with guarded allocations and
// deterministic API failures. None of these wrappers enter nssm.exe.
namespace registry_test {
struct allocation { BYTE *p; SIZE_T bytes; };
static std::vector<allocation> allocations;
static bool active;
static int fail_after = -1;
static const SIZE_T guard_size = 256;
static LPVOID WINAPI allocate(HANDLE heap, DWORD flags, SIZE_T bytes) {
  if (! active) return HeapAlloc(heap, flags, bytes);
  if (! fail_after) return NULL;
  if (fail_after > 0) --fail_after;
  BYTE *p = static_cast<BYTE *>(HeapAlloc(heap, flags, bytes + guard_size));
  if (p) {
    // Also bound legacy unbounded reads during the pre-fix reproduction.
    memset(p + bytes, 0x5a, guard_size);
    memset(p + bytes + guard_size - 2 * sizeof(TCHAR), 0, 2 * sizeof(TCHAR));
    allocation a = { p, bytes };
    allocations.push_back(a);
  }
  return p;
}
static BOOL WINAPI release(HANDLE heap, DWORD flags, LPVOID memory) {
  for (size_t i = 0; i < allocations.size(); i++) {
    if (allocations[i].p != memory) continue;
    const allocation a = allocations[i];
    for (SIZE_T j = 0; j < guard_size - 2 * sizeof(TCHAR); j++) {
      if (a.p[a.bytes + j] != 0x5a) {
        CHECK(false); // Production code wrote past its requested allocation.
        break;
      }
    }
    allocations.erase(allocations.begin() + i);
    break;
  }
  return HeapFree(heap, flags, memory);
}
enum query_behavior { normal, grow_once, grow_always, change_type, odd_size, query_error };
static query_behavior behavior;
static unsigned data_queries;
static LSTATUS WINAPI query(HKEY key, LPCTSTR name, LPDWORD reserved,
    LPDWORD type, LPBYTE data, LPDWORD bytes) {
  if (active && name && ! _tcscmp(name, _T("race")) && data) {
    ++data_queries;
    if (behavior == query_error) return ERROR_ACCESS_DENIED;
    if (behavior == grow_always || (behavior == grow_once && data_queries == 1)) {
      const TCHAR grown[] = _T("A=longer\0B=\0");
      REQUIRE(RegSetValueEx(key, name, 0, REG_MULTI_SZ,
        reinterpret_cast<const BYTE *>(grown), sizeof(grown)) == ERROR_SUCCESS);
      *bytes = sizeof(grown);
      return ERROR_MORE_DATA;
    }
    LSTATUS ret = RegQueryValueEx(key, name, reserved, type, data, bytes);
    if (ret == ERROR_SUCCESS) {
      if (behavior == change_type) *type = REG_SZ;
      if (behavior == odd_size) --*bytes;
    }
    return ret;
  }
  return RegQueryValueEx(key, name, reserved, type, data, bytes);
}
enum expansion_behavior { expand_normal, fail_probe, fail_write, grow_write };
static expansion_behavior expansion;
static DWORD WINAPI expand(LPCTSTR source, LPTSTR output, DWORD count) {
  if (active) {
    if ((! output && expansion == fail_probe) || (output && expansion == fail_write)) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return 0;
    }
    if (output && expansion == grow_write) {
      std::basic_string<TCHAR> grown(128, _T('z'));
      REQUIRE(SetEnvironmentVariable(_T("NSSM_TEST_REG"), grown.c_str()));
    }
  }
  return ExpandEnvironmentStrings(source, output, count);
}
static void reset() {
  CHECK(allocations.empty());
  active = true;
  fail_after = -1;
  behavior = normal;
  expansion = expand_normal;
  data_queries = 0;
}
}

#define HeapAlloc registry_test::allocate
#define HeapFree registry_test::release
#undef RegQueryValueEx
#define RegQueryValueEx registry_test::query
#undef ExpandEnvironmentStrings
#define ExpandEnvironmentStrings registry_test::expand
#include "../src/registry.cpp"
#undef HeapAlloc
#undef HeapFree
#undef RegQueryValueEx
#undef ExpandEnvironmentStrings

static void store_raw(HKEY key, const TCHAR *name, DWORD type, const void *data, DWORD bytes) {
  REQUIRE(RegSetValueEx(key, name, 0, type, static_cast<const BYTE *>(data), bytes) == ERROR_SUCCESS);
}
static void free_result(TCHAR *value) {
  if (value) registry_test::release(GetProcessHeap(), 0, value);
}
static void expect_block(const TCHAR *block, DWORD length, const TCHAR *expected, DWORD expected_length) {
  CHECK(length == expected_length);
  REQUIRE(block);
  if (length == expected_length) CHECK(! memcmp(block, expected, length * sizeof(TCHAR)));
}

// A real inaccessible page catches reads past the declared block, including
// one-character lookahead at an otherwise valid final terminator.
class guarded_block {
  BYTE *pages;
public:
  TCHAR *data;
  DWORD count;
  guarded_block(const TCHAR *value, DWORD length) : pages(NULL), data(NULL), count(length) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    REQUIRE(length * sizeof(TCHAR) <= info.dwPageSize);
    pages = static_cast<BYTE *>(VirtualAlloc(NULL, 2 * info.dwPageSize,
      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    REQUIRE(pages);
    DWORD old;
    if (! VirtualProtect(pages + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &old)) {
      VirtualFree(pages, 0, MEM_RELEASE);
      throw std::runtime_error("Could not protect block boundary");
    }
    data = reinterpret_cast<TCHAR *>(pages + info.dwPageSize) - length;
    memcpy(data, value, length * sizeof(TCHAR));
  }
  ~guarded_block() { CHECK(VirtualFree(pages, 0, MEM_RELEASE)); }
private:
  guarded_block(const guarded_block &);
  guarded_block &operator=(const guarded_block &);
};

void test_registry_bounds() {
  using namespace registry_test;
  registry_fixture fixture;
  HKEY key = open_registry(_T("registry-bounds"), KEY_READ | KEY_WRITE);
  REQUIRE(key);
  reset();

  printf("NSSM-001: expansion bounds and failures\n");
  const TCHAR reference[] = _T("%NSSM_TEST_REG%");
  store_raw(key, _T("value"), REG_EXPAND_SZ, reference, sizeof(reference));
  for (unsigned length = 0; length <= 80; length++) {
    std::basic_string<TCHAR> value(length, _T('x'));
    REQUIRE(SetEnvironmentVariable(_T("NSSM_TEST_REG"), value.c_str()));
    TCHAR output[160];
    for (size_t i = 0; i < _countof(output); i++) output[i] = 0x5a5a;
    int ret = get_string(key, _T("value"), output, 64 * sizeof(TCHAR), true, false, true);
    if (length < 64) { CHECK(! ret); CHECK(! _tcscmp(output, value.c_str())); }
    else { CHECK(ret != 0); CHECK(! output[0]); }
    for (size_t i = 64; i < _countof(output); i++) CHECK(output[i] == 0x5a5a);
  }
  TCHAR output[64];
  REQUIRE(SetEnvironmentVariable(_T("NSSM_TEST_REG"), _T("small")));
  for (int mode = fail_probe; mode <= grow_write; mode++) {
    expansion = static_cast<expansion_behavior>(mode);
    REQUIRE(SetEnvironmentVariable(_T("NSSM_TEST_REG"), _T("small")));
    CHECK(get_string(key, _T("value"), output, sizeof(output), true, false, true) != 0);
    CHECK(! output[0]);
  }
  expansion = expand_normal;
  for (int failure = 0; failure < 2; failure++) {
    REQUIRE(SetEnvironmentVariable(_T("NSSM_TEST_REG"), _T("small")));
    fail_after = failure;
    output[0] = _T('!');
    CHECK(get_string(key, _T("value"), output, sizeof(output), true, false, true) != 0);
    CHECK(! output[0]);
    fail_after = -1;
  }

  printf("NSSM-010: string types, alignment, termination and optional values\n");
  const TCHAR missing[] = _T("%NSSM_TEST_REG_ABSENT%");
  SetEnvironmentVariable(_T("NSSM_TEST_REG_ABSENT"), NULL);
  store_raw(key, _T("value"), REG_EXPAND_SZ, missing, sizeof(missing));
  CHECK(! get_string(key, _T("value"), output, sizeof(output), true, false, true));
  CHECK(! _tcscmp(output, missing));
  store_raw(key, _T("value"), REG_SZ, reference, sizeof(reference));
  CHECK(! get_string(key, _T("value"), output, sizeof(output), true, false, true));
  CHECK(! _tcscmp(output, reference));
  store_raw(key, _T("value"), REG_EXPAND_SZ, reference, sizeof(reference));
  CHECK(! get_string(key, _T("value"), output, sizeof(output), false, false, true));
  CHECK(! _tcscmp(output, reference));
  const TCHAR quoted[] = _T("\"C:\\Program Files\\Tool\"");
  store_raw(key, _T("value"), REG_SZ, quoted, sizeof(quoted));
  CHECK(! get_string(key, _T("value"), output, sizeof(output), false, true, true));
  CHECK(! _tcscmp(output, _T("C:\\Program Files\\Tool")));
  const TCHAR invalid[][4] = { { 'A', 'B', 'C', 'D' }, { 'A', 0, 'B', 0 } };
  for (unsigned i = 0; i < _countof(invalid); i++) {
    store_raw(key, _T("value"), REG_SZ, invalid[i], sizeof(invalid[i]));
    CHECK(get_string(key, _T("value"), output, sizeof(output), false, false, true) != 0);
    CHECK(! output[0]);
  }
  store_raw(key, _T("value"), REG_SZ, quoted, sizeof(quoted) - 1);
  CHECK(get_string(key, _T("value"), output, sizeof(output), false, false, true) != 0);
  CHECK(! output[0]);
  store_raw(key, _T("value"), REG_BINARY, quoted, sizeof(quoted));
  CHECK(get_string(key, _T("value"), output, sizeof(output), true, true, true) != 0);
  CHECK(! output[0]);
  store_raw(key, _T("value"), REG_SZ, NULL, 0);
  CHECK(get_string(key, _T("value"), output, sizeof(output), false, false, true) != 0);
  const TCHAR empty[] = _T("");
  store_raw(key, _T("value"), REG_SZ, empty, sizeof(empty));
  CHECK(! get_string(key, _T("value"), output, sizeof(TCHAR), true, false, true));
  CHECK(! output[0]);
  CHECK(get_string(key, _T("value"), output, 0, false, false, true) != 0);
  CHECK(get_string(key, _T("value"), output, 3, false, false, true) != 0);
  CHECK(! get_string(key, _T("absent"), output, sizeof(output), false, false, false));
  CHECK(! output[0]);
  CHECK(get_string(key, _T("absent"), output, sizeof(output), false, false, true) != 0);
  const TCHAR exact[] = _T("abc");
  store_raw(key, _T("value"), REG_SZ, exact, sizeof(exact));
  CHECK(! get_string(key, _T("value"), output, sizeof(exact), false, false, true));
  CHECK(! _tcscmp(output, exact));
  CHECK(get_string(key, _T("value"), output, sizeof(exact) - sizeof(TCHAR), false, false, true) != 0);
  CHECK(! output[0]);
  const TCHAR padded[] = { 'x', 0, 0, 0 };
  store_raw(key, _T("value"), REG_SZ, padded, sizeof(padded));
  CHECK(! get_string(key, _T("value"), output, sizeof(output), false, false, true));
  CHECK(! _tcscmp(output, _T("x")));

  printf("NSSM-010: bounded MULTI_SZ reads and changing registry data\n");
  const TCHAR good[] = _T("A=\0B=value\0");
  const TCHAR empty_block[] = { 0, 0 };
  const TCHAR malformed[][6] = {
    { 'A', '=', 'x', 'y', 'z', 'z' }, { 'A', '=', 'x', 'y', 'z', 0 },
    { 'A', 0, 0, 'B', 0, 0 }, { 0, 'B', '=', 'x', 0, 0 }
  };
  TCHAR *env = NULL;
  DWORD count = 99;
  for (unsigned i = 0; i < _countof(malformed); i++) {
    store_raw(key, _T("env"), REG_MULTI_SZ, malformed[i], sizeof(malformed[i]));
    CHECK(get_environment(_T("test"), key, _T("env"), &env, &count) != 0);
    CHECK(! env && ! count);
    free_result(env); env = NULL;
  }
  store_raw(key, _T("env"), REG_MULTI_SZ, good, sizeof(good));
  CHECK(! get_environment(_T("test"), key, _T("env"), &env, &count));
  expect_block(env, count, good, _countof(good));
  // Replacing or clearing a previously loaded block must not leak it.
  CHECK(! get_environment(_T("test"), key, _T("absent"), &env, &count));
  CHECK(! env && ! count);
  store_raw(key, _T("env"), REG_MULTI_SZ, empty_block, sizeof(empty_block));
  CHECK(! get_environment(_T("test"), key, _T("env"), &env, &count));
  expect_block(env, count, empty_block, 2);
  free_result(env); env = NULL;
  const TCHAR padded_env[] = { 'A', '=', 0, 0, 0, 0 };
  store_raw(key, _T("env"), REG_MULTI_SZ, padded_env, sizeof(padded_env));
  CHECK(! get_environment(_T("test"), key, _T("env"), &env, &count));
  CHECK(count == 4);
  free_result(env); env = NULL;
  store_raw(key, _T("env"), REG_MULTI_SZ, good, sizeof(good) - 1);
  CHECK(get_environment(_T("test"), key, _T("env"), &env, &count) != 0);
  CHECK(! env && ! count);
  free_result(env); env = NULL;
  store_raw(key, _T("env"), REG_SZ, good, sizeof(good));
  CHECK(get_environment(_T("test"), key, _T("env"), &env, &count) != 0);
  CHECK(! env && ! count);
  store_raw(key, _T("env"), REG_MULTI_SZ, good, sizeof(good));
  fail_after = 0;
  CHECK(get_environment(_T("test"), key, _T("env"), &env, &count) != 0);
  CHECK(! env && ! count);
  fail_after = -1;
  for (int mode = grow_once; mode <= query_error; mode++) {
    behavior = static_cast<query_behavior>(mode);
    data_queries = 0;
    store_raw(key, _T("race"), REG_MULTI_SZ, good, sizeof(good));
    int ret = get_environment(_T("test"), key, _T("race"), &env, &count);
    if (mode == grow_once) {
      const TCHAR grown[] = _T("A=longer\0B=\0");
      CHECK(! ret); if (env) expect_block(env, count, grown, _countof(grown));
    }
    else { CHECK(ret != 0); CHECK(! env && ! count); }
    CHECK(data_queries <= 3);
    free_result(env); env = NULL;
  }
  behavior = normal;
  REQUIRE(SetEnvironmentVariable(_T("NSSM_TEST_REG"), NULL));
  CHECK(allocations.empty());
  // Clean any legacy leaks after reporting them in the pre-fix run.
  while (! allocations.empty()) release(GetProcessHeap(), 0, allocations.back().p);
  active = false;
  RegCloseKey(key);
}

void test_multisz_bounds() {
  using namespace registry_test;
  reset();
  printf("NSSM-011: final-terminator lookahead\n");
  {
    TCHAR sentinel[] = { 'A', '=', 'x', 0, 0, 'Z', 0 };
    TCHAR *formatted = NULL;
    DWORD length = 0;
    CHECK(! format_double_null(sentinel, 5, &formatted, &length));
    const TCHAR expected[] = _T("A=x\0");
    expect_block(formatted, length, expected, _countof(expected));
    free_result(formatted);
  }
  printf("NSSM-003: duplicate replacement and complete-key matching\n");
  const TCHAR block[] = _T("A=one\0X=keep\0a=two\0AB=keep\0A=\0Z=last\0");
  const TCHAR expected[] = _T("A=replacement\0X=keep\0AB=keep\0Z=last\0");
  TCHAR *result = NULL;
  DWORD count = 0;
  CHECK(! append_to_environment_block(const_cast<TCHAR *>(block), _countof(block),
    _T("A=replacement"), &result, &count));
  expect_block(result, count, expected, _countof(expected));
  free_result(result);
  // Exercise duplicate positions and replacement sizes independently.
  for (unsigned duplicates = 1; duplicates <= 6; duplicates++) {
    for (unsigned value_length = 0; value_length <= 64; value_length += 16) {
      std::vector<TCHAR> input;
      for (unsigned i = 0; i < duplicates; i++) {
        const TCHAR item[] = _T("a=old");
        input.insert(input.end(), item, item + _countof(item));
      }
      input.push_back(0);
      std::basic_string<TCHAR> replacement = _T("A=");
      replacement.append(value_length, _T('x'));
      CHECK(! append_to_environment_block(input.data(), static_cast<DWORD>(input.size()),
        &replacement[0], &result, &count));
      CHECK(count == replacement.size() + 2);
      REQUIRE(result);
      CHECK(! _tcscmp(result, replacement.c_str()));
      CHECK(! result[replacement.size() + 1]);
      free_result(result);
    }
  }
  const TCHAR case_block[] = _T("A=one\0a=two\0");
  const TCHAR case_expected[] = _T("A=\0a=two\0");
  CHECK(! append_to_double_null(const_cast<TCHAR *>(case_block), _countof(case_block),
    &result, &count, _T("A="), 2, true));
  expect_block(result, count, case_expected, _countof(case_expected));
  free_result(result);
  const TCHAR dependencies[] = _T("ServiceLong\0Other\0");
  const TCHAR expected_dependencies[] = _T("ServiceLong\0Other\0Service\0");
  CHECK(! append_to_double_null(const_cast<TCHAR *>(dependencies), _countof(dependencies),
    &result, &count, _T("Service"), 0, false));
  expect_block(result, count, expected_dependencies, _countof(expected_dependencies));
  free_result(result);
  CHECK(! append_to_environment_block(NULL, 0, _T("A="), &result, &count));
  const TCHAR single[] = _T("A=\0");
  if (result) {
    expect_block(result, count, single, _countof(single));
    TCHAR *removed = NULL; DWORD removed_count = 0;
    CHECK(! remove_from_environment_block(result, count, _T("A"), &removed, &removed_count));
    const TCHAR empty[] = {0,0};
    expect_block(removed, removed_count, empty, 2);
    free_result(removed);
  }
  free_result(result);
  CHECK(! append_to_environment_block(const_cast<TCHAR *>(case_block), _countof(case_block),
    _T(""), &result, &count));
  expect_block(result, count, case_block, _countof(case_block));
  free_result(result);

  printf("NSSM-011: bounded formatting and round trips\n");
  TCHAR lines[] = _T("A=\0B=two\0C=three\0");
  const TCHAR formatted_lines[] = _T("A=\r\nB=two\r\nC=three\0");
  CHECK(! format_double_null(lines, _countof(lines), &result, &count));
  expect_block(result, count, formatted_lines, _countof(formatted_lines));
  TCHAR *roundtrip = NULL; DWORD roundtrip_count = 0;
  CHECK(! unformat_double_null(result, count, &roundtrip, &roundtrip_count));
  expect_block(roundtrip, roundtrip_count, lines, _countof(lines));
  free_result(roundtrip); free_result(result);
  TCHAR empty[] = {0,0};
  CHECK(! format_double_null(empty, 2, &result, &count));
  expect_block(result, count, empty, 2); free_result(result);
  CHECK(! format_double_null(NULL, 0, &result, &count));
  CHECK(! result && ! count);
  TCHAR invalid[] = { 'A', 0, 'B', 'C', 0, 0 };
  // The declared input ends before the two terminators; padded storage keeps
  // the legacy pre-fix execution contained while assertions catch acceptance.
  CHECK(format_double_null(invalid, 4, &result, &count) != 0);
  CHECK(! result && ! count); free_result(result);
  CHECK(append_to_environment_block(invalid, 4, _T("A="), &result, &count) != 0);
  CHECK(! result && ! count); free_result(result);
  for (int operation = 0; operation < 2; operation++) {
    fail_after = 0;
    int ret = operation ? format_double_null(lines, _countof(lines), &result, &count)
      : append_to_environment_block(lines, _countof(lines), _T("A=new"), &result, &count);
    CHECK(ret != 0); CHECK(! result && ! count);
    fail_after = -1;
  }
  printf("MULTI_SZ page-boundary validation\n");
  {
    guarded_block guarded(lines, _countof(lines));
    CHECK(! format_double_null(guarded.data, guarded.count, &result, &count));
    expect_block(result, count, formatted_lines, _countof(formatted_lines));
    free_result(result);
    CHECK(! append_to_environment_block(guarded.data, guarded.count, _T("A=changed"), &result, &count));
    free_result(result);
  }
  {
    const TCHAR missing_end[] = { 'A', '=', 'x', 0 };
    guarded_block guarded(missing_end, _countof(missing_end));
    CHECK(format_double_null(guarded.data, guarded.count, &result, &count) != 0);
    CHECK(! result && ! count);
    CHECK(append_to_environment_block(guarded.data, guarded.count, _T("A="), &result, &count) != 0);
    CHECK(! result && ! count);
  }
  CHECK(allocations.empty());
  while (! allocations.empty()) release(GetProcessHeap(), 0, allocations.back().p);
  active = false;
}
