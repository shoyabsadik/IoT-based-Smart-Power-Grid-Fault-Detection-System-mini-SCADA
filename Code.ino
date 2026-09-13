#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <math.h>

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= Fault Sensor Input Pins (NodeMCU labels) =================
// Using ESP8266's INTERNAL pull-up (no external resistor needed).
// Connect switch/sensor between this pin and GND. Pin floating/open = HIGH (no fault).
// Pin pulled to GND (switch closed / sensor triggers) = LOW = fault.
#define GROUND_FAULT_PIN 14  // D5
#define LOAD_SHORT_PIN   12  // D6

// ================= Buzzer =================
#define BUZZER_PIN 13         // D7

// ================= I2C Pins =================
#define I2C_SDA 4             // D2
#define I2C_SCL 5             // D1

// ================= Manual Relay Output Pins (breaker control) =================
// CAUTION: D3(GPIO0) and D4(GPIO2) are boot-strapping pins.
// Drive them through a relay module / transistor that stays high-impedance
// until the ESP actively sets the pin - do NOT tie them directly to GND/3.3V externally.
#define LOAD1_RELAY_PIN  0    // D3 - Load Short Line 1 breaker
#define LOAD2_RELAY_PIN  2    // D4 - Load Short Line 2 breaker (shares onboard LED, active LOW)
#define GROUND_RELAY_PIN 16   // D0 - Ground fault line breaker

// ================= WiFi Access Point =================
const char* AP_SSID     = "FaultMonitor_SCADA";
const char* AP_PASSWORD = "12345678";

ESP8266WebServer server(80);

// ================= Fault / Buzzer State =================
bool groundFaultActive = false;
bool loadShortActive   = false;
bool buzzerState       = false;

unsigned int groundFaultCount = 0;
unsigned int loadShortCount   = 0;

int prevGroundFault = HIGH;
int prevLoadShort   = HIGH;

unsigned long lastLCDUpdate = 0;
const unsigned long LCD_INTERVAL = 300;

// ================= Manual Breaker Commanded State (from web buttons: A, B, C) =================
bool cmdLoad1  = false; // 'A' - Load Line 1
bool cmdLoad2  = false; // 'B' - Load Line 2
bool cmdGround = false; // 'C' - Ground Line

// Effective (actual) output state after fault-override safety logic
bool effLoad1 = false, effLoad2 = false, effGround = false;

// ================= Simulated Electrical Readings =================
// NOTE: No voltage/current sensor is wired yet - these are DEMO values
// (smooth wave + small noise) purely for the prototype dashboard look.
// Replace with real ZMPT101B / ACS712 readings later if needed.
float voltageStepUp   = 0; // simulated transmission-side voltage (V)
float voltageStepDown = 0; // simulated load-side voltage (V)
float lineCurrent     = 0; // simulated current (A)
float transPower      = 0; // computed power (kW)

void updateSimulatedReadings() {
  float t = millis() / 1000.0;
  voltageStepUp   = 11000 + 150 * sin(t * 0.4) + random(-40, 40);
  voltageStepDown = 220   + 4   * sin(t * 0.7) + random(-2, 2);
  lineCurrent     = (loadShortActive || groundFaultActive) ? 0.0
                     : (8.0 + 2.0 * sin(t * 0.9) + random(-30, 30) / 100.0);
  transPower      = (voltageStepDown * lineCurrent) / 1000.0; // kW
}

// ============================================================
//                      WEB PAGE (HTML)
// ============================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>IoT Power Grid Fault Detection - Mini SCADA</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  *{box-sizing:border-box;}
  body{font-family:'Segoe UI',Arial,sans-serif;background:linear-gradient(180deg,#0a0e1a,#111a2e);color:#e8ecf1;margin:0;padding:16px;}
  h1{text-align:center;font-size:18px;color:#4fd1ff;margin:4px 0 2px;letter-spacing:1px;}
  h2{text-align:center;font-size:12px;color:#7f8ba3;margin:0 0 16px;font-weight:normal;}
  .panel{background:#151d30;border:1px solid #23304d;border-radius:12px;padding:14px;margin-bottom:14px;box-shadow:0 4px 12px rgba(0,0,0,.3);}
  .title{font-size:11px;color:#7f8ba3;text-transform:uppercase;letter-spacing:1px;margin-bottom:10px;text-align:center;}

  .line-diagram{display:flex;align-items:center;justify-content:space-between;padding:16px 6px 10px;overflow-x:auto;gap:2px;}
  .node{display:flex;flex-direction:column;align-items:center;min-width:58px;font-size:9px;color:#9aa7bd;font-weight:600;letter-spacing:.4px;}
  .node .icon{width:38px;height:38px;border-radius:50%;background:radial-gradient(circle at 30% 30%,#2c3f66,#18233c);display:flex;align-items:center;justify-content:center;font-size:17px;margin-bottom:6px;border:2px solid #3d5a8a;box-shadow:0 0 10px rgba(79,209,255,.3);color:#4fd1ff;}
  .wire{flex:1;height:4px;min-width:16px;background:repeating-linear-gradient(90deg,#2fd27a 0 9px,transparent 9px 18px);background-size:18px 4px;margin:0 3px;border-radius:2px;animation:flow .7s linear infinite;box-shadow:0 0 6px rgba(47,210,122,.4);}
  .wire.fault{background:repeating-linear-gradient(90deg,#ff4d4d 0 9px,transparent 9px 18px);animation:none;box-shadow:0 0 6px rgba(255,77,77,.5);}
  @keyframes flow{from{background-position:0 0;}to{background-position:18px 0;}} /* moves left -> right, toward the load */
  .node.fault .icon{border-color:#ff4d4d;background:radial-gradient(circle at 30% 30%,#4a1622,#2a0d14);color:#ff4d4d;box-shadow:0 0 12px rgba(255,77,77,.55);animation:blink 1s infinite;}
  @keyframes blink{50%{opacity:.4;}}

  .grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
  .card{background:#1b2438;border-radius:10px;padding:10px;text-align:center;}
  .card .label{font-size:10px;color:#8b96ad;margin-bottom:4px;}
  .card .val{font-size:19px;font-weight:bold;color:#4fd1ff;}
  .bar-bg{height:6px;background:#0c1220;border-radius:4px;margin-top:6px;overflow:hidden;}
  .bar-fill{height:100%;background:linear-gradient(90deg,#4fd1ff,#2fd27a);width:0%;transition:width .4s;}

  .status-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
  .status-card{background:#1b2438;border-radius:10px;padding:12px;text-align:center;}
  .status-card.full{grid-column:1/3;}
  .ok{color:#2fd27a;}
  .fault-text{color:#ff4d4d;animation:blink 1s infinite;}

  .switch-row{display:flex;justify-content:space-around;flex-wrap:wrap;gap:10px;}
  .switch-box{display:flex;flex-direction:column;align-items:center;font-size:11px;color:#9aa7bd;}
  .switch{width:52px;height:28px;background:#0c1220;border-radius:14px;position:relative;cursor:pointer;border:1px solid #35507a;margin:6px 0;}
  .switch .knob{width:22px;height:22px;background:#4fd1ff;border-radius:50%;position:absolute;top:2px;left:2px;transition:.25s;}
  .switch.on .knob{left:27px;background:#2fd27a;}
  .switch.locked{opacity:.5;cursor:not-allowed;}
  .switch.locked .knob{background:#ff4d4d;}

  #resetBtn{display:block;margin:14px auto 0;padding:10px 26px;background:linear-gradient(135deg,#ff6b4d,#e6392a);border:none;border-radius:20px;color:#fff;font-weight:bold;font-size:12px;letter-spacing:1px;cursor:pointer;box-shadow:0 4px 10px rgba(230,57,42,.35);}
  #resetBtn:active{transform:scale(.96);}

  #trend{width:100%;height:70px;}

  .credits{font-size:11px;color:#6b7690;line-height:1.7;}
  .credits div{text-align:justify;text-align-last:justify;}
  .credits b{color:#9aa7bd;}
  .footer-note{text-align:center;font-size:10px;color:#4a5470;margin-top:10px;}
</style>
</head>
<body>

<h1>IoT BASED POWER GRID FAULT DETECTION SYSTEM</h1>
<h2>( MINI SCADA PROTOTYPE )</h2>

<div class="panel">
  <div class="title">Single Line Diagram</div>
  <div class="line-diagram">
    <div class="node"><div class="icon">G</div>GEN</div>
    <div class="wire"></div>
    <div class="node"><div class="icon">&uarr;</div>STEP-UP</div>
    <div class="wire" id="wireGround"></div>
    <div class="node" id="nodeGround"><div class="icon">&#9013;</div>GND FAULT</div>
    <div class="wire" id="wireLoad"></div>
    <div class="node"><div class="icon">&darr;</div>STEP-DOWN</div>
    <div class="wire"></div>
    <div class="node" id="nodeLoad"><div class="icon">L</div>LOAD</div>
  </div>
</div>

<div class="panel">
  <div class="title">Live Electrical Parameters</div>
  <div class="grid">
    <div class="card"><div class="label">STEP-UP VOLTAGE</div><div class="val" id="vUp">--</div><div class="bar-bg"><div class="bar-fill" id="barVUp"></div></div></div>
    <div class="card"><div class="label">STEP-DOWN VOLTAGE</div><div class="val" id="vDown">--</div><div class="bar-bg"><div class="bar-fill" id="barVDown"></div></div></div>
    <div class="card"><div class="label">LINE CURRENT</div><div class="val" id="current">--</div><div class="bar-bg"><div class="bar-fill" id="barCurrent"></div></div></div>
    <div class="card"><div class="label">TRANSMISSION POWER</div><div class="val" id="power">--</div><div class="bar-bg"><div class="bar-fill" id="barPower"></div></div></div>
  </div>
</div>

<div class="panel">
  <div class="title">Power Trend (kW)</div>
  <svg id="trend" viewBox="0 0 300 70"><polyline id="trendLine" points="" fill="none" stroke="#4fd1ff" stroke-width="2"/></svg>
</div>

<div class="panel">
  <div class="title">System Status</div>
  <div class="status-grid">
    <div class="status-card full"><div class="label">OVERALL STATUS</div><div class="val" id="sysStatus">--</div></div>
    <div class="status-card"><div class="label">GROUND FAULT</div><div class="val" id="groundFault">--</div></div>
    <div class="status-card"><div class="label">LOAD SHORT</div><div class="val" id="loadShort">--</div></div>
    <div class="status-card"><div class="label">BUZZER</div><div class="val" id="buzzer">--</div></div>
    <div class="status-card"><div class="label">UPTIME (s)</div><div class="val" id="uptime">--</div></div>
    <div class="status-card"><div class="label">GND COUNT</div><div class="val" id="gCount">--</div></div>
    <div class="status-card"><div class="label">LOAD COUNT</div><div class="val" id="lCount">--</div></div>
  </div>
</div>

<div class="panel">
  <div class="title">Manual Breaker Control</div>
  <div class="switch-row">
    <div class="switch-box">Load Line 1<div class="switch" id="swA" onclick="sendCmd('A')"><div class="knob"></div></div><span id="lblA">OFF</span></div>
    <div class="switch-box">Load Line 2<div class="switch" id="swB" onclick="sendCmd('B')"><div class="knob"></div></div><span id="lblB">OFF</span></div>
    <div class="switch-box">Ground Line<div class="switch" id="swC" onclick="sendCmd('C')"><div class="knob"></div></div><span id="lblC">OFF</span></div>
  </div>
  <div class="footer-note">A fault on a line auto-trips (locks) its breaker until the fault clears</div>
  <button id="resetBtn" onclick="sendReset()">RESET SYSTEM</button>
</div>

<div class="panel credits">
  <div><b>Project Developer</b> - M. Shoyab Sadik</div>
  <div><b>Report Writing</b> - Md. Sohan</div>
  <div><b>3D Creative Designers</b></div>
  <div>Sadia Zaman Atoshi &nbsp;|&nbsp; Israt Jahan &nbsp;|&nbsp; Moumita Samadder</div>
</div>

<script>
let history = [];
let loadLocked = false, groundLocked = false;

async function sendReset(){
  await fetch('/reset');
  updateData();
}

async function sendCmd(c){
  if (c !== 'C' && loadLocked) return;
  if (c === 'C' && groundLocked) return;
  await fetch('/cmd?c=' + c);
  updateData();
}

function setSwitch(id, lblId, on, locked){
  const sw = document.getElementById(id);
  const lbl = document.getElementById(lblId);
  sw.classList.toggle('on', on);
  sw.classList.toggle('locked', locked);
  lbl.innerText = locked ? "TRIPPED" : (on ? "ON" : "OFF");
}

async function updateData(){
  try{
    const res = await fetch('/data');
    const d = await res.json();

    document.getElementById('groundFault').innerText = d.groundFault ? "FAULT" : "OK";
    document.getElementById('groundFault').className = "val " + (d.groundFault ? "fault-text" : "ok");
    document.getElementById('loadShort').innerText = d.loadShort ? "FAULT" : "OK";
    document.getElementById('loadShort').className = "val " + (d.loadShort ? "fault-text" : "ok");
    document.getElementById('buzzer').innerText = d.buzzer ? "ON" : "OFF";
    document.getElementById('buzzer').className = "val " + (d.buzzer ? "fault-text" : "ok");
    document.getElementById('sysStatus').innerText = (d.groundFault || d.loadShort) ? "!!! FAULT !!!" : "NORMAL";
    document.getElementById('sysStatus').className = "val " + ((d.groundFault || d.loadShort) ? "fault-text" : "ok");
    document.getElementById('uptime').innerText = d.uptime;
    document.getElementById('gCount').innerText = d.gCount;
    document.getElementById('lCount').innerText = d.lCount;

    document.getElementById('vUp').innerText = d.vUp + " V";
    document.getElementById('vDown').innerText = d.vDown + " V";
    document.getElementById('current').innerText = d.current + " A";
    document.getElementById('power').innerText = d.power + " kW";

    document.getElementById('barVUp').style.width = Math.min(100, (d.vUp / 12000) * 100) + "%";
    document.getElementById('barVDown').style.width = Math.min(100, (d.vDown / 250) * 100) + "%";
    document.getElementById('barCurrent').style.width = Math.min(100, (d.current / 15) * 100) + "%";
    document.getElementById('barPower').style.width = Math.min(100, (d.power / 3) * 100) + "%";

    document.getElementById('nodeGround').classList.toggle('fault', !!d.groundFault);
    document.getElementById('nodeLoad').classList.toggle('fault', !!d.loadShort);
    document.getElementById('wireGround').classList.toggle('fault', !!d.groundFault);
    document.getElementById('wireLoad').classList.toggle('fault', !!d.loadShort);

    groundLocked = !!d.groundFault;
    loadLocked = !!d.loadShort;
    setSwitch('swA', 'lblA', d.load1, loadLocked);
    setSwitch('swB', 'lblB', d.load2, loadLocked);
    setSwitch('swC', 'lblC', d.ground, groundLocked);

    history.push(parseFloat(d.power));
    if (history.length > 30) history.shift();
    const maxP = Math.max(3, ...history);
    let pts = history.map((v, i) => (i * (300 / 29)) + "," + (70 - (v / maxP) * 65)).join(" ");
    document.getElementById('trendLine').setAttribute('points', pts);

  } catch(e) {}
}
setInterval(updateData, 600);
updateData();
</script>
</body>
</html>
)rawliteral";

// ============================================================
//                      WEB SERVER HANDLERS
// ============================================================
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData() {
  String json = "{";
  json += "\"groundFault\":" + String(groundFaultActive ? 1 : 0) + ",";
  json += "\"loadShort\":"   + String(loadShortActive ? 1 : 0) + ",";
  json += "\"buzzer\":"      + String(buzzerState ? 1 : 0) + ",";
  json += "\"uptime\":"      + String(millis() / 1000) + ",";
  json += "\"gCount\":"      + String(groundFaultCount) + ",";
  json += "\"lCount\":"      + String(loadShortCount) + ",";
  json += "\"vUp\":"         + String(voltageStepUp, 0) + ",";
  json += "\"vDown\":"       + String(voltageStepDown, 1) + ",";
  json += "\"current\":"     + String(lineCurrent, 2) + ",";
  json += "\"power\":"       + String(transPower, 2) + ",";
  json += "\"load1\":"       + String(effLoad1 ? 1 : 0) + ",";
  json += "\"load2\":"       + String(effLoad2 ? 1 : 0) + ",";
  json += "\"ground\":"      + String(effGround ? 1 : 0);
  json += "}";
  server.send(200, "application/json", json);
}

void handleCmd() {
  if (server.hasArg("c")) {
    String c = server.arg("c");
    if (c == "A") cmdLoad1  = !cmdLoad1;
    else if (c == "B") cmdLoad2  = !cmdLoad2;
    else if (c == "C") cmdGround = !cmdGround;
  }
  server.send(200, "text/plain", "OK");
}

void handleReset() {
  groundFaultCount = 0;
  loadShortCount   = 0;
  cmdLoad1  = false;
  cmdLoad2  = false;
  cmdGround = false;
  server.send(200, "text/plain", "OK");
  // NOTE: if the sensor pin is still physically pulled LOW (real fault still
  // present, or nothing wired to it yet), groundFaultActive/loadShortActive
  // will read true again on the very next loop() and FAULT will reappear -
  // this reset only clears counts/manual state, it cannot hide a real fault.
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

// ============================================================
//                          SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // Fault sensor inputs - internal pull-up used, no external resistor needed
  pinMode(GROUND_FAULT_PIN, INPUT_PULLUP);
  pinMode(LOAD_SHORT_PIN, INPUT_PULLUP);

  // Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Manual breaker relay outputs - default OFF
  pinMode(LOAD1_RELAY_PIN, OUTPUT);
  pinMode(LOAD2_RELAY_PIN, OUTPUT);
  pinMode(GROUND_RELAY_PIN, OUTPUT);
  digitalWrite(LOAD1_RELAY_PIN, LOW);
  digitalWrite(LOAD2_RELAY_PIN, LOW);
  digitalWrite(GROUND_RELAY_PIN, LOW);

  // I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("POWER GRID");
  lcd.setCursor(0, 1);
  lcd.print("FAULT MONITOR");
  delay(1500);

  // -------- WiFi Access Point --------
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Starting AP...");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(500);

  IPAddress apIP = WiFi.softAPIP(); // default: 192.168.4.1

  Serial.println();
  Serial.print("Access Point started. SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Open IP: ");
  Serial.println(apIP);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi: ");
  lcd.print(AP_SSID);
  lcd.setCursor(0, 1);
  lcd.print(apIP);
  delay(4000);

  // -------- Web Server Routes --------
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/cmd", handleCmd);
  server.on("/reset", handleReset);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");

  lcd.clear();
}

// ============================================================
//                          LOOP
// ============================================================
void loop() {
  server.handleClient(); // no blocking delay() anywhere in loop()

  int groundFault = digitalRead(GROUND_FAULT_PIN);
  int loadShort   = digitalRead(LOAD_SHORT_PIN);

  groundFaultActive = (groundFault == LOW);
  loadShortActive   = (loadShort == LOW);

  // ---- Edge detection: count fault start + auto-trip (lock) breakers ----
  if (groundFault == LOW && prevGroundFault == HIGH) {
    groundFaultCount++;
    cmdGround = false; // safety trip - operator must manually re-close after clearing fault
  }
  if (loadShort == LOW && prevLoadShort == HIGH) {
    loadShortCount++;
    cmdLoad1 = false;
    cmdLoad2 = false; // both load lines share this sensor for now
  }
  prevGroundFault = groundFault;
  prevLoadShort   = loadShort;

  // ---- Effective breaker state = manual command AND NOT faulted (fault always wins) ----
  effLoad1  = cmdLoad1  && !loadShortActive;
  effLoad2  = cmdLoad2  && !loadShortActive;
  effGround = cmdGround && !groundFaultActive;

  digitalWrite(LOAD1_RELAY_PIN,  effLoad1  ? HIGH : LOW);
  digitalWrite(LOAD2_RELAY_PIN,  effLoad2  ? HIGH : LOW);
  digitalWrite(GROUND_RELAY_PIN, effGround ? HIGH : LOW);

  // ---- Buzzer ----
  buzzerState = (groundFaultActive || loadShortActive);
  digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);

  // ---- Simulated electrical readings for the dashboard ----
  updateSimulatedReadings();

  // ---- LCD + Serial update every 300 ms (non-blocking) ----
  if (millis() - lastLCDUpdate >= LCD_INTERVAL) {
    lastLCDUpdate = millis();
    lcd.clear();

    if (groundFaultActive) {
      lcd.setCursor(0, 0);
      lcd.print("!!! FAULT !!!");
      lcd.setCursor(0, 1);
      lcd.print("GROUND FAULT");
      Serial.println("GROUND FAULT DETECTED");
    }
    else if (loadShortActive) {
      lcd.setCursor(0, 0);
      lcd.print("!!! FAULT !!!");
      lcd.setCursor(0, 1);
      lcd.print("LOAD SHORT CKT");
      Serial.println("LOAD SHORT CIRCUIT");
    }
    else {
      lcd.setCursor(0, 0);
      lcd.print("GRID STATUS:");
      lcd.setCursor(0, 1);
      lcd.print("NORMAL");
      Serial.println("SYSTEM NORMAL");
    }
  }
}
