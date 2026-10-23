#include <WiFi.h>
#include <WebServer.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// --- Wi-Fi Access Point Configuration ---
const char* ssid = "ESP32-RC-Car";
const char* password = "password123";

WebServer server(80);

// --- Pin Definitions ---
const int ENA = 32; 
const int IN1 = 25; 
const int IN2 = 26; 
const int IN3 = 27; 
const int IN4 = 14; 
const int ENB = 33; 

const int INA_PIN = 34; // Current Sensor

// Sensor & Servo Pins
const int TRIG_PIN = 5;
const int ECHO_PIN = 18; 
const int SERVO_PIN = 13;

// --- Global State Variables ---
int servoPos = 90;
unsigned long lastPingTime = 0;

int currentDistance = 999;
bool isMovingForward = false;
bool isAutoMode = false;

// --- Native Servo PWM Helper Function ---
void writeServoAngle(int angle) {
  // Constrain angle between 45 and 135 degrees
  angle = constrain(angle, 45, 135);
  // Map angle (0-180 scale mapped to our sweep bounds) to 50Hz PWM duty cycle (16-bit resolution)
  // 500µs (0°) = ~1638 counts, 2400µs (180°) = ~7864 counts for a 20ms period
  int duty = map(angle, 0, 180, 1638, 7864);
  ledcWrite(SERVO_PIN, duty);
}

// --- Embedded HTML / UI Dashboard ---
const char MAIN_page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 RC Car Dashboard</title>
    <style>
        :root {
            --bg-color: #0f172a;
            --card-bg: #1e293b;
            --accent: #38bdf8;
            --accent-hover: #0ea5e9;
            --text-main: #f8fafc;
            --text-muted: #94a3b8;
            --danger: #ef4444;
            --warning: #f59e0b;
        }
        * { 
            box-sizing: border-box; 
            margin: 0; 
            padding: 0; 
            font-family: system-ui, sans-serif; 
            -webkit-user-select: none;
            user-select: none;
        }
        body { background-color: var(--bg-color); color: var(--text-main); display: flex; flex-direction: column; align-items: center; justify-content: center; min-height: 100vh; padding: 20px; }
        .container { width: 100%; max-width: 400px; background: var(--card-bg); padding: 24px; border-radius: 16px; box-shadow: 0 10px 25px -5px rgba(0,0,0,0.3); display: flex; flex-direction: column; gap: 20px; }
        h1 { font-size: 1.25rem; text-align: center; font-weight: 600; }
        
        .telemetry-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
        .telemetry-card { background: rgba(15,23,42,0.6); padding: 16px; border-radius: 12px; text-align: center; border: 1px solid rgba(255,255,255,0.05); }
        .telemetry-label { color: var(--text-muted); font-size: 0.875rem; margin-bottom: 4px; }
        .telemetry-value { font-size: 1.25rem; font-weight: 700; color: var(--accent); }
        
        .mode-btn { width: 100%; padding: 14px; border-radius: 12px; background: transparent; border: 2px solid var(--accent); color: var(--accent); font-weight: 700; font-size: 1rem; cursor: pointer; transition: 0.2s; }
        .mode-btn.active { background: var(--accent); color: var(--bg-color); }
        
        /* New Servo Button Styling */
        .servo-control { width: 100%; text-align: center; margin-top: 5px; }
        .servo-buttons { display: flex; justify-content: space-between; align-items: center; background: rgba(15,23,42,0.6); padding: 10px 20px; border-radius: 12px; border: 1px solid rgba(255,255,255,0.05); margin-top: 8px; }
        .btn-servo { background-color: var(--card-bg); color: var(--accent); border: 2px solid var(--accent); width: 50px; height: 50px; border-radius: 10px; font-size: 1.2rem; cursor: pointer; transition: 0.2s; display: flex; align-items: center; justify-content: center; }
        .btn-servo:active { background-color: var(--accent); color: var(--bg-color); transform: scale(0.92); }
        .servo-angle { font-size: 1.2rem; font-weight: bold; color: var(--text-main); width: 60px; }

        .controls-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; justify-items: center; margin-top: 10px; }
        .btn { 
            background-color: var(--accent); 
            color: var(--bg-color); 
            border: none; 
            width: 75px; 
            height: 75px; 
            border-radius: 14px; 
            font-size: 1.5rem; 
            font-weight: bold; 
            cursor: pointer; 
            display: flex; 
            align-items: center; 
            justify-content: center; 
            -webkit-touch-callout: none; 
            touch-action: none; 
        }
        .btn:active { transform: scale(0.92); background-color: var(--accent-hover); }
        .btn-stop { background-color: var(--danger); color: white; }
        .btn-stop:active { background-color: #dc2626; }
        .empty { visibility: hidden; }
    </style>
</head>
<body oncontextmenu="return false;">
    <div class="container">
        <h1>RC Car Control Hub</h1>
        
        <div class="telemetry-grid">
            <div class="telemetry-card">
                <div class="telemetry-label">Current Draw</div>
                <div id="current" class="telemetry-value">0.00 A</div>
            </div>
            <div class="telemetry-card">
                <div class="telemetry-label">Distance Ahead</div>
                <div id="distance" class="telemetry-value">-- cm</div>
            </div>
        </div>

        <button id="modeBtn" class="mode-btn" onclick="toggleMode()">Mode: MANUAL</button>

        <div class="servo-control">
            <div class="telemetry-label">Aim Sensor (45&deg; to 135&deg;)</div>
            <div class="servo-buttons">
                <button class="btn-servo" onclick="stepServo(-15)">&#9664;</button>
                <div id="servoAngleDisplay" class="servo-angle">90&deg;</div>
                <button class="btn-servo" onclick="stepServo(15)">&#9654;</button>
            </div>
        </div>

        <div class="controls-grid">
            <div class="empty"></div>
            <button class="btn" ontouchstart="sendCommand('forward')" onmousedown="sendCommand('forward')" onmouseup="sendCommand('stop')" ontouchend="sendCommand('stop')">&#9650;</button>
            <div class="empty"></div>

            <button class="btn" ontouchstart="sendCommand('left')" onmousedown="sendCommand('left')" onmouseup="sendCommand('stop')" ontouchend="sendCommand('stop')">&#9664;</button>
            <button class="btn btn-stop" onclick="sendCommand('stop')">&#9632;</button>
            <button class="btn" ontouchstart="sendCommand('right')" onmousedown="sendCommand('right')" onmouseup="sendCommand('stop')" ontouchend="sendCommand('stop')">&#9654;</button>

            <div class="empty"></div>
            <button class="btn" ontouchstart="sendCommand('backward')" onmousedown="sendCommand('backward')" onmouseup="sendCommand('stop')" ontouchend="sendCommand('stop')">&#9660;</button>
            <div class="empty"></div>
        </div>
    </div>

    <script>
        let isAuto = false;
        let currentAngle = 90;

        function toggleMode() {
            isAuto = !isAuto;
            let btn = document.getElementById('modeBtn');
            btn.innerText = isAuto ? 'Mode: AUTO-BRAKE' : 'Mode: MANUAL';
            btn.className = isAuto ? 'mode-btn active' : 'mode-btn';
            
            fetch('/setmode?auto=' + (isAuto ? '1' : '0')).catch(err => console.log(err));
            sendCommand('stop'); 
        }

        // --- New Servo Button Logic ---
        function stepServo(step) {
            currentAngle += step;
            // Strict bounds checking
            if (currentAngle < 45) currentAngle = 45;
            if (currentAngle > 135) currentAngle = 135;
            
            document.getElementById('servoAngleDisplay').innerHTML = currentAngle + '&deg;';
            fetch('/servo?angle=' + currentAngle).catch(err => console.log(err));
        }

        document.querySelectorAll('.btn').forEach(btn => {
            btn.addEventListener('contextmenu', e => e.preventDefault());
        });

        function sendCommand(cmd) { fetch('/move?dir=' + cmd).catch(err => console.log(err)); }

        setInterval(() => {
            fetch('/current').then(r => r.text()).then(d => { document.getElementById('current').innerText = parseFloat(d).toFixed(3) + ' A'; });
            fetch('/distance').then(r => r.text()).then(d => { 
                let distObj = document.getElementById('distance');
                distObj.innerText = d + ' cm';
                if(parseInt(d) < 15) distObj.style.color = '#ef4444'; 
                else if(parseInt(d) < 40) distObj.style.color = '#f59e0b'; 
                else distObj.style.color = '#38bdf8'; 
            });
        }, 300);
    </script>
</body>
</html>
)rawliteral";

// --- Motor Control Functions ---

void stopMotors() {
  isMovingForward = false;
  ledcWrite(ENA, 0);
  ledcWrite(ENB, 0);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void rampUpMotors() {
  for(int speed = 80; speed <= 255; speed += 20) {
    ledcWrite(ENA, speed);
    ledcWrite(ENB, speed);
    delay(10);
  }
}

void moveForward() {
  isMovingForward = true; 
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  rampUpMotors();
}

void moveBackward() {
  isMovingForward = false;
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  rampUpMotors();
}

void turnLeft() {
  isMovingForward = false;
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  rampUpMotors();
}

void turnRight() {
  isMovingForward = false;
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  rampUpMotors();
}

// --- Web Handlers ---
void handleRoot() { server.send(200, "text/html", MAIN_page); }
void handleDistance() { server.send(200, "text/plain", String(currentDistance)); }
void handleCurrent() {
  int rawValue = analogRead(INA_PIN);
  float voltage = (rawValue / 4095.0) * 3.3;
  float currentAmps = voltage / 10.0; // I = V / 10 ohms
  server.send(200, "text/plain", String(currentAmps, 3)); 
}

void handleSetMode() {
  if (server.hasArg("auto")) {
    isAutoMode = (server.arg("auto") == "1");
  }
  server.send(200, "text/plain", "OK");
}

void handleServo() {
  if (server.hasArg("angle")) {
    servoPos = server.arg("angle").toInt();
    writeServoAngle(servoPos);
  }
  server.send(200, "text/plain", "OK");
}

void handleMove() {
  if (server.hasArg("dir")) {
    String dir = server.arg("dir");
    if (dir == "forward") moveForward();
    else if (dir == "backward") moveBackward();
    else if (dir == "left") turnLeft();
    else if (dir == "right") turnRight();
    else if (dir == "stop") stopMotors();
  }
  server.send(200, "text/plain", "OK");
}

// --- Main Setup ---
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(INA_PIN, INPUT);
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Initialize Motor PWM (5kHz, 8-bit)
  ledcAttach(ENA, 5000, 8); 
  ledcAttach(ENB, 5000, 8);

  // Initialize Servo PWM natively (50Hz frequency, 16-bit resolution)
  ledcAttach(SERVO_PIN, 50, 16);
  
  stopMotors();
  writeServoAngle(90); // Center the servo on boot

  WiFi.softAP(ssid, password);
  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/current", handleCurrent);
  server.on("/distance", handleDistance);
  server.on("/setmode", handleSetMode);
  server.on("/servo", handleServo); // Handle manual servo movement
  server.begin();
}

void loop() {
  server.handleClient(); 

  // Non-blocking Ultrasonic Ping
  if (millis() - lastPingTime > 50) {
    lastPingTime = millis();
    
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    long duration = pulseIn(ECHO_PIN, HIGH, 30000); 
    
    if (duration == 0) {
      currentDistance = 999; 
    } else {
      currentDistance = duration * 0.034 / 2;
    }

    // Auto-Brake Logic
    if (isAutoMode && isMovingForward) {
      if (currentDistance < 15) {
        ledcWrite(ENA, 0);
        ledcWrite(ENB, 0);
      } else if (currentDistance < 40) {
        ledcWrite(ENA, 120); 
        ledcWrite(ENB, 120);
      } else {
        ledcWrite(ENA, 255);
        ledcWrite(ENB, 255);
      }
    }
  }
}