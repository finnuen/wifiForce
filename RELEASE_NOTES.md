# wifiForce 1.2

The window now checks once, on launch, whether a newer release exists. When there
is one it says so, with **Download latest** - which opens the release page in your
browser - and **Close**.

If the machine is offline, GitHub cannot be reached, or the answer is "no newer
version", nothing appears and the program behaves exactly as before. Nothing is
reported anywhere, and the background watchdog never makes the check.
