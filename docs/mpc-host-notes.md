# How the MPC treats a plugin's screen: Q-Links, the data wheel and redraws

Notes from tracing VHS Vibes on an MPC One+ (32-bit, MPC 3.10.1.12) in November 2026. A trace
build recorded every call the MPC made into the plugin and every call the plugin made back, with
timestamps. "Measured" means it is in those traces. "Seen" means it was watched on the device's
screen. "Not known" means exactly that.

The short version: **never ask for a redraw (`audioMasterUpdateDisplay`) on a timer.** Report
moving values one by one (`audioMasterAutomate`) and keep redraw requests for the moments that
need them.

## The two ways to get a value onto the screen

| | `audioMasterAutomate(index, value)` | `audioMasterUpdateDisplay` |
|---|---|---|
| What the MPC does (measured) | Returns in about 2 µs. About 30 ms later the screen thread asks for the names of the reported parameters. Nothing else. | Rebuilds the plugin's screen: asks for the current program, all program names (46 here), and the names, values and display texts of the parameters. About 12 ms of calls each time. |
| Writes the value back with `setParameter` | No, not on this MPC. Other notes report that it does on other models, so a plugin should tolerate it. | No |
| Updates film strips and meters (seen) | Yes | Yes |
| Updates a read-out whose text changed while its value did not (seen) | No | Yes |
| Effect on the program field | None | Puts it back on the plugin's current program (see below) |

## The data wheel on the program field

Measured:

- The MPC does not send a program change for every wheel step. It collects steps and applies them
  about every 0.4 to 1.0 s (0.7 s typical) as one `effSetProgram`, so a fast turn arrives as jumps
  such as 23, 35, 38, 43, 45.
- Right before each `effSetProgram` it calls `effGetChunk`.
- Until that call, the plugin still reports the old program.

Seen, with the traces to match:

- A redraw request that arrives while steps are being collected makes the MPC read the current
  program from the plugin again. The field goes back to the old program and the collected steps
  are lost. With 12 to 16 redraw requests between two program changes, a fast turn arrived as
  11, 13, 14, 15, 16, 17: the list crawls, skips and bounces back.
- One redraw request right after a program change (within about 50 ms) does no harm: nothing has
  been collected yet.
- A redraw request delayed by 500 ms after a program change lands in the middle of the next
  collection and causes the same skipping, plus a visible lag. Holding redraws back for 500 ms
  after a change and then resuming them does not help either, because the MPC's own interval is
  longer than that.
- With per-value reports instead of redraw requests, the same fast turn arrived as 33, 40, 45, 34,
  24, 14: clean, the same as with nothing sent at all.

The plugin never receives a program change "back" to the old program. The skipping happens in the
MPC's field, before anything reaches the plugin.

## Q-Links

- Seen: redraw requests at up to 20 a second during Q-Link sweeps on the plugin's own pages do not
  disturb the sweep. The plugin stores a host write at once, so a rebuild reads the value the
  Q-Link just set.
- Seen, never reproduced, cause not known: in builds that sent per-value reports during hard sweeps
  across several Q-Links, the MPC's Q-Link screen locked up a few times and needed a restart of
  the MPC. Sound kept playing. Whether the reports caused it is not proven.
- From Open MPC's notes, not checked here: a plugin that answers every host write with a redraw
  request makes Q-Links snap back on the track edit / automation screen of a Force, and repeated
  redraw requests close the PRESET list.

## Reporting latency

Measured: `audioMasterIOChanged` makes the MPC query the plugin heavily for about 200 ms (over a
hundred rounds of name, category, vendor and version, then the program names). VHS Vibes reports a
new latency 500 ms after a program change settles, so each program change costs one of these. It
does not disturb the data wheel.

## What VHS Vibes does with this

The helper thread (`notifierMain` in `src/VHSVibes.cpp`) looks 20 times a second:

- **Normally:** each meter whose frame changed is reported with `audioMasterAutomate`. No redraw
  requests, so the program field and the PRESET list are left alone.
- **While the host is moving a setting** (a write in the last 300 ms: touch, Q-Link, data wheel on
  a knob, automation): no reports. The meters are set silently and one redraw request per look
  shows them. This keeps reports out of Q-Link sweeps.
- **After a program or a state loads, or Sat Type changes:** one redraw request, at once.

Known gap: while automation keeps writing a setting, the plugin stays on redraw requests, and
scrolling programs with the data wheel during that can skip.

## Making a trace

The trace build is not part of the release. Its patch and the script that reads a trace are kept
outside the repository; the idea is simple enough to rebuild: wrap `dispatcher`, `setParameter`,
`getParameter` and the host callback, push fixed-size entries into a wait-free ring from any
thread, and let the helper thread append them to a file in a `trace` folder inside the plugin's
folder on the drive. The MPC loads the plugin from its place on the drive, the folder is writable,
and exporting the plugin folder brings the trace back.
