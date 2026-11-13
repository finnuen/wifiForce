# wifiForce

A tiny Windows app that keeps **Wi-Fi connected while a LAN cable is plugged in** (and has internet). Two buttons: **Activate** and **Restore to default**.

<img width="412" height="190" alt="PepperSnap_221" src="https://github.com/user-attachments/assets/ce6e7053-680f-4b14-a3f7-de90dcf5c911" />


## Why

Windows (and many laptop drivers) disconnect or idle Wi-Fi as soon as Ethernet is connected. wifiForce stops all the known causes and adds a watchdog that reconnects Wi-Fi if anything still drops it.

## Usage

1. Run `wifiForce.exe` (it requests administrator rights automatically).
2. Click **Activate**. The status line changes to `ACTIVE`.
3. Click **Restore to default** to undo everything and return to Windows defaults.

The version is shown in the title bar (`wifiForce` followed by the version). The app is unsigned, so SmartScreen may warn on first run: **More info > Run anyway**.

## What Activate does

| Layer | Change |
|---|---|
| Windows policy | `HKLM\SOFTWARE\Policies\Microsoft\Windows\WcmSvc\GroupPolicy`: `fMinimizeConnections=0`, `fBlockNonDomain=0`, `fDisablePowerManagement=1` |
| Wi-Fi adapter | Sets driver options like *Disable Upon Wired Connect* to Disabled; turns off "allow the computer to turn off this device to save power" |
| Wi-Fi profiles | Every saved profile set to connect automatically |
| Services | `WlanSvc` set to Automatic and started (the previous start type is remembered) |
| Watchdog | SYSTEM scheduled task `wifiForceWatchdog` (at startup) checks every 3 s: reconnects Wi-Fi, switches the software radio back on, re-enables a disabled adapter, rewrites the policy values |
| Guard | Task `wifiForceGuard` runs every minute and restarts the watchdog if it was killed (a named mutex prevents duplicates) |

Original values are saved to `C:\ProgramData\wifiForce\backup.json` before any change: the policy values, the driver options, the power setting, every profile's connection mode, the Wi-Fi adapters' on/off state and the `WlanSvc` start type. The file is read back before the first change, and if that fails nothing is touched.

## What Restore does

Deletes both tasks, stops the watchdog, then puts everything from the backup back: policy values, driver options, power setting, profile connection modes, the `WlanSvc` start type, and any Wi-Fi adapter that Activate switched on is switched off again. Finally it removes `C:\ProgramData\wifiForce`.

If a value cannot be put back, the backup is kept and the app reports an error instead of claiming success - run **Restore to default** again once the cause is gone (the backup is never overwritten by a second Activate).

## Notes

- While active, manually disconnecting Wi-Fi will not stick. Use **Restore to default** first.
- If the policy does not apply immediately, reboot once.
- Needs Windows 10/11, PowerShell 5+ and Task Scheduler.
- Click **Restore to default** before deleting the app. Deleting only `wifiForce.exe` leaves the background tasks running.
- Only one window can be open at a time; starting the exe again brings the existing one forward.
- `fDisablePowerManagement` under the `WcmSvc\GroupPolicy` key is not a documented policy value (the other two are). It is harmless - it is written on Activate and removed or reverted on Restore like everything else - but it may have no effect on some builds.

## Manual removal

If you lost the exe, run in an elevated Command Prompt:

```bat
schtasks /Delete /TN wifiForceGuard /F
schtasks /Delete /TN wifiForceWatchdog /F
taskkill /F /IM wifiForceSvc.exe
reg delete "HKLM\SOFTWARE\Policies\Microsoft\Windows\WcmSvc\GroupPolicy" /v fMinimizeConnections /f
reg delete "HKLM\SOFTWARE\Policies\Microsoft\Windows\WcmSvc\GroupPolicy" /v fBlockNonDomain /f
reg delete "HKLM\SOFTWARE\Policies\Microsoft\Windows\WcmSvc\GroupPolicy" /v fDisablePowerManagement /f
```

Adapter options can be reverted in Device Manager > your Wi-Fi adapter > Advanced; originals are listed in `C:\ProgramData\wifiForce\backup.json`.

## Build

```bat
build.bat
```

That bumps the version in `version.txt`, compiles, and leaves
`dist\wifiForce_v<version>.exe`. `build.bat --keep-version` rebuilds the same
version, `build.bat --version 2.0` sets it outright.

Requires MinGW-w64 (`winget install BrechtSanders.WinLibs.POSIX.UCRT`). The
`wifiForce_v<version>.exe` name carries the version so builds can be told apart;
the file attached to a release is renamed to `wifiForce.exe`.

Files: `src\` - `main.cpp` (all the code), `res.rc` (icon, manifest, version
block), `app.manifest` (admin elevation + visual styles) and `app.ico` (generated
by `tools\make-icon.ps1`) - plus `version.txt` and `tools\` (version and build
files). `build\` and `dist\` are output and are not committed.

## License

Apache License 2.0 - see [LICENSE](LICENSE).
