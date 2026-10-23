import cv2
from ultralytics import YOLO

# 1. Load the YOLOv8 model
model = YOLO('best.pt')

# 2. Open the video file
cap = cv2.VideoCapture('test_video.mp4')

# Get video dimensions
frame_width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
frame_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

# Define the "Danger Zone" (Center 40% of the screen)
center_left_boundary = int(frame_width * 0.3)
center_right_boundary = int(frame_width * 0.7)

while cap.isOpened():
    ret, frame = cap.read()
    if not ret:
        break # Video finished

    # 3. Run inference on the current frame
    results = model(frame, conf=0.4, verbose=False)
    
    # Default command: Drive straight
    speed = 200
    steer = 0
    action_text = "ALL CLEAR: Forward"
    color = (0, 255, 0) # Green

    # 4. Process detections
    for result in results:
        boxes = result.boxes
        for box in boxes:
            # Get bounding box coordinates
            x1, y1, x2, y2 = map(int, box.xyxy[0])
            center_x = int((x1 + x2) / 2)
            
            # Check if pothole is in the vehicle's direct path
            if center_left_boundary < center_x < center_right_boundary:
                speed = 20 # Slow down
                # Decide which way to swerve based on pothole position
                if center_x < (frame_width / 2):
                    steer = 255 # Swerve Right
                    action_text = "POTHOLE AHEAD: Swerve Right!"
                else:
                    steer = -255 # Swerve Left
                    action_text = "POTHOLE AHEAD: Swerve Left!"
                color = (0, 0, 255) # Red
            
            # Draw the bounding box for the pothole
            cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 165, 255), 2)

    # 5. Visual Debugging (Draw zones and commands on the video)
    # Draw danger zone lines
    cv2.line(frame, (center_left_boundary, 0), (center_left_boundary, frame_height), (255, 0, 0), 2)
    cv2.line(frame, (center_right_boundary, 0), (center_right_boundary, frame_height), (255, 0, 0), 2)
    
    # Display the simulated serial command
    cv2.putText(frame, f"CMD -> SPEED:{speed}, STEER:{steer}", (20, 50), 
                cv2.FONT_HERSHEY_SIMPLEX, 1, color, 2)
    cv2.putText(frame, action_text, (20, 100), 
                cv2.FONT_HERSHEY_SIMPLEX, 1, color, 2)

    # Show the frame
    cv2.imshow('RC Car Pothole Simulation', frame)

    # Mock sending the command via Serial
    print(f"Serial Output: SPEED:{speed}, STEER:{steer}")

    # Press 'q' to quit early
    if cv2.waitKey(30) & 0xFF == ord('q'):
        break

cap.release()
cv2.destroyAllWindows()