#!/usr/bin/env python3
# ============================================================
# camera.py  —  Object detection stub (YOLO to be added later)
#
# Communicates with locate.h via a named FIFO so the servo
# controller knows when and where an object is detected.
#
# IPC pipe:  /tmp/doggycart_locate   (created by locate.h)
# Protocol:  one ASCII float per line, newline-terminated
#
#   "-0.3200\n"   object is 32 % left  of frame centre
#    "0.0000\n"   object is centred     → locate.h stops the servo
#   "+0.1500\n"   object is 15 % right of frame centre
#
#   Range: -1.0 (object at left edge) … +1.0 (right edge)
#
# Run:  python3 camera.py
#       (start doggycart first so the pipe exists)
# ============================================================

import os
import time

PIPE_PATH = '/tmp/doggycart_locate'


# ── IPC ───────────────────────────────────────────────────────

def send_offset(offset: float) -> None:
    """
    Write the detected object's normalised horizontal offset to
    the locate pipe.  locate.h reads this non-blockingly and
    steers the servo toward the object.

    offset:  -1.0 = far left of frame
              0.0 = centred  (servo will stop)
             +1.0 = far right of frame
    """
    try:
        with open(PIPE_PATH, 'w') as pipe:
            pipe.write(f'{offset:.4f}\n')
    except OSError as exc:
        print(f'[camera] pipe write failed: {exc}')


# ── Detection helpers (stubs) ─────────────────────────────────

def normalise_offset(obj_centre_x: float, frame_width: int) -> float:
    """Convert pixel x position to [-1.0, +1.0] relative to frame centre."""
    return (obj_centre_x - frame_width / 2.0) / (frame_width / 2.0)


# ── Main ──────────────────────────────────────────────────────

if __name__ == '__main__':
    print('[camera] stub — YOLO detection not yet implemented')
    print(f'[camera] pipe: {PIPE_PATH}')

    if not os.path.exists(PIPE_PATH):
        print('[camera] WARNING: pipe not found — is doggycart running?')

    # ── TODO: replace this section with real detection ────────
    #
    # Suggested structure once camera + YOLO are added:
    #
    # from picamera2 import Picamera2          # or cv2.VideoCapture
    # from ultralytics import YOLO
    #
    # cam  = Picamera2()
    # cam.start()
    # model = YOLO('yolov8n.pt')
    #
    # while True:
    #     frame = cam.capture_array()
    #     results = model(frame, verbose=False)
    #
    #     if results[0].boxes:
    #         box    = results[0].boxes[0]          # highest-confidence detection
    #         cx     = float((box.xyxy[0][0] + box.xyxy[0][2]) / 2)
    #         offset = normalise_offset(cx, frame.shape[1])
    #         send_offset(offset)
    #
    #     time.sleep(1 / 30)   # ~30 fps
    # ─────────────────────────────────────────────────────────

    print('[camera] nothing to do yet — exiting.')
