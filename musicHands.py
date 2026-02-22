import cv2
import mediapipe as mp
from mediapipe.tasks import python
from mediapipe.tasks.python import vision
from mediapipe.tasks.python.vision import RunningMode
import os
import time

cap = cv2.VideoCapture(0)

MODEL_PATH = os.path.join(os.path.dirname(__file__), "hand_landmarker.task")

base_options = python.BaseOptions(model_asset_path=MODEL_PATH)

options = vision.HandLandmarkerOptions(
    base_options=base_options,
    num_hands=2,
    running_mode=RunningMode.VIDEO
)

HANDS_FILE = "/Users/quinnpeacock/lightthing/hands.txt"

# Small center region of the screen (normalized 0-1)
BOUNDS_X_MIN = 0.2
BOUNDS_X_MAX = 0.8
BOUNDS_Y_MIN = 0.2
BOUNDS_Y_MAX = 0.8

# --- Left hand (MediaPipe "Right") swipe detection ---
SWIPE_VELOCITY_THRESHOLD = 1.0   # normalized screen-widths per second
SWIPE_MIN_DX             = 0.03  # minimum movement to count (avoids noise)
SWIPE_COOLDOWN           = 0.5   # seconds between swipe triggers

prev_left_x    = None
prev_left_time = None
last_swipe_time = 0

with vision.HandLandmarker.create_from_options(options) as detector:
    while True:
        success, img = cap.read()
        if not success:
            print("Failed to grab frame")
            break

        img_rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB).copy()
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=img_rgb)

        timestamp_ms = int(time.time() * 1000)
        result = detector.detect_for_video(mp_image, timestamp_ms)

        right_hand_y = None       # mapped 0-1 within bounds (None = not seen)
        right_hand_in_bounds = False
        swipe_detected = False
        left_hand_seen = False

        now = time.time()

        if result.hand_landmarks:
            for i, hand_landmarks in enumerate(result.hand_landmarks):
                # Draw landmarks
                for lm in hand_landmarks:
                    h, w, _ = img.shape
                    cx, cy = int(lm.x * w), int(lm.y * h)
                    cv2.circle(img, (cx, cy), 5, (0, 255, 0), -1)

                label = result.handedness[i][0].category_name  # "Left" or "Right"
                wrist_x = hand_landmarks[0].x
                wrist_y = hand_landmarks[0].y
                print(f"Hand {i}: MediaPipe label='{label}'  wrist=({wrist_x:.2f}, {wrist_y:.2f})")

                if label == "Right":
                    # User's right hand → controls both radiuses
                    if (BOUNDS_X_MIN <= wrist_x <= BOUNDS_X_MAX and
                            BOUNDS_Y_MIN <= wrist_y <= BOUNDS_Y_MAX):
                        # Remap Y within the bounds to 0-1
                        right_hand_y = (wrist_y - BOUNDS_Y_MIN) / (BOUNDS_Y_MAX - BOUNDS_Y_MIN)
                        right_hand_in_bounds = True

                elif label == "Left":
                    # User's left hand → horizontal swipe detection
                    left_hand_seen = True
                    if prev_left_x is not None and prev_left_time is not None:
                        dt = now - prev_left_time
                        if 0 < dt < 0.15:  # only trust small frame gaps
                            dx = abs(wrist_x - prev_left_x)
                            velocity = dx / dt
                            print(f"  swipe candidate: dx={dx:.3f}  vel={velocity:.2f}")
                            if (velocity > SWIPE_VELOCITY_THRESHOLD
                                    and dx > SWIPE_MIN_DX
                                    and (now - last_swipe_time) > SWIPE_COOLDOWN):
                                swipe_detected = True
                                last_swipe_time = now
                                print("  >>> SWIPE DETECTED <<<")
                    prev_left_x = wrist_x
                    prev_left_time = now

                # Draw label
                wrist_x_px = int(hand_landmarks[0].x * img.shape[1])
                wrist_y_px = int(hand_landmarks[0].y * img.shape[0])
                cv2.putText(img, label, (wrist_x_px, wrist_y_px - 10),
                            cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 255, 0), 2)

        # Reset left-hand tracking if hand disappeared
        if not left_hand_seen:
            prev_left_x = None
            prev_left_time = None

        # Draw the active bounds rectangle (symmetric, so flip-safe)
        h, w, _ = img.shape
        cv2.rectangle(img,
                      (int(BOUNDS_X_MIN * w), int(BOUNDS_Y_MIN * h)),
                      (int(BOUNDS_X_MAX * w), int(BOUNDS_Y_MAX * h)),
                      (0, 255, 255), 2)

        # Write hand data to file
        with open(HANDS_FILE, "w") as f:
            if right_hand_in_bounds and right_hand_y is not None:
                f.write(f"LEFT {right_hand_y:.4f}\n")
            elif right_hand_y is None:
                f.write("LEFT NONE\n")
            else:
                f.write("LEFT OUT\n")
            f.write(f"SWIPE {1 if swipe_detected else 0}\n")

        img = cv2.flip(img, 1)
        cv2.imshow("Hand Tracker", img)

        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

cap.release()
cv2.destroyAllWindows()
