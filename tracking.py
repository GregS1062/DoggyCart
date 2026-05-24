import cv2
from picamera2 import Picamera2
from ultralytics import YOLO
import numpy as np
import os, sys, time
import errno
from pathlib import Path
import configparser
import logging

# ────────────────────────────────────────────────
# Priority and logging
# ────────────────────────────────────────────────

os.nice(10)  # lower CPU priority; 0 is default, 19 is lowest

logging.basicConfig(level=logging.INFO)
log = logging.getLogger("tracker")

# ────────────────────────────────────────────────
# Configuration
# ────────────────────────────────────────────────

config = configparser.ConfigParser()
config.read('/etc/DoggyCart/config.ini')

yoloPath = config.get("Tracking", "yoloPath")
jpgs     = config.getboolean("Tracking", "jpgs")
jpgsPath = config.get("Tracking", "jpgsPath")

# ────────────────────────────────────────────────
# Named Pipes
# ────────────────────────────────────────────────
#
# Pipe names are from tracking.py's perspective:
#   PIPE_CMD   (/tmp/resp_pipe) — C++ writes commands here; we read them.
#   PIPE_DATA  (/tmp/req_pipe)  — we write coordinates here; C++ reads them.
#
# C++ writes "SEND" to start coordinate delivery and "WAIT" to pause it.
# C++ writes "ack" in response to each coordinate we send.

PIPE_CMD  = "/tmp/doggycart_cmd"    # commands from C++  (O_RDONLY | O_NONBLOCK)
PIPE_DATA = "/tmp/doggycart_data"   # offset floats to C++ (O_WRONLY)

BUFFER_SIZE = 4096

# ────────────────────────────────────────────────
# State constants
# ────────────────────────────────────────────────

STATE_WAIT = "WAIT"   # waiting for C++ to send "SEND"
STATE_SEND = "SEND"   # actively sending coordinates

# ────────────────────────────────────────────────
# FIFO helpers
# ────────────────────────────────────────────────

def make_fifo(path):
    p = Path(path)
    if not p.exists():
        try:
            os.mkfifo(path)
            log.info("Created fifo: %s", path)
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise


def connect_pipes():
    """Open both pipes.  Returns (fd_cmd, fd_data) or (None, None) on failure.

    Pipes are created by locate.h before tracking.py is launched.
    fd_cmd  is opened O_RDONLY | O_NONBLOCK so poll_command() never blocks.
    fd_data is opened O_WRONLY  (blocking) — locate.h holds the read end.
    """
    try:
        fd_cmd = os.open(PIPE_CMD, os.O_RDONLY | os.O_NONBLOCK)
    except OSError as e:
        log.error("Cannot open command pipe: %s", e.strerror)
        return None, None

    log.info("Command pipe opened — waiting for C++ to open data pipe...")
    try:
        fd_data = os.open(PIPE_DATA, os.O_WRONLY)
    except OSError as e:
        log.error("Cannot open data pipe: %s", e.strerror)
        os.close(fd_cmd)
        return None, None

    log.info("Pipes connected.")
    return fd_cmd, fd_data


def close_pipes(fd_cmd, fd_data):
    if fd_cmd  is not None:
        try: os.close(fd_cmd)
        except OSError: pass
    if fd_data is not None:
        try: os.close(fd_data)
        except OSError: pass


def poll_command(fd_cmd):
    """Non-blocking read of a command from C++.

    Returns:
        "SEND"  — C++ wants coordinates
        "WAIT"  — C++ wants us to pause
        "ack"   — acknowledgement (in response to a coordinate we sent)
        None    — no data available (EAGAIN)
        "FATAL" — pipe broken / EOF
    """
    try:
        data = os.read(fd_cmd, BUFFER_SIZE)
    except OSError as e:
        if e.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
            return None   # no data yet — normal
        log.warning("Command pipe read error: %s", e.strerror)
        return "FATAL"

    if not data:
        log.warning("Command pipe EOF — C++ closed the pipe")
        return "FATAL"

    return data.decode("utf-8").rstrip("\n")


def read_ack(fd_cmd, timeout=2.0):
    """Blocking-style read of C++'s ack/command after sending a coordinate.

    Polls until data arrives or timeout expires.
    Returns the command/ack string, or "FATAL" on error/timeout.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        cmd = poll_command(fd_cmd)
        if cmd is not None:
            return cmd
        time.sleep(0.05)
    log.warning("Timeout waiting for ack from C++")
    return "FATAL"


def send_coords(fd_data, coords_str):
    """Write a coordinate string to C++.  Returns True on success."""
    try:
        os.write(fd_data, coords_str.encode("utf-8"))
        return True
    except OSError as e:
        if e.errno == errno.EPIPE:
            log.warning("Data pipe broken (EPIPE) — C++ disconnected")
        else:
            log.warning("Data pipe write error: %s", e.strerror)
        return False


def send_hello(fd_data):
    """Announce our presence to C++ when we first connect."""
    try:
        os.write(fd_data, b"hello")
        return True
    except OSError:
        return False

# ────────────────────────────────────────────────
# Model + camera (initialised once at startup)
# ────────────────────────────────────────────────

log.info("Loading YOLO model from %s ...", yoloPath)
model = YOLO(yoloPath)
person_classes = [k for k, v in model.names.items() if v in ("person", "human")]

cv2.startWindowThread()
picam2 = Picamera2()
picam2.configure(picam2.create_preview_configuration(
    main={"format": "XRGB8888", "size": (640, 480)}))
picam2.start()
log.info("Camera started.")

# ────────────────────────────────────────────────
# Main loop
# ────────────────────────────────────────────────

TARGET_FPS     = 30
FRAME_INTERVAL = 1.0 / TARGET_FPS
jpg_counter    = 0


def main():
    global jpg_counter

    log.info("tracking.py service starting — will reconnect indefinitely")

    while True:   # outer reconnect loop — never exits

        # ── Connect ───────────────────────────────────────────────
        fd_cmd, fd_data = connect_pipes()
        if fd_cmd is None or fd_data is None:
            log.warning("Pipe connect failed — retrying in 5 s")
            close_pipes(fd_cmd, fd_data)
            time.sleep(5)
            continue

        # Announce presence; C++ will reply with "SEND" when it is ready.
        send_hello(fd_data)

        state = STATE_WAIT
        log.info("Connected. State = WAIT — waiting for SEND from C++")

        try:
            while True:   # inner session loop

                if state == STATE_WAIT:
                    # ── Waiting for SEND command ───────────────────
                    cmd = poll_command(fd_cmd)
                    if cmd == "SEND":
                        state = STATE_SEND
                        log.info("Received SEND — resuming coordinate delivery")
                    elif cmd == "FATAL":
                        log.warning("Command pipe lost in WAIT state — reconnecting")
                        break
                    # None → no data yet; keep polling
                    time.sleep(0.1)
                    continue

                # ── STATE_SEND: capture + detect + send ───────────
                # Check for any pending command before capturing.
                cmd = poll_command(fd_cmd)
                if cmd == "WAIT":
                    state = STATE_WAIT
                    log.info("Received WAIT — pausing coordinate delivery")
                    continue
                if cmd == "FATAL":
                    log.warning("Command pipe lost in SEND state — reconnecting")
                    break

                # Capture frame at TARGET_FPS
                frame_start = time.monotonic()
                im  = picam2.capture_array()

                elapsed    = time.monotonic() - frame_start
                sleep_time = FRAME_INTERVAL - elapsed
                if sleep_time > 0:
                    time.sleep(sleep_time)

                img     = cv2.cvtColor(im, cv2.COLOR_BGR2RGB)
                results = model.predict(img, verbose=False, classes=person_classes)

                sent_any = False
                frame_width = im.shape[1]
                for result in results:
                    for box in result.boxes:
                        if box.conf[0] <= 0.80:
                            continue

                        xyxy   = box.xyxy[0]
                        cx     = (float(xyxy[0]) + float(xyxy[2])) / 2.0
                        offset = (cx - frame_width / 2.0) / (frame_width / 2.0)
                        coords_str = f"{offset:.4f}\n"

                        if not send_coords(fd_data, coords_str):
                            # Data pipe broken — fall back to reconnect
                            state = STATE_WAIT
                            break

                        # Wait for C++ ack or command
                        response = read_ack(fd_cmd)
                        if response == "WAIT":
                            state = STATE_WAIT
                            log.info("Received WAIT (as ack) — pausing")
                            break
                        if response == "FATAL":
                            log.warning("No ack received — pipe broken")
                            state = STATE_WAIT
                            break
                        # "ack" → continue sending

                        sent_any = True
                        jpg_counter += 1
                        if jpgs:
                            log.info("Photo %d: offset=%.4f", jpg_counter, offset)
                            img_bgr  = cv2.cvtColor(img, cv2.COLOR_RGB2BGR)
                            img_path = jpgsPath + str(jpg_counter) + "person.jpg"
                            cv2.imwrite(img_path, img_bgr)

                    if state == STATE_WAIT:
                        break

        except Exception as e:
            log.exception("Unexpected error in session loop: %s", e)

        close_pipes(fd_cmd, fd_data)
        log.info("Session ended — reconnecting in 2 s")
        time.sleep(2)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        log.info("Interrupted by user.")
        sys.exit(130)