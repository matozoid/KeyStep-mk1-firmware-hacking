# MCC parity list

The editor app planned in [ideas.md](ideas.md) ("New MIDI Control Center") replaces Arturia's MIDI
Control Center (MCC) for the KeyStep Mk1. Everything a user can do in MCC for this device must
also be possible in the app. This file is the checklist.

Sources: the KeyStep manual (chapters 9 and 10), the general MCC manual, MCC 1.23's device
definition for the KeyStep, and the decoded protocol in [re/sysex.md](re/sysex.md) and
[re/settings.md](re/settings.md). MCC itself could not be run against the unit (it does not find
the device under Wine), so items marked (?) are taken from the documents and not seen in the
running program.

Status columns: **Proto** = the SysEx needed is decoded and answered by our firmware (tested with
a script on hardware, see `Firmware/HARDWARE_TEST.md`). **App** = built in the app (nothing is
built yet).

## 1. Application features

### 1.1 Connection and device

| # | Feature | What MCC does | Proto | App |
|---|---|---|---|---|
| A1 | Device detection | Lists connected Arturia devices, selects one. Recognises the KeyStep by USB ID and the identity reply. | yes | – |
| A2 | Firmware version display | Shows the device's firmware revision (from the identity reply). | yes | – |
| A3 | Firmware update | Offers a newer firmware when online, installs it through the updater. A local firmware file can also be installed (?). | yes ([bootloader_protocol.md](bootloader_protocol.md)) | – |
| A4 | MIDI console | Panel that lists messages to and from the device, as hex and as readable text; text can be selected and copied. | n/a | – |
| A5 | Touch-strip calibration | Guides the user through holding the low and high end of each strip and stores the result on the device (sysex.md 5.6). Where MCC shows this is not verified (?). | yes (not hardware tested) | – |
| A6 | App update check | MCC checks for a new version of itself. | n/a | – |
| A7 | Error reporting | Messages when the device is missing, busy in another program, or fails to open. | n/a | – |

### 1.2 Sync and the working project

| # | Feature | What MCC does | Proto | App |
|---|---|---|---|---|
| B1 | Sync | One button: reads all 8 sequences into a new template named with date and time, reads all device settings into the settings page, and turns live editing on. | yes | – |
| B2 | Live editing | While synced, every sequence edit is sent to the device at once. One direction only: changes made on the device are not seen until Recall From. | yes | – |
| B3 | Unsynced editing | Without sync, templates and sequences can still be edited, saved and sent on request; edits stay in the app until sent. | yes | – |
| B4 | Working project | Entry that stands for the device memory. Drop target for a template (sends all 8) or a single sequence (sends it to the same slot number). | yes | – |
| B5 | Store To | Sends the selected template (all 8 sequences) to the device, overwriting its memory. | yes | – |
| B6 | Recall From | Reads all 8 sequences from the device into a new template named with date and time. The chord memory is not included. | yes | – |
| B7 | Select template while synced | Selecting a template in the browser sends it to the device immediately. | yes | – |

### 1.3 Template library

A template holds the 8 sequences with their length, swing and gate. It does not hold the device
settings.

| # | Feature | What MCC does | App |
|---|---|---|---|
| C1 | Browser | Lists templates in two groups: factory (read-only, one default: 8 empty sequences of 16 steps, swing 50, gate 50) and user. A template expands to show its 8 sequences. | – |
| C2 | Unsaved marker | An edited template shows an asterisk until saved. | – |
| C3 | Save / Save As | Save in place, or under a new name. | – |
| C4 | New | New template from the defaults. | – |
| C5 | Delete | Removes a user template. | – |
| C6 | Rename | Templates can be renamed (auto names are date and time). | – |
| C7 | Import / Export | One file with all 8 sequences **and** all device settings, for exchange with other users. The app uses its own file format; MCC's `.keystep` files are out of scope. | – |
| C8 | Resizable browser pane | The list can be made taller. | – |

### 1.4 Sequence editor

Eight tabs, one per sequence. Each has a piano roll: keyboard on the left, one column per step,
steps numbered, 64 columns always present.

| # | Feature | What MCC does | Proto | App |
|---|---|---|---|---|
| D1 | Length | Knob or typed value, 1–64. A line marks the end; steps beyond it are dimmed but keep their notes. | yes | – |
| D2 | Swing | Knob or typed value, 50–75 % (finer than the 10 values on the device). | yes | – |
| D3 | Gate | List: 10, 25, 50, 75, 90 %. Applies to every note of the sequence. | yes | – |
| D4 | Add note | Double-click a cell. Up to 8 notes per step. | yes | – |
| D5 | Delete notes | Select one (click) or several (drag a box), then Delete. | yes | – |
| D6 | Move notes | Drag one note or a selection in pitch and in time. A note dropped on an occupied cell replaces it. The roll turns red when the move would leave the note range or the 64 steps. | yes | – |
| D7 | Copy notes | Alt/Option + drag a selection; copies keep velocity and gate. | yes | – |
| D8 | Velocity | Drag the top of a note up or down, 1–127. Colour shows the value (pale = low, red = high). With several notes selected all get the same value. | yes | – |
| D9 | Tie | Drag the right edge of a note: the only two lengths are the sequence gate or Tie. A tied note is drawn with an arrow into the next step. With several notes selected all get the same value. | yes | – |
| D10 | Scroll | Wheel = vertical, Shift + wheel = horizontal, plus scroll bars. | n/a | – |
| D11 | Zoom | Ctrl/Cmd + wheel over the grid = horizontal, over the keyboard = vertical. | n/a | – |
| D12 | Copy sequence to another slot | Drag one sequence tab onto another. Overwrites the target on the device when synced. | yes | – |
| D13 | Sequence from library to a slot | Drag a sequence out of any template onto a tab, to combine sequences from several templates. | yes | – |

### 1.5 Device settings page

| # | Feature | What MCC does | Proto | App |
|---|---|---|---|---|
| E1 | Edit settings | One scrolling page, in groups (section 2). Lists, number fields, note fields and channel fields with a "User" choice. A change is sent to the device at once, synced or not. | yes | – |
| E2 | Conditional fields | Some fields depend on another: the 0 V note shows for V/oct, the 1 V note for Hz/V. | n/a | – |
| E3 | Read settings from device | Done by Sync. | yes | – |
| E4 | Export settings | File with the device settings only. Own file format; MCC's `.keystep_ds` files are out of scope. | n/a | – |
| E5 | Import settings | Loads such a file and sends the settings. | yes | – |

### 1.6 Not applicable to the KeyStep

MCC has controller maps (per-control MIDI assignment with a picture of the panel), a Project tab,
drum maps and user scales for other devices. The KeyStep definition has none of these.

## 2. Device settings

All 48 settings in MCC's device definition. Code = parameter index `p` in settings.md §2, which has
the stock defaults and clamp rules. Channel fields: 1–16; "User" means follow the keyboard channel.

**FW** = our firmware acts on the value (every one of the 48 is stored and read back correctly;
that part is hardware tested). Behaviour on hardware has only been checked for a few settings at
their default values, so no setting counts as fully verified yet.

### MIDI controller

| Code | Setting | Values | FW |
|---|---|---|---|
| 0x16 | User Channel | 1–16 | yes |
| 0x07 | MIDI Input Channel (MIDI to CV while stopped) | 1–16, User | yes |
| 0x08 | MIDI Thru | Off, On | yes |
| 0x09 | Sustain Pedal Mode | Toggle, Gate | yes |
| 0x10 | Sustain Pedal Function | Hold, Sustain, Both | yes |
| 0x0E | Hold Switch Mode | Toggle, Gate | yes |
| 0x0F | Hold Switch Function | Hold, Sustain, Both | yes |
| 0x0A | Sustain Message MIDI Channel | 1–16, User | yes |
| 0x0B | Sustain Message Range Low | 0–127 | yes |
| 0x0C | Sustain Message Range High | 0–127 | yes |
| 0x0D | Sustain Message CC | 0–127 | yes |
| 0x13 | Mod Strip MIDI Channel | 1–16, User | yes |
| 0x14 | Mod Strip CC | 0–127 | yes |
| 0x15 | Pitch Bend MIDI Channel | 1–16, User | yes |
| 0x19 | Velocity Curve | Lin, Log, AntiLog | yes |
| 0x1A | Aftertouch Curve | Lin, Log, AntiLog, Soft | yes |
| 0x6B | LED Brightness | 10–100 % | yes |

### Sequence

| Code | Setting | Values | FW |
|---|---|---|---|
| 0x20 | Sync Clock In/Out | 1 step Gate, 1 step Clock, 1 pulse (Korg), 24 PPQ, 48 PPQ | yes |
| 0x23 | Sync Clock Start | On Clock, On Gate Start | **no (stored only)** |
| 0x24 | Next Seq | End, Instant Reset, Instant Continue | yes |
| 0x27 | Transposition Input Port | USB, MIDI, Both | yes |
| 0x21 | Transpose Input Channel | 1–16, None | yes |
| 0x26 | Transposition Latch | Off, On | yes |
| 0x2B | Transpose Center Pitch | any note | yes |
| 0x29 | Velocity | As Recorded, Fixed | yes |
| 0x2A | Fixed Velocity Value | 1–127 | yes |
| 0x28 | Tempo Knob Mode | Jump, Pickup | yes |
| 0x22 | Tap Tempo Average | 2, 3, 4 taps | yes |
| 0x2D | Arm to Start | Off, On | yes |
| 0x2E | Tie Mode | Tie to Previous, Tie to Next | yes |
| 0x2F | Arp Random | Total, Pattern, Brownian | yes |
| 0x30 | Sequence Length Update | Keep content beyond the length, Clear it | yes |

### CV/Gate

| Code | Setting | Values | FW |
|---|---|---|---|
| 0x46 | Pitch CV Output | Volt per octave, Hertz per volt | yes |
| 0x4A | 0 V MIDI Note (V/oct only) | any note | yes |
| 0x4C | 1 V MIDI Note (Hz/V only) | any note | yes |
| 0x4D | Note Priority | Low, High, Last | yes |
| 0x4E | Mod CV Source | Mod strip, Velocity, Aftertouch | yes |
| 0x4F | Mod CV Max Voltage | 5–12 V | yes |
| 0x50 | Pitch Bend Range | 1–24 semitones | yes |
| 0x47 | Gate CV Output | S-trig, V-trig 5 V, V-trig 12 V | yes |

### Transport

| Code | Setting | Values | FW |
|---|---|---|---|
| 0x67 | Play/Pause | Pause, Reset | yes |
| 0x60 | Transport Mode | MIDI CC, MMC, Both | yes |
| 0x61 | Stop Channel | 1–16, User | yes |
| 0x62 | Record Channel | 1–16, User | yes |
| 0x63 | Play Channel | 1–16, User | yes |
| 0x64 | Stop CC | 0–127 | yes |
| 0x65 | Record CC | 0–127 | yes |
| 0x66 | Play CC | 0–127 | yes |

The grouping follows the manual; the manual predates the last five Sequence settings and LED
Brightness, so their group in MCC 1.23 is a guess (?).

### Beyond MCC

Settings the device has but MCC does not show (0x17, 0x18, 0x25, 0x2C, 0x68–0x6A, settings.md §2a)
and the features in ideas.md (`.mid` import/export, simulated device, new firmware settings) are
extras, not part of parity.

## 3. Open points

1. MCC's file formats (`.keystep`, `.keystep_ds`) are deliberately not supported: they belong to
   MCC, not to the device. Parity means the same functions, with the app's own files.
2. MCC on Windows or macOS has never been run against our firmware; A5 and the (?) items need a
   look at the running program.
3. Sync Clock Start (0x23) has no effect in our firmware yet.
4. Per-setting behaviour tests on hardware are missing for most settings (CV/gate, sync, pedal,
   DIN are untested as a whole).
