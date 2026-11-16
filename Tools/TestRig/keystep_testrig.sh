#!/usr/bin/env bash
# KeyStep Mk1 USB test rig (Linux, ALSA/PipeWire).
#
# Usage:
#   keystep_testrig.sh                       play test only
#   keystep_testrig.sh --flash FILE.led      flash, then play test
#   keystep_testrig.sh --flash FILE.bin      pack app.bin (linked at 0x08004000) into a .led
#                                            using the official .led as template, flash, play test
#   options: --template FILE.led  (default vendor/keystep_1_0_28.led)
#            --flash-only         stop after flashing
#            --yes                skip the flash confirmation prompt
#
# Steps:
#  0. (--flash) validates the image, asks you to start the KeyStep in bootloader mode
#     (hold Record + Stop + Play while plugging in USB), uploads it over SysEx with
#     Tools/Uploader, then asks you to replug normally.
#  1. Waits for the KeyStep (normal mode) on USB and logs its USB descriptors.
#  2. Sends a MIDI Identity Request and logs the SysEx reply (firmware version).
#  3. Starts FluidSynth (TiMidity++ fallback) as an ALSA sequencer synth and routes the KeyStep into it,
#     so whatever is played is audible.
#  4. Dumps every incoming MIDI event (timestamped) to the console and a log file.
#
# The Arturia bootloader (0x08000000-0x08003FFF) is never overwritten, so the official
# firmware can always be restored with: keystep_testrig.sh --flash vendor/keystep_1_0_28.led
#
# Ctrl-C stops everything. Logs go to vendor/testrig/ (gitignored).
# Needs: lsusb, amidi, aconnect, aseqdump, fluidsynth (or timidity), python3, a GM soundfont.

set -u
cd "$(dirname "$0")/../.."

FLASH=
TEMPLATE=vendor/keystep_1_0_28.led
FLASH_ONLY=0
YES=0
while [ $# -gt 0 ]; do
    case "$1" in
        --flash)      FLASH=${2:?--flash needs a file}; shift 2 ;;
        --template)   TEMPLATE=${2:?--template needs a file}; shift 2 ;;
        --flash-only) FLASH_ONLY=1; shift ;;
        --yes)        YES=1; shift ;;
        -h|--help)    sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)            echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

LOGDIR=vendor/testrig
mkdir -p "$LOGDIR"
STAMP=$(date +%Y%m%d-%H%M%S)
LOG="$LOGDIR/session-$STAMP.log"
SF2=${SF2:-/usr/share/sounds/sf2/FluidR3_GM.sf2}
VENDOR=1c75            # Arturia
UPDATER_PID=0208       # KeyStep Updater (bootloader mode)
UPLOADER=Tools/Uploader/keystep_rawmidi_handshake_uploader.py
PACKER=Tools/Ledpacker/make_keystep_custom_led.py

log() { echo "$*" | tee -a "$LOG"; }

usb_updater() { lsusb -d "$VENDOR:$UPDATER_PID" 2>/dev/null; }
usb_normal()  { lsusb -d "$VENDOR:" 2>/dev/null | grep -v -i "$VENDOR:$UPDATER_PID"; }
usb_any()     { lsusb -d "$VENDOR:" 2>/dev/null; }

wait_unplugged() {
    if usb_any >/dev/null; then
        log "== unplug the KeyStep ..."
        while usb_any >/dev/null; do sleep 0.3; done
    fi
}

# --- 0. flash -----------------------------------------------------------------
if [ -n "$FLASH" ]; then
    [ -f "$FLASH" ] || { log "!! no such file: $FLASH"; exit 1; }
    LED=$FLASH
    case "$FLASH" in
        *.bin)
            [ -f "$TEMPLATE" ] || { log "!! template .led not found: $TEMPLATE"; exit 1; }
            LED="$LOGDIR/$(basename "${FLASH%.bin}")-$STAMP.led"
            log "== packing $FLASH -> $LED (template $TEMPLATE)"
            python3 -I "$PACKER" "$TEMPLATE" "$FLASH" "$LED" 2>&1 | tee -a "$LOG"
            [ "${PIPESTATUS[0]}" -eq 0 ] || { log "!! packing failed"; exit 1; }
            ;;
        *.led) ;;
        *) log "!! --flash expects a .led or .bin file"; exit 1 ;;
    esac

    log "== checking image $LED"
    python3 -I "$UPLOADER" "$LED" --dry-run 2>&1 | tee -a "$LOG"
    [ "${PIPESTATUS[0]}" -eq 0 ] || { log "!! image rejected"; exit 1; }
    log "sha256 $(sha256sum "$LED" | cut -d' ' -f1)"

    if [ "$YES" -ne 1 ]; then
        echo
        echo "About to flash $LED to the KeyStep application area (0x08004000+)."
        echo "The bootloader is not touched; the official .led can always be re-flashed."
        read -r -p "Type 'flash' to continue: " ans
        [ "$ans" = flash ] || { log "cancelled"; exit 1; }
    fi

    if ! usb_updater >/dev/null; then
        wait_unplugged
        log "== hold RECORD + STOP + PLAY and plug the KeyStep in (bootloader mode) ..."
        until usb_updater >/dev/null; do
            if usb_normal >/dev/null; then
                log "   started in normal mode, not bootloader. Unplug and try again."
                while usb_any >/dev/null; do sleep 0.3; done
            fi
            sleep 0.3
        done
    fi
    log "$(usb_updater)"

    HW=
    for _ in $(seq 40); do
        HW=$(amidi -l 2>/dev/null | awk 'tolower($0) ~ /keystep updater/ {print $2; exit}')
        [ -n "$HW" ] && break
        sleep 0.25
    done
    [ -n "$HW" ] || { log "!! updater found on USB but no rawmidi port"; amidi -l | tee -a "$LOG"; exit 1; }

    log "== uploading via $HW"
    python3 -I -u "$UPLOADER" "$LED" --port "$HW" --yes 2>&1 | tee -a "$LOG"
    rc=${PIPESTATUS[0]}
    if [ "$rc" -ne 0 ]; then
        log "!! upload failed (exit $rc). The KeyStep stays in bootloader mode; rerun to retry,"
        log "   or flash the official image: $0 --flash vendor/keystep_1_0_28.led"
        exit 1
    fi
    log "== flash OK"
    [ "$FLASH_ONLY" -eq 1 ] && exit 0

    wait_unplugged
    log "== plug the KeyStep in normally (no buttons held) ..."
fi

# --- 1. wait for device -------------------------------------------------------
if usb_updater >/dev/null && ! usb_normal >/dev/null; then
    log "!! KeyStep is in bootloader mode; replug it without holding buttons."
    while usb_updater >/dev/null; do sleep 0.3; done
fi
log "== waiting for KeyStep on USB (plug it in) ..."
until usb_normal >/dev/null; do sleep 0.3; done
log "$(usb_normal)"
VIDPID=$(usb_normal | head -1 | awk '{print $6}')

# Give ALSA a moment to create the ports.
CLIENT=
for _ in $(seq 40); do
    CLIENT=$(aconnect -i | grep -i 'client [0-9]*: .*keystep' | grep -v -i updater | head -1)
    [ -n "$CLIENT" ] && break
    sleep 0.25
done
if [ -z "$CLIENT" ]; then
    log "!! KeyStep found on USB but no ALSA MIDI port appeared"
    aconnect -i | tee -a "$LOG"
    exit 1
fi
SEQPORT=$(echo "$CLIENT" | sed -E 's/client ([0-9]+):.*/\1/'):0
log "ALSA seq port: $SEQPORT  ($CLIENT)"

log "== USB descriptors"
lsusb -v -d "$VIDPID" 2>/dev/null \
    | grep -E 'idVendor|idProduct|bcdDevice|iManufacturer|iProduct|iSerial|bInterfaceClass|bInterfaceSubClass|bEndpointAddress|wMaxPacketSize|MaxPower|bNumConfigurations|baAssocJackID|bJackType|bJackID' \
    | sed 's/^ */  /' | tee -a "$LOG"

# --- 2. identity request (raw MIDI, before the sequencer grabs the port) ------
RAW=$(amidi -l | awk 'tolower($0) ~ /keystep/ && tolower($0) !~ /updater/ {print $2; exit}')
if [ -n "$RAW" ]; then
    log "== Identity Request on $RAW"
    REPLY=$(amidi -p "$RAW" -S 'F0 7E 7F 06 01 F7' -d -t 1 2>&1)
    log "${REPLY:-<no reply>}"
else
    log "!! no rawmidi device for KeyStep (amidi -l)"
fi

# --- 3. synth -----------------------------------------------------------------
SYNTHLOG="$LOGDIR/synth-$STAMP.log"
CFG=$(mktemp)
if command -v fluidsynth >/dev/null; then
    # FluidSynth straight to PipeWire, small period: ~10-20 ms latency.
    # FLUID_DRIVER (pipewire|pulseaudio|alsa) and FLUID_PERIOD to tune; raise the
    # period if it crackles.
    SYNTH=FluidSynth
    log "== starting FluidSynth ($SF2)"
    fluidsynth -is -a "${FLUID_DRIVER:-pipewire}" -m alsa_seq -g 1.0 \
        -o synth.reverb.active=0 -o synth.chorus.active=0 \
        -o audio.period-size="${FLUID_PERIOD:-128}" -o audio.periods=2 \
        -o midi.autoconnect=0 \
        "$SF2" >"$SYNTHLOG" 2>&1 &
else
    # Fallback: TiMidity++ via libao (-OO). -Os segfaults inside libasound on this
    # system with every device, and there is no JACK server for -Oj. Expect lag.
    #   -q sec/pct : TiMidity's own audio queue (default 5 s) -> keep small (TIMIDITY_Q).
    SYNTH=TiMidity
    log "== fluidsynth not installed; falling back to TiMidity++ (laggy) ($SF2)"
    echo "soundfont $SF2" > "$CFG"
    timidity -c "$CFG" -iA -OO -q"${TIMIDITY_Q:-0.1/0}" \
        -EFresamp=1 -EFreverb=0 -EFchorus=0 --volume=120 \
        >"$SYNTHLOG" 2>&1 &
fi
SYNTH_PID=$!

cleanup() {
    kill "$SYNTH_PID" 2>/dev/null
    rm -f "$CFG"
    log "== stopped. log: $LOG"
}
trap cleanup EXIT INT TERM

SYNTHPORT=
for _ in $(seq 60); do
    SYNTHPORT=$(aconnect -o | grep -E "client [0-9]+: '($SYNTH|FLUID Synth)" | head -1 \
                | sed -E 's/client ([0-9]+):.*/\1/')
    [ -n "$SYNTHPORT" ] && SYNTHPORT=$SYNTHPORT:0 && break
    kill -0 "$SYNTH_PID" 2>/dev/null || break
    sleep 0.25
done
if [ -z "$SYNTHPORT" ]; then
    log "!! $SYNTH did not come up, see $SYNTHLOG"
    exit 1
fi
aconnect "$SEQPORT" "$SYNTHPORT" && log "routed $SEQPORT -> $SYNTH $SYNTHPORT"

# --- 4. event dump ------------------------------------------------------------
log "== play! every MIDI event below is logged. Ctrl-C to stop."
aseqdump -p "$SEQPORT" | while IFS= read -r line; do
    printf '%s %s\n' "$(date +%T.%3N)" "$line"
done | tee -a "$LOG"
