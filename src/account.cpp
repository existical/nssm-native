#include "nssm.h"

#include <sddl.h>

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ERROR_SUCCESS
#endif

extern imports_t imports;

/* Keep each Windows allocation paired with its documented deallocator. */
template<class T> struct account_heap_t {
  T *value;
  account_heap_t() : value(0) {}
  ~account_heap_t() { if (value) HeapFree(GetProcessHeap(), 0, value); }
private:
  account_heap_t(const account_heap_t &);
  account_heap_t &operator=(const account_heap_t &);
};
template<class T> struct account_lsa_t {
  T *value;
  account_lsa_t() : value(0) {}
  ~account_lsa_t() { if (value) LsaFreeMemory(value); }
private:
  account_lsa_t(const account_lsa_t &);
  account_lsa_t &operator=(const account_lsa_t &);
};
struct account_policy_t {
  LSA_HANDLE value;
  account_policy_t() : value(0) {}
  ~account_policy_t() { if (value) LsaClose(value); }
private:
  account_policy_t(const account_policy_t &);
  account_policy_t &operator=(const account_policy_t &);
};

static bool valid_lsa_string(const LSA_UNICODE_STRING &s) {
  return !(s.Length % sizeof(wchar_t)) && s.Length <= s.MaximumLength && (! s.Length || s.Buffer);
}

/* Open Policy object. Lookup callers do not need policy modification rights. */
int open_lsa_policy(LSA_HANDLE *policy, ACCESS_MASK access) {
  if (! policy) return 1;
  *policy = 0;
  LSA_OBJECT_ATTRIBUTES attributes;
  ZeroMemory(&attributes, sizeof(attributes));
  NTSTATUS status = LsaOpenPolicy(0, &attributes, access, policy);
  if (status != STATUS_SUCCESS) {
    *policy = 0;
    print_message(stderr, NSSM_MESSAGE_LSAOPENPOLICY_FAILED, error_string(LsaNtStatusToWinError(status)));
    return 1;
  }
  return 0;
}

/* Successful output belongs to the caller and must be freed with HeapFree. */
int username_sid(const TCHAR *username, SID **sid, LSA_HANDLE *policy) {
  if (! sid) return 1;
  *sid = 0;
  if (! username || ! *username) return 1;
  account_policy_t owned_policy;
  if (! policy) {
    if (open_lsa_policy(&owned_policy.value, POLICY_LOOKUP_NAMES)) return 1;
    policy = &owned_policy.value;
  }

  /* LSA accepts COMPUTERNAME\user, but not the SCM's .\user spelling. */
  account_heap_t<TCHAR> expanded;
  const TCHAR *lookup = username;
  if (! _tcsnicmp(_T(".\\"), username, 2)) {
    TCHAR computername[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD length = _countof(computername);
    if (! GetComputerName(computername, &length)) return 2;
    size_t capacity = length + _tcslen(username);
    if (capacity > USHRT_MAX / sizeof(wchar_t)) return 4;
    expanded.value = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(TCHAR));
    if (! expanded.value) return 2;
    if (_sntprintf_s(expanded.value, capacity, _TRUNCATE, _T("%s\\%s"), computername, username + 2) < 0) return 2;
    lookup = expanded.value;
  }
  if (_tcslen(lookup) >= USHRT_MAX / sizeof(wchar_t)) return 4;
  account_heap_t<wchar_t> wide;
  unsigned long length = 0;
  if (to_utf16(lookup, &wide.value, &length) || length >= USHRT_MAX / sizeof(wchar_t)) return 4;
  LSA_UNICODE_STRING lsa_username;
  lsa_username.Buffer = wide.value;
  lsa_username.Length = (USHORT) (length * sizeof(wchar_t));
  lsa_username.MaximumLength = lsa_username.Length + sizeof(wchar_t);

  account_lsa_t<LSA_REFERENCED_DOMAIN_LIST> domains;
  account_lsa_t<LSA_TRANSLATED_SID> translated;
  NTSTATUS status = LsaLookupNames(*policy, 1, &lsa_username, &domains.value, &translated.value);
  if (status != STATUS_SUCCESS) {
    print_message(stderr, NSSM_MESSAGE_LSALOOKUPNAMES_FAILED, username, error_string(LsaNtStatusToWinError(status)));
    return 5;
  }
  if (! translated.value || ! domains.value || ! domains.value->Domains ||
      translated.value->DomainIndex < 0 || (ULONG) translated.value->DomainIndex >= domains.value->Entries) return 7;
  if (translated.value->Use != SidTypeUser && translated.value->Use != SidTypeWellKnownGroup) {
    if (translated.value->Use != SidTypeUnknown || _tcsnicmp(NSSM_VIRTUAL_SERVICE_ACCOUNT_DOMAIN _T("\\"), username, _tcslen(NSSM_VIRTUAL_SERVICE_ACCOUNT_DOMAIN) + 1)) return 6;
  }
  PSID domain = domains.value->Domains[translated.value->DomainIndex].Sid;
  if (! domain || ! IsValidSid(domain)) return 7;
  unsigned char n = *GetSidSubAuthorityCount(domain);
  if (n >= SID_MAX_SUB_AUTHORITIES) return 7;
  account_heap_t<SID> result;
  result.value = (SID *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, GetSidLengthRequired(n + 1));
  if (! result.value) return 8;
  if (! InitializeSid(result.value, GetSidIdentifierAuthority(domain), n + 1)) {
    print_message(stderr, NSSM_MESSAGE_INITIALIZESID_FAILED, username, error_string(GetLastError()));
    return 9;
  }
  for (unsigned char i = 0; i < n; i++) *GetSidSubAuthority(result.value, i) = *GetSidSubAuthority(domain, i);
  *GetSidSubAuthority(result.value, n) = translated.value->RelativeId;
  if (translated.value->Use == SidTypeWellKnownGroup && ! well_known_sid(result.value)) {
    print_message(stderr, NSSM_GUI_INVALID_USERNAME, username);
    return 10;
  }
  *sid = result.value;
  result.value = 0;
  return 0;
}

int username_sid(const TCHAR *username, SID **sid) {
  return username_sid(username, sid, 0);
}

int canonicalise_username(const TCHAR *username, TCHAR **canon) {
  if (! canon) return 1;
  *canon = 0;
  account_policy_t policy;
  if (open_lsa_policy(&policy.value, POLICY_LOOKUP_NAMES)) return 1;
  account_heap_t<SID> sid;
  if (username_sid(username, &sid.value, &policy.value)) return 2;
  PSID sids = sid.value;
  account_lsa_t<LSA_REFERENCED_DOMAIN_LIST> domains;
  account_lsa_t<LSA_TRANSLATED_NAME> translated;
  NTSTATUS status = LsaLookupSids(policy.value, 1, &sids, &domains.value, &translated.value);
  if (status != STATUS_SUCCESS) {
    print_message(stderr, NSSM_MESSAGE_LSALOOKUPSIDS_FAILED, error_string(LsaNtStatusToWinError(status)));
    return 3;
  }
  if (! translated.value || ! domains.value || ! domains.value->Domains ||
      translated.value->DomainIndex < 0 || (ULONG) translated.value->DomainIndex >= domains.value->Entries) return 3;
  const LSA_UNICODE_STRING &domain = domains.value->Domains[translated.value->DomainIndex].Name;
  const LSA_UNICODE_STRING &name = translated.value->Name;
  if (! valid_lsa_string(domain) || ! valid_lsa_string(name) || ! domain.Length || ! name.Length ||
      wmemchr(domain.Buffer, 0, domain.Length / sizeof(wchar_t)) || wmemchr(name.Buffer, 0, name.Length / sizeof(wchar_t))) return 3;
  /* Use size_t for the combined size: two USHORT lengths can overflow USHORT. */
  size_t bytes = (size_t) domain.Length + name.Length + 2 * sizeof(wchar_t);
  account_heap_t<wchar_t> combined;
  combined.value = (wchar_t *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
  if (! combined.value) return 9;
  memmove(combined.value, domain.Buffer, domain.Length);
  combined.value[domain.Length / sizeof(wchar_t)] = L'\\';
  memmove((char *) combined.value + domain.Length + sizeof(wchar_t), name.Buffer, name.Length);
  unsigned long canonlen;
  if (from_utf16(combined.value, canon, &canonlen)) return 10;
  return 0;
}

/* Do two usernames map to the same SID? */
int username_equiv(const TCHAR *a, const TCHAR *b) {
  SID *sid_a, *sid_b;
  if (username_sid(a, &sid_a)) return 0;

  if (username_sid(b, &sid_b)) {
    HeapFree(GetProcessHeap(), 0, sid_a);
    return 0;
  }

  int ret = 0;
  if (EqualSid(sid_a, sid_b)) ret = 1;

  HeapFree(GetProcessHeap(), 0, sid_a);
  HeapFree(GetProcessHeap(), 0, sid_b);

  return ret;
}

/* Does the username represent the LocalSystem account? */
int is_localsystem(const TCHAR *username) {
  if (str_equiv(username, NSSM_LOCALSYSTEM_ACCOUNT)) return 1;
  if (! imports.IsWellKnownSid) return 0;

  SID *sid;
  if (username_sid(username, &sid)) return 0;

  int ret = 0;
  if (imports.IsWellKnownSid(sid, WinLocalSystemSid)) ret = 1;

  HeapFree(GetProcessHeap(), 0, sid);

  return ret;
}

/* Build the virtual account name. */
TCHAR *virtual_account(const TCHAR *service_name) {
  size_t len = _tcslen(NSSM_VIRTUAL_SERVICE_ACCOUNT_DOMAIN) + _tcslen(service_name) + 2;
  TCHAR *name = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, len * sizeof(TCHAR));
  if (! name) {
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("name"), _T("virtual_account"));
    return 0;
  }

  _sntprintf_s(name, len, _TRUNCATE, _T("%s\\%s"), NSSM_VIRTUAL_SERVICE_ACCOUNT_DOMAIN, service_name);
  return name;
}

/* Does the username represent a virtual account for the service? */
int is_virtual_account(const TCHAR *service_name, const TCHAR *username) {
  if (! imports.IsWellKnownSid) return 0;
  if (! service_name) return 0;
  if (! username) return 0;

  TCHAR *canon = virtual_account(service_name);
  int ret = str_equiv(canon, username);
  HeapFree(GetProcessHeap(), 0, canon);
  return ret;
}

/*
  Get well-known alias for LocalSystem and friends.
  Returns a pointer to a static string.  DO NOT try to free it.
*/
const TCHAR *well_known_sid(SID *sid) {
  if (! imports.IsWellKnownSid) return 0;
  if (imports.IsWellKnownSid(sid, WinLocalSystemSid)) return NSSM_LOCALSYSTEM_ACCOUNT;
  if (imports.IsWellKnownSid(sid, WinLocalServiceSid)) return NSSM_LOCALSERVICE_ACCOUNT;
  if (imports.IsWellKnownSid(sid, WinNetworkServiceSid)) return NSSM_NETWORKSERVICE_ACCOUNT;
  return 0;
}

const TCHAR *well_known_username(const TCHAR *username) {
  if (! username) return NSSM_LOCALSYSTEM_ACCOUNT;
  if (str_equiv(username, NSSM_LOCALSYSTEM_ACCOUNT)) return NSSM_LOCALSYSTEM_ACCOUNT;
  SID *sid;
  if (username_sid(username, &sid)) return 0;

  const TCHAR *well_known = well_known_sid(sid);
  HeapFree(GetProcessHeap(), 0, sid);

  return well_known;
}

int grant_logon_as_service(const TCHAR *username) {
  if (! username) return 0;
  account_policy_t policy;
  if (open_lsa_policy(&policy.value)) return 1;
  account_heap_t<SID> sid;
  if (username_sid(username, &sid.value, &policy.value)) return 2;
  if (well_known_sid(sid.value)) return 3;

  LSA_UNICODE_STRING lsa_right;
  lsa_right.Buffer = NSSM_LOGON_AS_SERVICE_RIGHT;
  lsa_right.Length = (USHORT) (wcslen(lsa_right.Buffer) * sizeof(wchar_t));
  lsa_right.MaximumLength = lsa_right.Length + sizeof(wchar_t);
  account_lsa_t<LSA_UNICODE_STRING> rights;
  unsigned long count = 0;
  NTSTATUS status = LsaEnumerateAccountRights(policy.value, sid.value, &rights.value, &count);
  if (status != STATUS_SUCCESS) {
    unsigned long error = LsaNtStatusToWinError(status);
    if (error != ERROR_FILE_NOT_FOUND) {
      print_message(stderr, NSSM_MESSAGE_LSAENUMERATEACCOUNTRIGHTS_FAILED, username, error_string(error));
      return 4;
    }
    count = 0;
  }
  if (count && ! rights.value) return 4;
  for (unsigned long i = 0; i < count; i++) {
    const LSA_UNICODE_STRING &right = rights.value[i];
    if (! valid_lsa_string(right)) return 4;
    if (right.Length != lsa_right.Length) continue;
    if (! _wcsnicmp(right.Buffer, lsa_right.Buffer, right.Length / sizeof(wchar_t))) return 0;
  }
  status = LsaAddAccountRights(policy.value, sid.value, &lsa_right, 1);
  if (status != STATUS_SUCCESS) {
    print_message(stderr, NSSM_MESSAGE_LSAADDACCOUNTRIGHTS_FAILED, error_string(LsaNtStatusToWinError(status)));
    return 5;
  }
  print_message(stdout, NSSM_MESSAGE_GRANTED_LOGON_AS_SERVICE, username);
  return 0;
}
