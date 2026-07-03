# HTTP API examples

These files provide examples for interacting with the dosbox-automation HTTP API.
Make sure to enable the API by setting `webserver_enabled = on` in the
`[webserver]` config section and restart dosbox-automation. Then open
http://localhost:8386/ while dosbox-automation is running to learn more about
the API. Also check out the provided `api.js` wrapper.

These files here need to be placed in the `webserver` dir inside your
dosbox-automation config directory to use them.


## Memory monitor

http://localhost:8086/memory_monitor.html

Monitor and manipulate memory locations live.

As an example you can run the
[Commander Keen Episode 4 Demo](https://www.dosgames.com/game/commander-keen-4-secret-of-the-oracle/)
with the default dosbox-automation configuration and import this config string:

```
W3siYWRkciI6IjB4YzhlZmUiLCJ0eXBlIjoiY3N0cmluZyIsImxhYmVsIjoiRmlsZSJ9LHsiYWRkciI6IjB4MzgyZjgiLCJ0eXBlIjoidWludDgiLCJsYWJlbCI6IkFtbW8ifSx7ImFkZHIiOiIweDAzODMwYSIsInR5cGUiOiJ1aW50OCIsImxhYmVsIjoiTGl2ZXMifV0==
```

You will now see the current values for lives and ammo. Yes, you can edit them.


## Memory scanner

http://localhost:8086/memory_scanner.html

The Memory Scanner is a lightweight version of tools like Cheat Engine. It
identifies where specific values are stored in memory by iteratively tracking
changes.

1. Begin by searching for a known value (e.g., your current ammo count).
2. Filter the results by performing actions in-game that change that value.
3. Repeat until you have isolated the specific memory address.

Try finding the ammo address for the Commander Keen demo from above yourself.
It usually only takes a few iterations.


## Memory viewer

http://localhost:8086/memory.html

A memory viewer with a disassembler.
