// Included after io.cpp and the ownership fixture to exercise the real worker
// with deterministic pipe fragments and real temporary output files.
namespace logger_test {
static std::string wide_bytes(const wchar_t *s) {
  return std::string(reinterpret_cast<const char *>(s), wcslen(s) * sizeof(wchar_t));
}
static std::string read_path(const TCHAR *path) {
  HANDLE file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  REQUIRE(file != INVALID_HANDLE_VALUE);
  std::string bytes; char buffer[4096]; DWORD count;
  while (ReadFile(file, buffer, sizeof(buffer), &count, NULL) && count) bytes.append(buffer, count);
  CloseHandle(file); return bytes;
}
struct stream_fixture : fixture {
  stream_fixture(const std::string &input, DWORD chunk, bool timestamps = true, ULONGLONG threshold = 0) {
    stream_test = true; stream_input = input; read_chunk = chunk; read_position = 0;
    fail_info = fail_size = fail_move = fail_truncate = fail_read = false;
    short_write = MAXDWORD; fail_after = -1; bytes_written = 0;
    rotations = write_errors = 0; rotated_paths.clear();
    service->timestamp_log = timestamps;
    service->rotate_stdout_online = NSSM_ROTATE_ONLINE;
    service->rotate_bytes_low = static_cast<DWORD>(threshold);
    service->rotate_bytes_high = static_cast<DWORD>(threshold >> 32);
    thread_mode = 2;
  }
  DWORD run() {
    REQUIRE(get_output_handles(service, &si) == 0);
    REQUIRE(deferred_routine);
    DWORD result = deferred_routine(deferred_argument);
    SetEvent(deferred_thread);
    deferred_routine = NULL; deferred_argument = NULL;
    stop(); deferred_thread = NULL;
    return result;
  }
  ~stream_fixture() {
    stream_test = false;
    for (size_t i = 0; i < rotated_paths.size(); i++) CHECK(DeleteFile(rotated_paths[i].c_str()));
    rotated_paths.clear();
  }
};
static void test_stream_bytes() {
  const std::string stamp = "2026-11-02 03:04:05.006: ";
  // TIMESTAMP_LEN includes 25 bytes: verify the expectation independently.
  const std::string bom("\xff\xfe", 2);
  const std::string wide_stamp = wide_bytes(L"2026-11-02 03:04:05.006: ");
  CHECK(stamp.size() == TIMESTAMP_LEN);
  const std::string ascii = "first\r\n\nlast\nend";
  const std::string expected = stamp + "first\r\n" + stamp + "\n" + stamp + "last\n" + stamp + "end";
  for (DWORD chunk = 1; chunk <= 20; chunk++) {
    { stream_fixture f(ascii, chunk); CHECK(f.run() == 0); CHECK(f.contents() == expected); CHECK(bytes_written == expected.size()); }
    const std::string input = bom + wide_bytes(L"first\r\n\nlast\nend");
    const std::string result = bom + wide_stamp + wide_bytes(L"first\r\n") + wide_stamp + wide_bytes(L"\n") + wide_stamp + wide_bytes(L"last\n") + wide_stamp + wide_bytes(L"end");
    { stream_fixture f(input, chunk); CHECK(f.run() == 0); CHECK(f.contents() == result); CHECK(bytes_written == result.size()); }
  }
  { stream_fixture f("", 1); CHECK(f.run() == 0); CHECK(f.contents().empty()); }
  { stream_fixture f("abc", 1); fail_read = true; CHECK(f.run() == 2); CHECK(f.contents().empty()); }
  { stream_fixture f(bom, 1); CHECK(f.run() == 0); CHECK(f.contents() == bom); }
  { stream_fixture f("x", 1); CHECK(f.run() == 0); CHECK(f.contents() == stamp + "x"); }
  // UTF-8 remains byte-oriented, including multibyte sequences split across reads.
  { stream_fixture f("caf\xc3\xa9\n\xe2\x98\x83", 1); CHECK(f.run() == 0); CHECK(f.contents() == stamp + "caf\xc3\xa9\n" + stamp + "\xe2\x98\x83"); }
  // U+010A contains a 0x0A byte but is not LF; surrogate pairs are preserved.
  { stream_fixture f(bom + wide_bytes(L"\x010a\xd83d\xde00\nX"), 1); CHECK(f.run() == 0); CHECK(f.contents() == bom + wide_stamp + wide_bytes(L"\x010a\xd83d\xde00\n") + wide_stamp + wide_bytes(L"X")); }
  { stream_fixture f(bom + wide_bytes(L"x\n") + std::string("\x0a", 1), 1); CHECK(f.run() == 0); CHECK(f.contents() == bom + wide_stamp + wide_bytes(L"x\n") + wide_stamp + std::string("\x0a", 1)); }
  // BOM-less UTF-16 retains the legacy IsTextUnicode heuristic.
  { stream_fixture f(wide_bytes(L"hello\n"), 1024); CHECK(f.run() == 0); CHECK(f.contents() == bom + wide_stamp + wide_bytes(L"hello\n")); }
  for (unsigned encoding = 0; encoding < 2; encoding++) {
    for (unsigned timestamps = 0; timestamps < 2; timestamps++) {
      const std::string line = encoding ? wide_bytes(L"abc\n") : "abc\n";
      const std::string tail = encoding ? wide_bytes(L"x") : "x";
      const std::string prefix = timestamps ? (encoding ? wide_stamp : stamp) : "";
      const std::string mark = encoding ? bom : "";
      const std::string archive = mark + prefix + line;
      for (int delta = -1; delta <= 1; delta++) {
        stream_fixture f(mark + line + tail, 1, timestamps != 0, archive.size() + delta);
        CHECK(f.run() == 0);
        if (delta > 0) { CHECK(rotations == 0); CHECK(f.contents() == archive + prefix + tail); }
        else {
          REQUIRE(rotated_paths.size() == 1); CHECK(rotations == 1);
          CHECK(read_path(rotated_paths[0].c_str()) == archive);
          CHECK(f.contents() == mark + prefix + tail);
        }
      }
    }
  }
  { stream_fixture f("abc\nx", 2, false, 1ULL << 63); CHECK(f.run() == 0); CHECK(! rotations); CHECK(f.contents() == "abc\nx"); }
  { stream_fixture f("abc\nx", 1, false); f.service->rotate_stdout_online = NSSM_ROTATE_ONLINE_ASAP; CHECK(f.run() == 0); CHECK(rotations == 1); CHECK(f.contents() == "x"); }
  { stream_fixture f("abc\nx", 1, false, 4); f.service->rotate_stdout_online = NSSM_ROTATE_OFFLINE; CHECK(f.run() == 0); CHECK(! rotations); CHECK(f.contents() == "abc\nx"); }
  { stream_fixture f(wide_bytes(L"abc\n"), 1); CHECK(f.run() == 0); CHECK(f.contents() == bom + wide_stamp + wide_bytes(L"abc\n")); }
  for (unsigned metadata = 0; metadata < 2; metadata++) {
    stream_fixture f("abc\nx", 2, false, 4);
    fail_info = true; fail_size = metadata != 0;
    CHECK(f.run() == 0); CHECK(rotations == 1); CHECK(f.contents() == "x");
  }
  { // An unknown existing size must not cause a BOM to be appended mid-file.
    stream_fixture f(bom + wide_bytes(L"x"), 1);
    HANDLE file = CreateFile(f.path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    REQUIRE(file != INVALID_HANDLE_VALUE); output(file, bom + wide_bytes(L"old\n")); CloseHandle(file);
    fail_info = fail_size = true;
    CHECK(f.run() == 0); CHECK(f.contents() == bom + wide_bytes(L"old\n") + wide_stamp + wide_bytes(L"x"));
  }
  { // Existing contents and an input BOM must not acquire a second BOM.
    stream_fixture f(bom + wide_bytes(L"x"), 1);
    HANDLE file = CreateFile(f.path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    REQUIRE(file != INVALID_HANDLE_VALUE); output(file, bom + wide_bytes(L"old\n")); CloseHandle(file);
    CHECK(f.run() == 0); CHECK(f.contents() == bom + wide_bytes(L"old\n") + wide_stamp + wide_bytes(L"x"));
  }
  for (unsigned fault = 0; fault < 3; fault++) {
    stream_fixture f("abc\nx", 1024, false, 4);
    f.service->stdout_copy_and_truncate = fault != 0;
    fail_move = fault == 0; fail_truncate = fault == 2;
    CHECK(f.run() == 0);
    CHECK(f.contents() == (fault == 1 ? "x" : "abc\nx"));
    CHECK(rotations == (fault == 1 ? 1U : 0U));
  }
  { stream_fixture f(bom + wide_bytes(L"one\ntwo"), 1); short_write = 1; CHECK(f.run() == 0); CHECK(f.contents() == bom + wide_stamp + wide_bytes(L"one\n") + wide_stamp + wide_bytes(L"two")); }
  { stream_fixture f("abc", 2); short_write = 0; CHECK(f.run() == 3); CHECK(write_errors == 1); CHECK(f.contents().empty()); }
  for (unsigned cut = 0; cut < 65; cut++) {
    stream_fixture f(bom + wide_bytes(L"abc\ndef"), 1);
    fail_after = cut;
    CHECK(f.run() == 3); CHECK(write_errors == 1);
    // The second line has its own timestamp; compare to the complete expected stream.
    const std::string result = bom + wide_stamp + wide_bytes(L"abc\n") + wide_stamp + wide_bytes(L"def");
    CHECK(f.contents() == result.substr(0, cut)); CHECK(bytes_written == cut);
  }
}
}
