# wifiForce 1.3

Turning Wi-Fi off in Windows now turns the forcing off with it. The watchdog
leaves the radio alone and stops reconnecting, so Wi-Fi can be switched off for
a while without running **Restore to default** first. Turn it back on and the
forcing resumes by itself.

The red line under the buttons now says so: "Turn off auto-reconnect by
restoring to default above or turn off Wi-Fi."

To update, close the running wifiForce first (Task Manager > wifiForce > End
task) and overwrite the old `.exe` with this one - Windows cannot replace a file
that is in use. If it is active, click **Activate** once afterwards, so the
background copy is updated too.
