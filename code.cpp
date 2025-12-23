// ESP32 web-based motor + servo controller
// - Two motors on GPIO1 and GPIO2 (PWM throttle with steering mix)
// - SG90 elevator servo on GPIO3
// Flash with ESP32 Arduino core. Requires ESP32Servo library.

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// ==== User configuration ==== 
constexpr char WIFI_SSID[] = "your-ssid";      // TODO: set your WiFi SSID
constexpr char WIFI_PASS[] = "your-password";  // TODO: set your WiFi password

// ==== Pin configuration ==== 
constexpr int PIN_MOTOR_LEFT = 1;   // GPIO1
constexpr int PIN_MOTOR_RIGHT = 2;  // GPIO2
constexpr int PIN_ELEVATOR = 3;     // GPIO3

// ==== PWM configuration ==== 
constexpr uint32_t MOTOR_PWM_FREQ = 20000;  // 20 kHz for quiet DC motor drivers/ESCs
constexpr uint8_t MOTOR_PWM_BITS = 10;      // 10-bit resolution (0-1023)
constexpr int MOTOR_MIN = 0;
constexpr int MOTOR_MAX = (1 << MOTOR_PWM_BITS) - 1;  // 1023

// Steering/throttle mixing limits
constexpr int THROTTLE_MIN = 0;
constexpr int THROTTLE_MAX = 100;
constexpr int STEER_MIN = -100;  // -100 full left, +100 full right
constexpr int STEER_MAX = 100;

// Elevator servo
constexpr int ELEVATOR_MIN_DEG = 0;
constexpr int ELEVATOR_MAX_DEG = 180;

// ==== Globals ==== 
WebServer server(80);
Servo elevator;

int throttlePct = 0;  // 0..100%
int steerPct = 0;     // -100..100%
int elevatorDeg = 90; // center position

// ==== Helpers ==== 
int clampInt(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

int mixMotor(int throttle, int steer) {
	// Simple differential mix: left = throttle - steer, right = throttle + steer
	// Input: throttle 0..100, steer -100..100
	int mixed = throttle + steer;  // steer sign decides which side speeds up
	return clampInt(mixed, THROTTLE_MIN, THROTTLE_MAX);
}

void applyOutputs() {
	// Convert percentage (0-100) to PWM value (0-1023)
	auto toPwm = [](int pct) {
		pct = clampInt(pct, THROTTLE_MIN, THROTTLE_MAX);
		return map(pct, THROTTLE_MIN, THROTTLE_MAX, MOTOR_MIN, MOTOR_MAX);
	};

	int leftPct = mixMotor(throttlePct, -steerPct);  // steer left slows right motor
	int rightPct = mixMotor(throttlePct, steerPct);

	int leftPwm = toPwm(leftPct);
	int rightPwm = toPwm(rightPct);

	ledcWrite(0, leftPwm);
	ledcWrite(1, rightPwm);

	elevatorDeg = clampInt(elevatorDeg, ELEVATOR_MIN_DEG, ELEVATOR_MAX_DEG);
	elevator.write(elevatorDeg);
}

String htmlPage() {
	// Minimal single-page UI with fetch-based updates.
	return R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
	<meta charset="UTF-8" />
	<meta name="viewport" content="width=device-width, initial-scale=1" />
	<title>ESP32 Motor & Servo</title>
	<style>
		body { font-family: 'Segoe UI', sans-serif; margin: 0; padding: 16px; background: #0b1224; color: #e8edf7; }
		h1 { margin-top: 0; }
		.card { background: #131b30; border: 1px solid #1f2a44; border-radius: 12px; padding: 16px; margin-bottom: 16px; box-shadow: 0 10px 30px rgba(0,0,0,0.35); }
		label { display: block; margin: 8px 0 4px; font-weight: 600; }
		input[type=range] { width: 100%; }
		.row { display: flex; gap: 16px; flex-wrap: wrap; }
		.col { flex: 1 1 260px; }
		.value { font-variant-numeric: tabular-nums; }
		button { background: #3b82f6; color: white; border: none; padding: 10px 14px; border-radius: 8px; cursor: pointer; font-weight: 600; }
		button:active { transform: translateY(1px); }
		.status { font-size: 0.9rem; opacity: 0.85; margin-top: 8px; }
	</style>
</head>
<body>
	<h1>ESP32 Motor & Servo</h1>
	<div class="row">
		<div class="card col">
			<label for="throttle">Throttle (0-100%)</label>
			<input id="throttle" type="range" min="0" max="100" value="0" />
			<div class="value" id="throttleVal">0%</div>
			<label for="steer">Steer (-100..100)</label>
			<input id="steer" type="range" min="-100" max="100" value="0" />
			<div class="value" id="steerVal">0</div>
		</div>
		<div class="card col">
			<label for="elev">Elevator (0-180°)</label>
			<input id="elev" type="range" min="0" max="180" value="90" />
			<div class="value" id="elevVal">90°</div>
			<button id="center">Center Elevator</button>
		</div>
	</div>
	<div class="card">
		<div class="status" id="status">Ready</div>
	</div>

	<script>
		const statusEl = document.getElementById('status');
		const throttle = document.getElementById('throttle');
		const steer = document.getElementById('steer');
		const elev = document.getElementById('elev');
		const throttleVal = document.getElementById('throttleVal');
		const steerVal = document.getElementById('steerVal');
		const elevVal = document.getElementById('elevVal');

		function updateLabels() {
			throttleVal.textContent = `${throttle.value}%`;
			steerVal.textContent = steer.value;
			elevVal.textContent = `${elev.value}°`;
		}

		async function sendUpdate() {
			updateLabels();
			const params = new URLSearchParams({
				throttle: throttle.value,
				steer: steer.value,
				elev: elev.value,
			});
			try {
				const res = await fetch(`/control?${params.toString()}`);
				const text = await res.text();
				statusEl.textContent = text;
			} catch (err) {
				statusEl.textContent = 'Error talking to ESP32';
			}
		}

		throttle.addEventListener('input', sendUpdate);
		steer.addEventListener('input', sendUpdate);
		elev.addEventListener('input', sendUpdate);
		document.getElementById('center').addEventListener('click', () => {
			elev.value = 90;
			sendUpdate();
		});

		// Initial push
		sendUpdate();
	</script>
</body>
</html>
	)HTML";
}

// ==== HTTP handlers ==== 
void handleRoot() { server.send(200, "text/html", htmlPage()); }

void handleControl() {
	if (server.hasArg("throttle")) throttlePct = clampInt(server.arg("throttle").toInt(), THROTTLE_MIN, THROTTLE_MAX);
	if (server.hasArg("steer")) steerPct = clampInt(server.arg("steer").toInt(), STEER_MIN, STEER_MAX);
	if (server.hasArg("elev")) elevatorDeg = clampInt(server.arg("elev").toInt(), ELEVATOR_MIN_DEG, ELEVATOR_MAX_DEG);

	applyOutputs();

	String resp = "Throttle: " + String(throttlePct) + "%  |  Steer: " + String(steerPct) + "  |  Elevator: " + String(elevatorDeg) + "°";
	server.send(200, "text/plain", resp);
}

void handleNotFound() { server.send(404, "text/plain", "Not found"); }

// ==== Setup ==== 
void setup() {
	Serial.begin(115200);
	delay(200);
	Serial.println("Booting...");

	// Servo PWM timer reservation must happen before ledcAttachPin for the same timer index.
	ESP32PWM::allocateTimer(0);
	ESP32PWM::allocateTimer(1);
	ESP32PWM::allocateTimer(2);
	ESP32PWM::allocateTimer(3);

	elevator.setPeriodHertz(50);  // SG90 expects ~50Hz
	elevator.attach(PIN_ELEVATOR, 500, 2400);  // typical pulse bounds

	// Motors on LEDC channels 0 and 1
	ledcSetup(0, MOTOR_PWM_FREQ, MOTOR_PWM_BITS);
	ledcAttachPin(PIN_MOTOR_LEFT, 0);
	ledcSetup(1, MOTOR_PWM_FREQ, MOTOR_PWM_BITS);
	ledcAttachPin(PIN_MOTOR_RIGHT, 1);

	applyOutputs();

	WiFi.mode(WIFI_STA);
	WiFi.begin(WIFI_SSID, WIFI_PASS);
	Serial.print("Connecting to WiFi");
	while (WiFi.status() != WL_CONNECTED) {
		delay(400);
		Serial.print('.');
	}
	Serial.println();
	Serial.print("Connected, IP: ");
	Serial.println(WiFi.localIP());

	server.on("/", HTTP_GET, handleRoot);
	server.on("/control", HTTP_GET, handleControl);
	server.onNotFound(handleNotFound);
	server.begin();
	Serial.println("HTTP server started");
}

// ==== Loop ==== 
void loop() {
	server.handleClient();
}
