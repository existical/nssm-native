#include "test.h"
#include <map>

namespace process_live_test {
bool active;
static std::vector<DWORD> allowed, visited;
bool owns(DWORD pid) {
  for (size_t i = 0; i < allowed.size(); i++) if (allowed[i] == pid) return true;
  return false;
}
static int visit(nssm_service_t *, kill_t *k) { visited.push_back(k->pid); return 1; }

// Helpers only start copies of this test executable, with no console or UI.
static bool start_child(unsigned depth, HANDLE report, HANDLE gate, DWORD parent, PROCESS_INFORMATION *pi) {
  TCHAR exe[PATH_LENGTH], command[CMD_LENGTH];
  if (! GetModuleFileName(NULL, exe, _countof(exe))) return false;
  if (_sntprintf_s(command, _countof(command), _TRUNCATE, _T("\"%s\" --tree-child %u %llu %llu %lu"),
      exe, depth, static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(report)),
      static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(gate)), parent) < 0) return false;
  STARTUPINFO si = {}; si.cb = sizeof(si);
  return CreateProcess(exe, command, NULL, NULL, TRUE, DETACHED_PROCESS, NULL, NULL, &si, pi) != FALSE;
}

struct fixture {
  HANDLE reader, writer, gate;
  PROCESS_INFORMATION root;
  std::vector<HANDLE> observers;
  fixture() : reader(NULL), writer(NULL), gate(NULL), root() {}
  ~fixture() {
    active = false; allowed.clear(); visited.clear();
    if (gate) SetEvent(gate);
    for (size_t i = 0; i < observers.size(); i++) {
      if (WaitForSingleObject(observers[i], 2000) != WAIT_OBJECT_0) {
        TerminateProcess(observers[i], 0); WaitForSingleObject(observers[i], 2000);
      }
      CloseHandle(observers[i]);
    }
    if (root.hProcess) {
      if (WaitForSingleObject(root.hProcess, 2000) != WAIT_OBJECT_0) {
        TerminateProcess(root.hProcess, 0); WaitForSingleObject(root.hProcess, 2000);
      }
      CloseHandle(root.hProcess);
    }
    if (root.hThread) CloseHandle(root.hThread);
    if (reader) CloseHandle(reader);
    if (writer) CloseHandle(writer);
    if (gate) CloseHandle(gate);
  }
  void start() {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    REQUIRE(CreatePipe(&reader, &writer, &sa, 0));
    REQUIRE(SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0));
    gate = CreateEvent(&sa, TRUE, FALSE, NULL); REQUIRE(gate);
    REQUIRE(start_child(2, writer, gate, GetCurrentProcessId(), &root));
    CloseHandle(root.hThread); root.hThread = NULL;
    CloseHandle(writer); writer = NULL;
    DWORD started = GetTickCount();
    std::map<DWORD, DWORD> parents;
    while (allowed.size() < 7 && GetTickCount() - started < 10000) {
      DWORD available;
      REQUIRE(PeekNamedPipe(reader, NULL, 0, NULL, &available, NULL));
      if (available < sizeof(DWORD) * 3) { Sleep(5); continue; }
      DWORD record[3], read;
      REQUIRE(ReadFile(reader, record, sizeof(record), &read, NULL) && read == sizeof(record));
      REQUIRE(! owns(record[0]));
      allowed.push_back(record[0]); parents[record[0]] = record[1];
      HANDLE observer = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE, FALSE, record[0]);
      REQUIRE(observer); observers.push_back(observer);
    }
    REQUIRE(allowed.size() == 7);
    for (std::map<DWORD, DWORD>::iterator i = parents.begin(); i != parents.end(); ++i) {
      CHECK(i->first == root.dwProcessId ? i->second == GetCurrentProcessId() : owns(i->second));
    }
    active = true; // Production OpenProcess/TerminateProcess are restricted to these fixture PIDs.
  }
};
}

int process_tree_child(int argc, TCHAR **argv) {
  if (argc != 6) return 2;
  unsigned depth = _ttoi(argv[2]);
  if (depth > 2) return 3;
  HANDLE report = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(_tcstoui64(argv[3], NULL, 10)));
  HANDLE gate = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(_tcstoui64(argv[4], NULL, 10)));
  DWORD record[] = { GetCurrentProcessId(), _tcstoul(argv[5], NULL, 10), depth };
  DWORD written;
  if (! WriteFile(report, record, sizeof(record), &written, NULL) || written != sizeof(record)) return 4;
  if (depth) {
    for (unsigned i = 0; i < 2; i++) {
      PROCESS_INFORMATION pi = {};
      if (! process_live_test::start_child(depth - 1, report, gate, record[0], &pi)) return 5;
      CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
  }
  CloseHandle(report);
  DWORD result = WaitForSingleObject(gate, 30000); // Bounded fallback if the parent test crashes.
  CloseHandle(gate);
  return result == WAIT_OBJECT_0 ? 0 : 6;
}

void test_process_live() {
  using namespace process_live_test;
  { fixture warmup; warmup.start(); } // Allow Windows' first-launch initialization before accounting.
  DWORD before = test_handle_count();
  for (unsigned already_exited = 0; already_exited < 2; already_exited++) {
    fixture f; f.start();
    kill_t k = {}; k.name = _T("Owned process-tree fixture");
    k.pid = f.root.dwProcessId; k.process_handle = f.root.hProcess; k.stop_method = NSSM_STOP_METHOD_TERMINATE;
    REQUIRE(! get_process_creation_time(k.process_handle, &k.creation_time));
    CHECK(walk_process_tree(NULL, visit, &k, k.pid) == 0);
    CHECK(visited.size() == 7);
    for (size_t i = 0; i < f.observers.size(); i++) CHECK(WaitForSingleObject(f.observers[i], 0) == WAIT_TIMEOUT);
    if (already_exited) {
      REQUIRE(TerminateProcess(k.process_handle, 0));
      REQUIRE(WaitForSingleObject(k.process_handle, 5000) == WAIT_OBJECT_0);
    }
    DWORD held = test_handle_count();
    CHECK(kill_process_tree(&k, k.pid) == 0);
    for (size_t i = 0; i < f.observers.size(); i++) CHECK(WaitForSingleObject(f.observers[i], 5000) == WAIT_OBJECT_0);
    CHECK(k.process_handle == f.root.hProcess && k.pid == f.root.dwProcessId);
    CHECK(test_handle_count() == held);
  }
  DWORD after = test_handle_count();
  CHECK(after == before);
  printf("Owned 7-process trees (live and exited roots): handles before=%lu after=%lu\n", before, after);
}
