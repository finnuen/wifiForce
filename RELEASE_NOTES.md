# wifiForce v1.0

First release. The app forces Windows to keep Wi-Fi connected while Ethernet is
plugged in, and undoes everything it changes.

- Restore puts back the `WlanSvc` start type, switches a Wi-Fi adapter off again
  if Activate switched it on, and keeps `backup.json` when a policy value did not
  come back - run Restore again and it picks up where it stopped instead of
  losing the original values.
- The backup is read back before the first change; if it cannot be read, nothing
  is touched.
- A failed operation now shows the script's exit code, so "it did not work" says
  which part did not work.
- Activate no longer leaves one of the two scheduled tasks behind when the other
  cannot be created.
- A stuck PowerShell is terminated after a timeout instead of leaving the window
  on "Working..." for ever, and the watchdog's adapter re-enable is capped at 30
  seconds so it cannot stall the 3 second reconnect loop.
- The watchdog exits when `backup.json` is gone, rather than enforcing a policy
  that can no longer be undone.
- Closing the window while Activate or Restore is running asks you to wait
  instead of killing the operation halfway; starting a second copy brings the
  existing window forward instead of racing it.
- An icon (a Wi-Fi symbol in a shield, on JustViewMD's blue tile), version
  information in the file properties and the title bar, and an Apache-2.0
  licence. The name is `wifiForce` throughout.
- A build made with MinGW-w64 on Windows no longer carries two manifests, which
  could have let the app start without administrator rights.

Known limitations are unchanged: while active a manual Wi-Fi disconnect does not
stick, a reboot may be needed for the policy to apply, and the exe is unsigned.
