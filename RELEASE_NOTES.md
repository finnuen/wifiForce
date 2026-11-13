# wifiForce 1.0

First release. wifiForce keeps Wi-Fi connected while an Ethernet cable is
plugged in, and puts everything back when you are done with it.

- **Activate** applies the Windows policy, switches off the adapter's "disable on
  wired connect" option and its power saving, and sets every saved network to
  connect automatically.
- A SYSTEM watchdog checks every 3 seconds and puts those settings back if
  Windows or a driver undoes them, reconnects Wi-Fi, and re-enables a disabled
  adapter.
- **Restore to default** undoes every change from a record written before the
  first one. If a value does not come back, the record is kept so Restore can be
  run again instead of losing the originals.
- One executable, no installer. It asks for administrator rights because the
  settings are machine-wide, and needs Windows 10/11, PowerShell 5+ and Task
  Scheduler.

Known limits: while active, a manual Wi-Fi disconnect will not stick - restore
first. A reboot may be needed for the policy to take effect. The executable is
unsigned, so SmartScreen may warn on first run.
