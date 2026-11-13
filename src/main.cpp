// wifiForce - keeps Wi-Fi connected while a LAN cable is plugged in.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wlanapi.h>
#include <winhttp.h>
#include <shellapi.h>
#include <string>

// Written by tools\build-files.ps1 from version.txt: the window title carries
// the same number as the file properties and the build's file name.
#include "version_generated.h"

// ---------------------------------------------------------------- scripts
static const char* APPLY_PS = R"PS(
$ErrorActionPreference = 'SilentlyContinue'
$dir = Join-Path $env:ProgramData 'wifiForce'
$bf  = Join-Path $dir 'backup.json'
$k   = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WcmSvc\GroupPolicy'
$want = [ordered]@{ fMinimizeConnections = 0; fBlockNonDomain = 0; fDisablePowerManagement = 1 }
$wifi = @(Get-NetAdapter | Where-Object { $_.PhysicalMediaType -match '802\.11|Wireless' -or $_.InterfaceDescription -match 'Wi-?Fi|Wireless|802\.11' })
$names = @(netsh wlan show profiles | ForEach-Object { if ($_ -match '^\s{2,}\S[^:]*:\s+(.+?)\s*$') { $Matches[1] } })

if (-not (Test-Path $bf)) {
  $b = [ordered]@{ reg=@(); adv=@(); pm=@(); prof=@(); state=@(); svc=@() }
  foreach ($n in $want.Keys) {
    $v = (Get-ItemProperty -Path $k -Name $n).$n
    $b.reg += [pscustomobject]@{ name=$n; exists=($null -ne $v); value=$v }
  }
  foreach ($a in $wifi) {
    # Activate switches a disabled Wi-Fi adapter on, so its state is recorded too.
    $b.state += [pscustomobject]@{ adapter=$a.Name; status=[string]$a.Status }
    foreach ($p in (Get-NetAdapterAdvancedProperty -Name $a.Name)) {
      if ($p.DisplayName -match 'disable.*(wired|ethernet|\blan\b)') {
        $b.adv += [pscustomobject]@{ adapter=$a.Name; display=$p.DisplayName; value=[string]$p.DisplayValue }
      }
    }
    $pm = Get-NetAdapterPowerManagement -Name $a.Name
    if ($pm -and ([string]$pm.AllowComputerToTurnOffDevice) -ne 'Unsupported') {
      $b.pm += [pscustomobject]@{ adapter=$a.Name; value=[string]$pm.AllowComputerToTurnOffDevice }
    }
  }
  # Step 4 sets WlanSvc to Automatic; remember what it was.
  $svc = Get-Service WlanSvc
  if ($svc) { $b.svc += [pscustomobject]@{ name='WlanSvc'; startup=[string]$svc.StartType } }
  $tmp = Join-Path $env:TEMP 'wfexp'
  New-Item -ItemType Directory -Force $tmp | Out-Null
  foreach ($nm in $names) {
    Remove-Item "$tmp\*" -Force
    netsh wlan export profile name="$nm" folder="$tmp" | Out-Null
    $f = Get-ChildItem $tmp -Filter *.xml | Select-Object -First 1
    if ($f) {
      $x = [xml](Get-Content $f.FullName -Raw)
      $b.prof += [pscustomobject]@{ name=$nm; mode=[string]$x.WLANProfile.connectionMode }
    }
  }
  Remove-Item $tmp -Recurse -Force
  New-Item -ItemType Directory -Force $dir | Out-Null
  ($b | ConvertTo-Json -Depth 5) | Set-Content -Path $bf -Encoding UTF8

  # Read the record back before a single setting is changed. A truncated backup
  # is worse than none at all: Restore would trust it and then delete it.
  $check = $null
  try { $check = Get-Content $bf -Raw | ConvertFrom-Json } catch { }
  if (-not $check -or -not $check.reg) {
    Remove-Item $bf -Force
    exit 2
  }
}

# 1) Windows policy: do NOT minimize simultaneous connections
New-Item -Path $k -Force | Out-Null
foreach ($n in $want.Keys) {
  New-ItemProperty -Path $k -Name $n -Value $want[$n] -PropertyType DWord -Force | Out-Null
}

# 2) Wi-Fi adapter: no "disable on wired connect", no power-saving shutdown
$changed = @()
foreach ($a in $wifi) {
  foreach ($p in (Get-NetAdapterAdvancedProperty -Name $a.Name)) {
    if ($p.DisplayName -match 'disable.*(wired|ethernet|\blan\b)') {
      $off = $p.ValidDisplayValues | Where-Object { $_ -match '^(disabled|off|never|no)$' } | Select-Object -First 1
      if ($off -and ([string]$p.DisplayValue) -ne $off) {
        Set-NetAdapterAdvancedProperty -Name $a.Name -DisplayName $p.DisplayName -DisplayValue $off -NoRestart
        $changed += $a.Name
      }
    }
  }
  $pm = Get-NetAdapterPowerManagement -Name $a.Name
  if ($pm -and ([string]$pm.AllowComputerToTurnOffDevice) -ne 'Unsupported') {
    Set-NetAdapterPowerManagement -Name $a.Name -AllowComputerToTurnOffDevice Disabled
  }
  Enable-NetAdapter -Name $a.Name -Confirm:$false
}
foreach ($n in ($changed | Select-Object -Unique)) { Restart-NetAdapter -Name $n -Confirm:$false }

# 3) Every saved Wi-Fi profile: connect automatically
foreach ($nm in $names) { netsh wlan set profileparameter name="$nm" connectionmode=auto | Out-Null }

# 4) Services
Set-Service WlanSvc -StartupType Automatic
Start-Service WlanSvc
Start-Service WcmSvc

# The policy values are the part Activate cannot do half way: if they are not
# there, say so instead of reporting success.
foreach ($n in $want.Keys) {
  $v = (Get-ItemProperty -Path $k -Name $n).$n
  if ("$v" -ne "$($want[$n])") { exit 3 }
}
exit 0
)PS";

static const char* RESTORE_PS = R"PS(
$ErrorActionPreference = 'SilentlyContinue'
$dir = Join-Path $env:ProgramData 'wifiForce'
$bf  = Join-Path $dir 'backup.json'
$k   = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WcmSvc\GroupPolicy'
if (-not (Test-Path $bf)) { exit 0 }
$b = $null
try { $b = Get-Content $bf -Raw | ConvertFrom-Json } catch { }
if (-not $b) { exit 3 }

foreach ($r in @($b.reg)) {
  if ($r.exists) { New-ItemProperty -Path $k -Name $r.name -Value $r.value -PropertyType DWord -Force | Out-Null }
  else { Remove-ItemProperty -Path $k -Name $r.name -Force }
}
$item = Get-Item $k
if ($item -and $item.ValueCount -eq 0 -and $item.SubKeyCount -eq 0) { Remove-Item $k -Force }

$adapters = @()
foreach ($p in @($b.adv)) {
  Set-NetAdapterAdvancedProperty -Name $p.adapter -DisplayName $p.display -DisplayValue $p.value -NoRestart
  $adapters += $p.adapter
}
foreach ($p in @($b.pm)) { Set-NetAdapterPowerManagement -Name $p.adapter -AllowComputerToTurnOffDevice $p.value }
foreach ($p in @($b.prof)) {
  if ($p.mode) { netsh wlan set profileparameter name="$($p.name)" connectionmode=$($p.mode) | Out-Null }
}
foreach ($n in ($adapters | Select-Object -Unique)) { Restart-NetAdapter -Name $n -Confirm:$false }

# Adapters that were switched off before Activate go back to off.
foreach ($s in @($b.state)) {
  if ($s -and $s.status -eq 'Disabled') { Disable-NetAdapter -Name $s.adapter -Confirm:$false }
}

# The startup type step 4 changed goes back to what it was.
foreach ($s in @($b.svc)) {
  if ($s -and $s.startup) { Set-Service -Name $s.name -StartupType $s.startup }
}

# Check before the record is deleted: if the policy values are not back, the
# backup is the only copy of the originals and has to stay for another try.
$bad = 0
foreach ($r in @($b.reg)) {
  $v = (Get-ItemProperty -Path $k -Name $r.name).$($r.name)
  if ($r.exists) {
    if ("$v" -ne "$($r.value)") { $bad++ }
  } else {
    if ($null -ne $v) { $bad++ }
  }
}
if ($bad -gt 0) { exit 4 }

Remove-Item $bf -Force
if (Test-Path $bf) { exit 5 }
exit 0
)PS";

static const char* ENABLE_PS = R"PS(
Get-NetAdapter | Where-Object { $_.Status -eq 'Disabled' -and ($_.PhysicalMediaType -match '802\.11|Wireless' -or $_.InterfaceDescription -match 'Wi-?Fi|Wireless|802\.11') } | Enable-NetAdapter -Confirm:$false
)PS";

// ---------------------------------------------------------------- helpers
static std::wstring DataDir() {
    wchar_t b[MAX_PATH] = {0};
    GetEnvironmentVariableW(L"ProgramData", b, MAX_PATH);
    return std::wstring(b) + L"\\wifiForce";
}
static std::wstring SvcExe() { return DataDir() + L"\\wifiForceSvc.exe"; }

static DWORD Run(const std::wstring& cmd, bool wait = true, DWORD timeout = 180000) {
    STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring c = cmd;
    if (!CreateProcessW(nullptr, &c[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return (DWORD)-1;
    DWORD code = 0;
    if (wait) {
        // A child that never exits would otherwise leave the window on
        // "Working... please wait" for ever and keep its own children alive.
        if (WaitForSingleObject(pi.hProcess, timeout) == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, (UINT)-1);
            WaitForSingleObject(pi.hProcess, 2000);
            code = (DWORD)-2;                       // "timed out", never 0
        } else {
            GetExitCodeProcess(pi.hProcess, &code);
        }
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return code;
}
static bool WriteBytes(const std::wstring& path, const void* data, DWORD len) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0; BOOL ok = WriteFile(h, data, len, &w, nullptr);
    CloseHandle(h); return ok && w == len;
}
static bool WriteText(const std::wstring& path, const char* text) {
    const char bom[] = "\xEF\xBB\xBF";
    std::string s = std::string(bom, 3) + text;
    return WriteBytes(path, s.data(), (DWORD)s.size());
}
static void RunPS(const std::wstring& file, DWORD* code = nullptr, DWORD timeout = 180000) {
    DWORD c = Run(L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + file + L"\"",
                  true, timeout);
    if (code) *code = c;
}

// ---------------------------------------------------------------- watchdog
static void EnforceRegistry() {
    HKEY h;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WcmSvc\\GroupPolicy",
                        0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return;
    struct V { const wchar_t* n; DWORD v; } vals[] = {
        {L"fMinimizeConnections", 0}, {L"fBlockNonDomain", 0}, {L"fDisablePowerManagement", 1}};
    for (auto& x : vals) {
        DWORD cur = 0xFFFFFFFF, sz = sizeof cur, type = 0;
        if (RegQueryValueExW(h, x.n, nullptr, &type, (LPBYTE)&cur, &sz) != ERROR_SUCCESS || type != REG_DWORD || cur != x.v)
            RegSetValueExW(h, x.n, 0, REG_DWORD, (const BYTE*)&x.v, sizeof x.v);
    }
    RegCloseKey(h);
}

static std::wstring LoadLast() {
    HANDLE h = CreateFileW((DataDir() + L"\\last.bin").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    wchar_t buf[256] = {0}; DWORD r = 0;
    ReadFile(h, buf, sizeof(buf) - sizeof(wchar_t), &r, nullptr);
    CloseHandle(h);
    return buf;
}
static void SaveLast(const std::wstring& s) { WriteBytes(DataDir() + L"\\last.bin", s.c_str(), (DWORD)(s.size() * sizeof(wchar_t))); }

static void WlanTick(std::wstring& last, ULONGLONG& lastEnable, ULONGLONG& lastSvcStart) {
    static ULONGLONG lastConnect = 0;
    HANDLE h = nullptr; DWORD ver = 0;
    if (WlanOpenHandle(2, nullptr, &ver, &h) != ERROR_SUCCESS) {
        // WLAN AutoConfig stopped? start it again (at most every 20 s). This has
        // its own throttle: sharing one timestamp with the adapter enable below
        // would let a service restart postpone that for a minute.
        if (GetTickCount64() - lastSvcStart > 20000) { lastSvcStart = GetTickCount64(); Run(L"sc start WlanSvc", false); }
        return;
    }
    PWLAN_INTERFACE_INFO_LIST list = nullptr;
    if (WlanEnumInterfaces(h, nullptr, &list) == ERROR_SUCCESS && list) {
        if (list->dwNumberOfItems == 0 && GetTickCount64() - lastEnable > 60000) {
            lastEnable = GetTickCount64();
            WriteText(DataDir() + L"\\enable.ps1", ENABLE_PS);
            // Short timeout on purpose: this runs on the watchdog loop, and a
            // stuck PowerShell must not hold the reconnect checks up for
            // minutes.
            RunPS(DataDir() + L"\\enable.ps1", nullptr, 30000);
        }
        for (DWORD i = 0; i < list->dwNumberOfItems; i++) {
            WLAN_INTERFACE_INFO& info = list->InterfaceInfo[i];
            GUID guid = info.InterfaceGuid;

            // software radio switched off? switch it on.
            PWLAN_RADIO_STATE rs = nullptr; DWORD sz = 0;
            if (WlanQueryInterface(h, &guid, wlan_intf_opcode_radio_state, nullptr, &sz, (PVOID*)&rs, nullptr) == ERROR_SUCCESS && rs) {
                for (DWORD k = 0; k < rs->dwNumberOfPhys; k++) {
                    if (rs->PhyRadioState[k].dot11SoftwareRadioState == dot11_radio_state_off &&
                        rs->PhyRadioState[k].dot11HardwareRadioState == dot11_radio_state_on) {
                        WLAN_PHY_RADIO_STATE s{};
                        s.dwPhyIndex = rs->PhyRadioState[k].dwPhyIndex;
                        s.dot11SoftwareRadioState = dot11_radio_state_on;
                        WlanSetInterface(h, &guid, wlan_intf_opcode_radio_state, sizeof s, &s, nullptr);
                    }
                }
                WlanFreeMemory(rs);
            }

            if (info.isState == wlan_interface_state_connected) {
                PWLAN_CONNECTION_ATTRIBUTES ca = nullptr; DWORD cs = 0;
                if (WlanQueryInterface(h, &guid, wlan_intf_opcode_current_connection, nullptr, &cs, (PVOID*)&ca, nullptr) == ERROR_SUCCESS && ca) {
                    std::wstring p = ca->strProfileName;
                    if (!p.empty() && p != last) { last = p; SaveLast(last); }
                    WlanFreeMemory(ca);
                }
            } else if (info.isState == wlan_interface_state_disconnected ||
                       info.isState == wlan_interface_state_not_ready ||
                       info.isState == wlan_interface_state_disconnecting) {
                std::wstring profile = last;
                PWLAN_PROFILE_INFO_LIST pl = nullptr;
                bool found = false; std::wstring first;
                if (WlanGetProfileList(h, &guid, nullptr, &pl) == ERROR_SUCCESS && pl) {
                    for (DWORD k = 0; k < pl->dwNumberOfItems; k++) {
                        if (first.empty()) first = pl->ProfileInfo[k].strProfileName;
                        if (profile == pl->ProfileInfo[k].strProfileName) found = true;
                    }
                    WlanFreeMemory(pl);
                }
                if (!found) profile = first;
                if (!profile.empty() && GetTickCount64() - lastConnect >= 8000) {
                    lastConnect = GetTickCount64();
                    WLAN_CONNECTION_PARAMETERS cp{};
                    cp.wlanConnectionMode = wlan_connection_mode_profile;
                    cp.strProfile = profile.c_str();
                    cp.dot11BssType = dot11_BSS_type_any;
                    WlanConnect(h, &guid, &cp, nullptr);
                }
            }
        }
        WlanFreeMemory(list);
    }
    WlanCloseHandle(h, nullptr);
}

static int Watchdog() {
    // Without the record there is nothing to restore to, so enforcing the policy
    // would be a change nobody can undo. Activate writes the record before the
    // tasks exist, so this only fires when the data folder was removed by hand.
    if (GetFileAttributesW((DataDir() + L"\\backup.json").c_str()) == INVALID_FILE_ATTRIBUTES) return 0;
    HANDLE m = CreateMutexW(nullptr, TRUE, L"Global\\wifiForceWatchdogMutex");
    if (!m || GetLastError() == ERROR_ALREADY_EXISTS) return 0;   // already running
    std::wstring last = LoadLast();
    ULONGLONG lastEnable = 0, lastSvcStart = 0;
    for (;;) {
        EnforceRegistry();
        WlanTick(last, lastEnable, lastSvcStart);
        Sleep(3000);
    }
}

// ---------------------------------------------------------------- activate / restore
static DWORD g_lastExit = 0;    // last script exit code, for the error message

static bool IsActive() {
    return GetFileAttributesW((DataDir() + L"\\backup.json").c_str()) != INVALID_FILE_ATTRIBUTES &&
           Run(L"schtasks /Query /TN wifiForceWatchdog") == 0;
}

static bool Activate() {
    std::wstring dir = DataDir();
    g_lastExit = 0;
    CreateDirectoryW(dir.c_str(), nullptr);
    Run(L"taskkill /F /IM wifiForceSvc.exe");
    Sleep(500);

    WriteText(dir + L"\\apply.ps1", APPLY_PS);
    WriteText(dir + L"\\restore.ps1", RESTORE_PS);
    DWORD code = 1; RunPS(dir + L"\\apply.ps1", &code);
    if (code != 0) { g_lastExit = code; return false; }
    // apply.ps1 writes the record before it changes anything, and refuses to
    // continue when it could not read it back; without it there is nothing to
    // restore from, so a missing one means Activate did not really happen.
    if (GetFileAttributesW((dir + L"\\backup.json").c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    bool copied = false;
    for (int i = 0; i < 10 && !copied; ++i) {
        copied = CopyFileW(self, SvcExe().c_str(), FALSE) != 0;
        if (!copied) Sleep(200);   // the running copy may still hold the file
    }
    if (!copied) return false;

    std::wstring tr = L"\\\"" + SvcExe() + L"\\\" --watchdog";
    DWORD a = Run(L"schtasks /Create /TN wifiForceWatchdog /TR \"" + tr + L"\" /SC ONSTART /RU SYSTEM /RL HIGHEST /F");
    DWORD b = Run(L"schtasks /Create /TN wifiForceGuard /TR \"" + tr + L"\" /SC MINUTE /MO 1 /RU SYSTEM /RL HIGHEST /F");
    if (a != 0 || b != 0) {
        // Never leave half of the pair behind: a guard without a watchdog, or
        // the reverse, is a task that can only fail. The record stays, so the
        // applied settings can still be undone with Restore.
        Run(L"schtasks /Delete /TN wifiForceGuard /F");
        Run(L"schtasks /Delete /TN wifiForceWatchdog /F");
        return false;
    }
    // schtasks defaults stop tasks on battery and after 72 h; lift both limits.
    DWORD s = Run(L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \""
                  L"$s=New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries "
                  L"-StartWhenAvailable -ExecutionTimeLimit (New-TimeSpan -Seconds 0); "
                  L"foreach($n in 'wifiForceWatchdog','wifiForceGuard'){Set-ScheduledTask -TaskName $n -Settings $s | Out-Null}\"");
    if (s != 0) return false;
    Run(L"schtasks /Run /TN wifiForceWatchdog");
    return true;
}

// A file that a stopping task or the killed service still holds open cannot be
// deleted on the first try; give it a moment instead of reporting success while
// the folder stays behind.
static bool RemoveWithRetry(const std::wstring& path) {
    for (int i = 0; i < 10; ++i) {
        if (DeleteFileW(path.c_str())) return true;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
        Sleep(200);
    }
    return false;
}

static bool Restore() {
    std::wstring dir = DataDir();
    g_lastExit = 0;
    Run(L"schtasks /Delete /TN wifiForceGuard /F");
    Run(L"schtasks /Delete /TN wifiForceWatchdog /F");
    Run(L"taskkill /F /IM wifiForceSvc.exe");
    Sleep(800);
    DWORD code = 1;
    CreateDirectoryW(dir.c_str(), nullptr);
    WriteText(dir + L"\\restore.ps1", RESTORE_PS);
    RunPS(dir + L"\\restore.ps1", &code);
    // restore.ps1 keeps backup.json when a value did not come back, so the
    // restore can simply be run again; leaving everything in place is the
    // point of that. Only a clean restore is followed by the cleanup.
    if (code != 0) { g_lastExit = code; return false; }
    const wchar_t* files[] = {L"\\wifiForceSvc.exe", L"\\apply.ps1", L"\\restore.ps1", L"\\enable.ps1", L"\\last.bin"};
    bool clean = true;
    for (auto f : files) if (!RemoveWithRetry(dir + f)) clean = false;
    if (!clean) return false;
    for (int i = 0; i < 5; ++i) {
        if (RemoveDirectoryW(dir.c_str())) break;
        Sleep(200);
    }
    return GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES;
}

// ---------------------------------------------------------------- GUI
enum { ID_ACT = 101, ID_RST = 102 };
static HWND g_wnd, g_stat, g_note, g_act, g_rst;
static volatile LONG g_checking = 0;
static bool g_busy = false;

// ---------------------------------------------------------------- update check
// The only network call the program makes, and only in GUI mode: ask GitHub for
// the newest release tag. Every failure - offline, blocked, rate limited,
// unparsable - is silent, so a machine without internet behaves exactly as it
// did before. The watchdog never calls out.
static const wchar_t* kReleasePage = L"https://github.com/finnuen/wifiForce/releases/latest";
static std::wstring g_latest;

static bool IsNewer(const std::wstring& candidate, const std::wstring& current) {
    int a[4] = {0, 0, 0, 0}, b[4] = {0, 0, 0, 0};
    auto fill = [](const std::wstring& s, int (&out)[4]) {
        int i = 0;
        for (wchar_t c : s) {
            if (c == L'.') { if (++i > 3) return; continue; }
            if (c < L'0' || c > L'9' || out[i] > 9999) { out[0] = -1; return; }
            out[i] = out[i] * 10 + (c - L'0');
        }
    };
    fill(candidate, a); fill(current, b);
    if (a[0] < 0 || b[0] < 0) return false;      // unparsable: say nothing
    for (int i = 0; i < 4; ++i) if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

static std::wstring LatestVersion() {
    std::wstring tag;
    HINTERNET session = WinHttpOpen(L"wifiForce/" WIFIFFORCE_VERSION_DOTTED,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return tag;
    // Never let a check hold the program up: the timeouts are short and this
    // runs on its own thread.
    WinHttpSetTimeouts(session, 3000, 3000, 3000, 5000);
    HINTERNET conn = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", L"/repos/finnuen/wifiForce/releases/latest",
                                              nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    std::string body;
    if (req && WinHttpSendRequest(req, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, size = sizeof status;
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) &&
            status == 200) {
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(req, &chunk[0], avail, &read) || read == 0) break;
                body.append(chunk.data(), read);
                if (body.size() > 262144) break;   // a release body is a few hundred bytes
            }
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);

    // "tag_name": "1.2"
    size_t key = body.find("\"tag_name\"");
    if (key == std::string::npos) return tag;
    size_t open = body.find('"', body.find(':', key));
    if (open == std::string::npos) return tag;
    size_t shut = body.find('"', open + 1);
    if (shut == std::string::npos) return tag;
    std::string raw = body.substr(open + 1, shut - open - 1);
    if (!raw.empty() && (raw[0] == 'v' || raw[0] == 'V')) raw.erase(0, 1);
    return std::wstring(raw.begin(), raw.end());
}

static void OpenReleasePage() {
    // explorer.exe starts at the user's own integrity level, so the browser does
    // not inherit this program's administrator rights.
    if ((INT_PTR)ShellExecuteW(nullptr, L"open", L"explorer.exe", kReleasePage, nullptr, SW_SHOWNORMAL) <= 32)
        ShellExecuteW(nullptr, L"open", kReleasePage, nullptr, nullptr, SW_SHOWNORMAL);
}

enum { ID_DL = 201, ID_LATER = 202 };

static LRESULT CALLBACK UpdateProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND a = CreateWindowW(L"STATIC", L"A newer version of wifiForce is available.",
            WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 16, 380, 20, h, nullptr, nullptr, nullptr);
        std::wstring line = L"Installed " WIFIFFORCE_VERSION_DOTTED L"    -    newest " + g_latest;
        HWND b = CreateWindowW(L"STATIC", line.c_str(),
            WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 40, 380, 20, h, nullptr, nullptr, nullptr);
        HWND c = CreateWindowW(L"STATIC", L"Download opens the release page in your browser.",
            WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 62, 380, 20, h, nullptr, nullptr, nullptr);
        HWND d = CreateWindowW(L"BUTTON", L"Download latest",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 36, 94, 168, 32, h, (HMENU)ID_DL, nullptr, nullptr);
        HWND e = CreateWindowW(L"BUTTON", L"Close",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 216, 94, 128, 32, h, (HMENU)ID_LATER, nullptr, nullptr);
        for (HWND x : {a, b, c, d, e}) SendMessageW(x, WM_SETFONT, (WPARAM)f, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == ID_DL) { OpenReleasePage(); DestroyWindow(h); }
        else if (LOWORD(w) == ID_LATER) DestroyWindow(h);
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// Modal, so the notice cannot be clicked away by accident: the main window is
// disabled until it is dismissed.
static void ShowUpdateNotice() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc; wc.lpfnWndProc = UpdateProc; wc.hInstance = inst;
    wc.lpszClassName = L"wifiForceUpdateWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    RegisterClassExW(&wc);

    RECT o{};
    GetWindowRect(g_wnd, &o);
    const int w = 420, hgt = 172;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"wifiForceUpdateWnd", L"wifiForce update",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        o.left + ((o.right - o.left) - w) / 2, o.top + ((o.bottom - o.top) - hgt) / 2,
        w, hgt, g_wnd, nullptr, inst, nullptr);
    if (!dlg) return;

    EnableWindow(g_wnd, FALSE);
    bool quit = false;
    MSG m;
    while (IsWindow(dlg) && IsWindow(g_wnd)) {
        if (!GetMessageW(&m, nullptr, 0, 0)) { quit = true; break; }
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    if (IsWindow(dlg)) DestroyWindow(dlg);
    if (IsWindow(g_wnd)) { EnableWindow(g_wnd, TRUE); SetActiveWindow(g_wnd); }
    if (quit) PostQuitMessage(0);   // the main window is gone: let the program end
}

static DWORD WINAPI UpdateCheck(LPVOID) {
    const std::wstring current = L"" WIFIFFORCE_VERSION_DOTTED;
    std::wstring latest = LatestVersion();
    if (!latest.empty() && IsNewer(latest, current))
        PostMessageW(g_wnd, WM_APP + 3, 0, (LPARAM)new std::wstring(latest));
    return 0;
}

static void ShowStatus(bool on) {
    SetWindowTextW(g_stat, on ? L"Status: ACTIVE  -  Wi-Fi is forced to stay connected"
                              : L"Status: default Windows behavior");
    if (!g_busy) { EnableWindow(g_act, TRUE); EnableWindow(g_rst, TRUE); }
}

// IsActive() runs schtasks, which can take up to a second on a busy machine, so
// it runs on a worker thread and answers with WM_APP+2. The UI thread never
// waits for it, not even at startup.
static DWORD WINAPI Checker(LPVOID) {
    bool on = IsActive();
    PostMessageW(g_wnd, WM_APP + 2, on ? 1 : 0, 0);
    return 0;
}

static void RefreshAsync() {
    if (InterlockedExchange(&g_checking, 1) != 0) return;   // one check at a time
    SetWindowTextW(g_stat, L"Checking status...");
    HANDLE t = CreateThread(nullptr, 0, Checker, nullptr, 0, nullptr);
    if (t) CloseHandle(t);   // the thread keeps running; the handle is not needed
}

static DWORD WINAPI Worker(LPVOID p) {
    bool ok = ((INT_PTR)p == ID_ACT) ? Activate() : Restore();
    PostMessageW(g_wnd, WM_APP + 1, ok, (LPARAM)(INT_PTR)p);
    return 0;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_wnd = h;   // the status check posts back before CreateWindowW returns
        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND t = CreateWindowW(L"STATIC", L"Keep Wi-Fi connected with LAN cable", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 14, 380, 20, h, nullptr, nullptr, nullptr);
        g_stat = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 44, 380, 20, h, nullptr, nullptr, nullptr);
        g_act = CreateWindowW(L"BUTTON", L"Activate", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 30, 80, 160, 44, h, (HMENU)ID_ACT, nullptr, nullptr);
        g_rst = CreateWindowW(L"BUTTON", L"Restore to default", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 210, 80, 160, 44, h, (HMENU)ID_RST, nullptr, nullptr);
        // Two lines of room: the sentence wraps rather than clipping when the
        // user's dialog font is wider than the default. It is only a warning, so
        // it never changes - which also means the red draw is safe.
        g_note = CreateWindowW(L"STATIC", L"Wi-Fi cannot be disconnected while wifiForce is active.",
                               WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 132, 380, 34, h, nullptr, nullptr, nullptr);
        for (HWND c : {t, g_stat, g_note, g_act, g_rst}) SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        RefreshAsync();
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        // The warning is drawn in red on the dialog's own face colour, so it sits
        // on the background instead of in a box. The other statics keep the
        // default look.
        if ((HWND)l == g_note) {
            SetTextColor((HDC)w, RGB(0xC0, 0x30, 0x2B));
            SetBkMode((HDC)w, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(w) == ID_ACT || LOWORD(w) == ID_RST) {
            g_busy = true;
            EnableWindow(g_act, FALSE); EnableWindow(g_rst, FALSE);
            SetWindowTextW(g_stat, L"Working... please wait");
            HANDLE t = CreateThread(nullptr, 0, Worker, (LPVOID)(INT_PTR)LOWORD(w), 0, nullptr);
            if (t) CloseHandle(t);
        }
        return 0;
    case WM_APP + 1:
        g_busy = false;
        RefreshAsync();
        if (!w) {
            // The script's own exit code is the only clue to what went wrong
            // (2 = backup failed, 3/4 = a value did not take, 5 = cleanup).
            std::wstring msg = L"Something went wrong";
            if (g_lastExit) msg += L" (script exit code " + std::to_wstring(g_lastExit) + L")";
            msg += L". Make sure PowerShell and Task Scheduler are available, then try again.";
            MessageBoxW(h, msg.c_str(), L"wifiForce", MB_ICONWARNING);
        }
        return 0;
    case WM_APP + 2:
        InterlockedExchange(&g_checking, 0);
        // A check started before the last click may answer while Activate or
        // Restore is still running; that answer is stale.
        if (!g_busy) ShowStatus(w != 0);
        return 0;
    case WM_APP + 3: {
        std::wstring* v = (std::wstring*)l;
        if (v) {
            g_latest = *v;
            delete v;
            ShowUpdateNotice();
        }
        return 0;
    }
    case WM_CLOSE:
        // Closing in the middle of Activate or Restore would kill the worker
        // thread with the process and leave the machine half changed.
        if (g_busy) {
            MessageBoxW(h, L"An operation is still running. Wait for it to finish, then close the window.",
                        L"wifiForce", MB_ICONINFORMATION);
            return 0;
        }
        break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    if (wcsstr(GetCommandLineW(), L"--watchdog")) return Watchdog();
    // One window only: two copies could run Activate and Restore at the same
    // time and fight over the same files. A second launch brings the first
    // window forward instead. The handle is kept for the life of the process;
    // closing it would give the name up.
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\wifiForceGuiMutex");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"wifiForceWnd", nullptr);
        if (other) { ShowWindow(other, SW_RESTORE); SetForegroundWindow(other); }
        return 0;
    }
    // The icon comes from res.rc (id 1, the Wi-Fi-in-a-shield artwork); the
    // small one is loaded at the shell's own small-icon size so the title bar
    // and Alt+Tab do not have to scale a 256 px frame down.
    HICON bigIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    HICON smallIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);

    WNDCLASSEXW wc{}; wc.cbSize = sizeof wc; wc.lpfnWndProc = WndProc; wc.hInstance = inst; wc.lpszClassName = L"wifiForceWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = bigIcon;
    wc.hIconSm = smallIcon;
    RegisterClassExW(&wc);
    g_wnd = CreateWindowW(L"wifiForceWnd", L"wifiForce " WIFIFFORCE_VERSION_DOTTED,
                          WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                          CW_USEDEFAULT, CW_USEDEFAULT, 416, 208, nullptr, nullptr, inst, nullptr);
    ShowWindow(g_wnd, show); UpdateWindow(g_wnd);
    // The window asks once whether something newer exists; the watchdog copy of
    // the program never does.
    HANDLE chk = CreateThread(nullptr, 0, UpdateCheck, nullptr, 0, nullptr);
    if (chk) CloseHandle(chk);
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
