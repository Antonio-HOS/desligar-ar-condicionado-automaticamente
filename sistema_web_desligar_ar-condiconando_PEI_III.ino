#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <time.h>

#include <IRremoteESP8266.h>
#include <IRsend.h>

// ============================================================
//                     CONFIGURAÇÕES
// ============================================================

// ---------------- Wi-Fi ----------------

// const char* WIFI_SSID     = "AP102";
// const char* WIFI_PASSWORD = "Hacker@1959";

const char* WIFI_SSID = "Uesc2";
const char* WIFI_PASSWORD = "Uesc@Convidado";


// Portal cativo UESC
const char* WIFI_USERNAME = "01202320094";
const char* WIFI_USER_PASSWORD = "@PC2026.2";


// ---------------- API ----------------

// Troque esta senha.
const char* API_KEY = "123456";

// ---------------- NTP ----------------

const char* NTP_SERVER = "pool.ntp.org";

const long GMT_OFFSET_SEC = -10800;  // UTC-3
const int DAYLIGHT_OFFSET_SEC = 0;

// ---------------- IR ----------------

const uint16_t IR_LED_PIN = 4;

IRsend irsend(IR_LED_PIN);


// ============================================================
//                     COMANDOS IR
// ============================================================

// ------------------------------------------------------------
// COMANDO DESLIGAR
// ------------------------------------------------------------

const uint16_t RAW_OFF[] = { 3110, 1592, 498, 1064, 524, 1064, 526, 288, 528, 290, 530, 290, 530, 1064, 524, 290, 530, 290, 530, 1064, 522, 1064, 524, 290, 530, 1064, 524, 292, 528, 290, 530, 1062, 524, 1064, 522, 290, 530, 1064, 524, 1066, 522, 290, 530, 290, 530, 1064, 522, 290, 530, 290, 530, 290, 530, 1064, 522, 290, 530, 288, 530, 292, 528, 290, 530, 290, 530, 290, 530, 290, 530, 290, 530, 288, 530, 290, 530, 290, 528, 290, 530, 290, 530, 290, 530, 290, 530, 290, 530, 290, 530, 288, 530, 290, 530, 290, 530, 1048, 538, 290, 528, 290, 530, 290, 530, 290, 530, 288, 530, 290, 530, 290, 530, 1044, 542, 1064, 524, 288, 530, 290, 530, 290, 530, 1090, 498, 1088, 498, 290, 530, 290, 530, 1088, 500, 1088, 498, 1090, 498, 290, 530, 290, 530, 1088, 498, 1090, 498, 288, 530, 1090, 498, 288, 532, 288, 532, 288, 530, 290, 530, 290, 530, 290, 530, 290, 528, 290, 530, 170, 648, 136, 684, 156, 662, 134, 686, 162, 658, 160, 660, 164, 654, 156, 662, 164, 632, 220, 598, 228, 594, 158, 660, 226, 594, 230, 588, 230, 590, 230, 588, 324, 496, 322, 496, 324, 494, 238, 582, 322, 496, 324, 496, 324, 494, 326, 494, 324, 496, 324, 496, 324, 496, 324, 494, 1094, 494, 1092, 494, 1092, 494, 324, 496 };



const uint16_t RAW_OFF_SIZE =
  sizeof(RAW_OFF) / sizeof(RAW_OFF[0]);




// ------------------------------------------------------------
// COMANDO LIGAR
// ------------------------------------------------------------
//
// ATENÇÃO:
// Neste momento estou usando o mesmo comando como exemplo.
//
// Você deve substituir RAW_ON pelo vetor capturado quando
// pressionar o botão de LIGAR no controle original.
// ------------------------------------------------------------

const uint16_t RAW_ON[] = { 3088, 1618, 472, 1094, 494, 1094, 494, 326, 494, 324, 494, 326, 494, 1092, 494, 326, 494, 326, 494, 1094, 494, 1094, 494, 326, 494, 1094, 494, 326, 494, 326, 494, 1094, 494, 1094, 494, 326, 494, 1094, 494, 1094, 494, 326, 494, 326, 494, 1094, 494, 326, 494, 324, 494, 326, 494, 1094, 494, 326, 494, 326, 494, 326, 494, 326, 494, 326, 494, 326, 494, 326, 494, 324, 494, 324, 494, 326, 494, 324, 494, 324, 496, 324, 494, 324, 494, 326, 494, 324, 494, 324, 496, 324, 494, 324, 496, 324, 496, 1092, 494, 324, 494, 326, 494, 324, 496, 324, 496, 324, 498, 322, 494, 326, 494, 1092, 494, 1094, 494, 324, 496, 324, 494, 324, 494, 1092, 494, 1092, 496, 324, 496, 324, 496, 1092, 494, 1094, 494, 1094, 496, 324, 496, 324, 496, 1092, 496, 1092, 496, 328, 492, 1092, 494, 324, 496, 324, 496, 324, 496, 324, 496, 324, 496, 324, 496, 324, 494, 324, 496, 324, 496, 324, 496, 324, 496, 324, 494, 324, 496, 324, 496, 324, 496, 324, 496, 324, 496, 324, 494, 324, 496, 326, 494, 324, 496, 324, 496, 322, 496, 324, 496, 324, 496, 324, 496, 324, 496, 322, 496, 324, 496, 324, 496, 324, 496, 324, 496, 324, 496, 322, 496, 324, 496, 228, 592, 1114, 474, 1114, 474, 1114, 474, 168, 644 };

const uint16_t RAW_ON_SIZE =
  sizeof(RAW_ON) / sizeof(RAW_ON[0]);


// ============================================================
//                     SERVIDOR
// ============================================================

WebServer server(80);

Preferences preferences;


// ============================================================
//                     ESTADO DO AC
// ============================================================

bool acLigado = false;


// ============================================================
//                     AGENDAMENTOS
// ============================================================

#define MAX_SCHEDULES 10

struct Schedule {

  bool active;

  uint8_t hour;
  uint8_t minute;

  // Ação:
  // 0 = OFF
  // 1 = ON
  uint8_t action;

  // Dias da semana:
  //
  // bit 0 = domingo
  // bit 1 = segunda
  // bit 2 = terça
  // bit 3 = quarta
  // bit 4 = quinta
  // bit 5 = sexta
  // bit 6 = sábado
  //
  uint8_t days;

  // Último dia em que o agendamento foi executado.
  int lastYear;
  int lastYDay;
};

Schedule schedules[MAX_SCHEDULES];


// ============================================================
//                     UTILITÁRIOS
// ============================================================

String actionToString(uint8_t action) {

  if (action == 1) {
    return "on";
  }

  return "off";
}


uint8_t stringToAction(String action) {

  action.toLowerCase();

  if (action == "on") {
    return 1;
  }

  return 0;
}




String daysToString(uint8_t days) {

  String result = "";

  for (int i = 0; i < 7; i++) {

    if (days & (1 << i)) {
      result += "1";
    } else {
      result += "0";
    }
  }

  return result;
}


uint8_t stringToDays(String value) {

  uint8_t result = 0;

  for (int i = 0; i < 7 && i < value.length(); i++) {

    if (value[i] == '1') {
      result |= (1 << i);
    }
  }

  return result;
}

String urlencode(const String& str) {
  String encoded = "";

  const char* hex = "0123456789ABCDEF";

  for (size_t i = 0; i < str.length(); i++) {
    char c = str[i];

    if (
      (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }

  return encoded;
}

// ============================================================
//                     PERSISTÊNCIA
// ============================================================

void saveSchedules() {

  preferences.begin("ac-controller", false);

  preferences.putBytes(
    "schedules",
    schedules,
    sizeof(schedules));

  preferences.end();

  Serial.println("Agendamentos salvos.");
}


void loadSchedules() {

  preferences.begin("ac-controller", true);

  size_t size = preferences.getBytes(
    "schedules",
    schedules,
    sizeof(schedules));

  preferences.end();

  if (size != sizeof(schedules)) {

    Serial.println("Nenhum agendamento salvo.");

    for (int i = 0; i < MAX_SCHEDULES; i++) {

      schedules[i].active = false;
      schedules[i].hour = 0;
      schedules[i].minute = 0;
      schedules[i].action = 0;
      schedules[i].days = 0;
      schedules[i].lastYear = -1;
      schedules[i].lastYDay = -1;
    }

    saveSchedules();

  } else {

    Serial.println("Agendamentos carregados da memória.");
  }
}


// ============================================================
//                     CONTROLE IR
// ============================================================

void ligarAC() {

  Serial.println("Enviando comando IR: LIGAR");

  for (int i = 0; i < 10; i++) {

    irsend.sendRaw(
      RAW_ON,
      RAW_ON_SIZE,
      38);

    delay(100);
  }

  acLigado = true;

  Serial.println("Comando LIGAR enviado 10x.");
}


void desligarAC() {

  Serial.println("Enviando comando IR: DESLIGAR");

  for (int i = 0; i < 10; i++) {

    irsend.sendRaw(
      RAW_OFF,
      RAW_OFF_SIZE,
      38);

    delay(100);
  }

  acLigado = false;

  Serial.println("Comando DESLIGAR enviado 10x.");
}


void toggleAC() {

  if (acLigado) {

    desligarAC();

  } else {

    ligarAC();
  }
}


// ============================================================
//                     AUTENTICAÇÃO
// ============================================================

bool authenticated() {

  if (!server.hasHeader("X-API-Key")) {

    Serial.println(
      "API: header X-API-Key ausente");

    return false;
  }

  String key =
    server.header("X-API-Key");

  Serial.print("API Key recebida: [");
  Serial.print(key);
  Serial.println("]");

  Serial.print("API Key esperada: [");
  Serial.print(API_KEY);
  Serial.println("]");

  return key == API_KEY;
}


bool requireAuth() {

  if (!authenticated()) {

    server.send(
      401,
      "application/json",
      "{\"error\":\"unauthorized\"}");

    return false;
  }

  return true;
}


// ============================================================
//                     API STATUS
// ============================================================

void handleStatus() {

  if (!requireAuth()) {
    return;
  }

  struct tm timeinfo;

  String currentTime = "";

  if (getLocalTime(&timeinfo)) {

    char buffer[30];

    strftime(
      buffer,
      sizeof(buffer),
      "%Y-%m-%d %H:%M:%S",
      &timeinfo);

    currentTime = buffer;
  }

  String json = "{";

  json += "\"power\":\"";
  json += acLigado ? "on" : "off";
  json += "\",";

  json += "\"wifi\":true,";

  json += "\"ip\":\"";
  json += WiFi.localIP().toString();
  json += "\",";

  json += "\"time\":\"";
  json += currentTime;
  json += "\",";

  json += "\"schedules\":";
  json += String(MAX_SCHEDULES);

  json += "}";

  server.send(
    200,
    "application/json",
    json);
}


// ============================================================
//                     API POWER ON
// ============================================================

void handlePowerOn() {

  if (!requireAuth()) {
    return;
  }

  ligarAC();

  server.send(
    200,
    "application/json",
    "{\"success\":true,\"power\":\"on\"}");
}


// ============================================================
//                     API POWER OFF
// ============================================================

void handlePowerOff() {

  if (!requireAuth()) {
    return;
  }

  desligarAC();

  server.send(
    200,
    "application/json",
    "{\"success\":true,\"power\":\"off\"}");
}


// ============================================================
//                     API TOGGLE
// ============================================================

void handlePowerToggle() {

  if (!requireAuth()) {
    return;
  }

  toggleAC();

  String json = "{\"success\":true,\"power\":\"";

  json += acLigado ? "on" : "off";

  json += "\"}";

  server.send(
    200,
    "application/json",
    json);
}


// ============================================================
//                     GET SCHEDULES
// ============================================================

void handleGetSchedules() {

  if (!requireAuth()) {
    return;
  }

  String json = "[";

  bool first = true;

  for (int i = 0; i < MAX_SCHEDULES; i++) {

    if (!schedules[i].active) {
      continue;
    }

    if (!first) {
      json += ",";
    }

    first = false;

    json += "{";

    json += "\"id\":";
    json += String(i);
    json += ",";

    json += "\"hour\":";
    json += String(schedules[i].hour);
    json += ",";

    json += "\"minute\":";
    json += String(schedules[i].minute);
    json += ",";

    json += "\"action\":\"";
    json += actionToString(schedules[i].action);
    json += "\",";

    json += "\"days\":\"";
    json += daysToString(schedules[i].days);
    json += "\"";

    json += "}";
  }

  json += "]";

  server.send(
    200,
    "application/json",
    json);
}


// ============================================================
//                     CREATE SCHEDULE
// ============================================================
//
// Exemplo:
//
// POST /schedule?hour=18&minute=0&action=off&days=0111110
//
// days:
// domingo = posição 0
// segunda = posição 1
// ...
// sábado = posição 6
//
// 0111110 = segunda a sexta
// 1111111 = todos os dias
// ============================================================

void handleCreateSchedule() {

  if (!requireAuth()) {
    return;
  }

  if (!server.hasArg("hour") || !server.hasArg("minute") || !server.hasArg("action") || !server.hasArg("days")) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"missing parameters\"}");

    return;
  }


  int hour = server.arg("hour").toInt();
  int minute = server.arg("minute").toInt();

  String action = server.arg("action");
  String days = server.arg("days");


  if (hour < 0 || hour > 23) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"invalid hour\"}");

    return;
  }


  if (minute < 0 || minute > 59) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"invalid minute\"}");

    return;
  }


  if (action != "on" && action != "off") {

    server.send(
      400,
      "application/json",
      "{\"error\":\"invalid action\"}");

    return;
  }


  if (days.length() != 7) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"days must have 7 characters\"}");

    return;
  }


  int slot = -1;

  for (int i = 0; i < MAX_SCHEDULES; i++) {

    if (!schedules[i].active) {

      slot = i;
      break;
    }
  }


  if (slot == -1) {

    server.send(
      507,
      "application/json",
      "{\"error\":\"schedule limit reached\"}");

    return;
  }


  schedules[slot].active = true;

  schedules[slot].hour = hour;

  schedules[slot].minute = minute;

  schedules[slot].action =
    stringToAction(action);

  schedules[slot].days =
    stringToDays(days);

  schedules[slot].lastYear = -1;

  schedules[slot].lastYDay = -1;


  saveSchedules();


  String json = "{";

  json += "\"success\":true,";

  json += "\"id\":";
  json += String(slot);
  json += ",";

  json += "\"hour\":";
  json += String(hour);
  json += ",";

  json += "\"minute\":";
  json += String(minute);
  json += ",";

  json += "\"action\":\"";
  json += action;
  json += "\",";

  json += "\"days\":\"";
  json += days;
  json += "\"";

  json += "}";


  server.send(
    201,
    "application/json",
    json);
}


// ============================================================
//                     DELETE SCHEDULE
// ============================================================
//
// DELETE /schedule?id=0
// ============================================================

void handleDeleteSchedule() {

  if (!requireAuth()) {
    return;
  }

  if (!server.hasArg("id")) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"missing id\"}");

    return;
  }


  int id = server.arg("id").toInt();


  if (id < 0 || id >= MAX_SCHEDULES) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"invalid id\"}");

    return;
  }


  schedules[id].active = false;

  saveSchedules();


  server.send(
    200,
    "application/json",
    "{\"success\":true}");
}


// ============================================================
//                     WEB INTERFACE
// ============================================================

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#0f172a">
<title>AC Controller</title>

<style>
:root {
    --bg: #f4f7fb;
    --card: rgba(255,255,255,.92);
    --text: #0f172a;
    --muted: #64748b;
    --border: #e2e8f0;
    --primary: #2563eb;
    --primary-dark: #1d4ed8;
    --success: #16a34a;
    --danger: #dc2626;
    --shadow: 0 18px 45px rgba(15,23,42,.08);
    --radius: 22px;
}

* { box-sizing: border-box; }

body {
    margin: 0;
    min-height: 100vh;
    font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    color: var(--text);
    background:
        radial-gradient(circle at top left, rgba(37,99,235,.13), transparent 32%),
        radial-gradient(circle at top right, rgba(14,165,233,.10), transparent 28%),
        var(--bg);
}

button, input, select { font: inherit; }

button {
    border: 0;
    cursor: pointer;
    transition: transform .18s ease, box-shadow .18s ease, opacity .18s ease;
}

button:hover {
    transform: translateY(-1px);
}

button:active {
    transform: translateY(0);
}

.container {
    width: min(100% - 28px, 820px);
    margin: 0 auto;
    padding: 28px 0 44px;
}

.header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 18px;
    margin-bottom: 22px;
}

.brand {
    display: flex;
    align-items: center;
    gap: 14px;
}

.brand-icon {
    width: 52px;
    height: 52px;
    display: grid;
    place-items: center;
    border-radius: 16px;
    color: white;
    font-size: 25px;
    background: linear-gradient(135deg, #2563eb, #06b6d4);
    box-shadow: 0 12px 25px rgba(37,99,235,.25);
}

.brand h1 {
    margin: 0;
    font-size: clamp(22px, 4vw, 30px);
    letter-spacing: -.7px;
}

.brand p {
    margin: 3px 0 0;
    color: var(--muted);
    font-size: 13px;
}

.connection {
    display: inline-flex;
    align-items: center;
    gap: 7px;
    padding: 9px 12px;
    border-radius: 999px;
    color: #166534;
    background: #dcfce7;
    font-size: 12px;
    font-weight: 700;
}

.connection-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: #22c55e;
    box-shadow: 0 0 0 4px rgba(34,197,94,.14);
}

.card {
    background: var(--card);
    border: 1px solid rgba(255,255,255,.8);
    border-radius: var(--radius);
    padding: 24px;
    margin-bottom: 18px;
    box-shadow: var(--shadow);
    backdrop-filter: blur(10px);
}

.card-title {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin-bottom: 20px;
}

.card-title h2 {
    margin: 0;
    font-size: 18px;
    letter-spacing: -.3px;
}

.card-title p {
    margin: 4px 0 0;
    color: var(--muted);
    font-size: 13px;
}

.status-panel {
    display: grid;
    grid-template-columns: 1fr auto;
    align-items: center;
    gap: 20px;
    padding: 22px;
    border-radius: 18px;
    background: #f8fafc;
    border: 1px solid var(--border);
    margin-bottom: 18px;
}

.status-label {
    color: var(--muted);
    font-size: 12px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: .08em;
}

.status-value {
    display: flex;
    align-items: center;
    gap: 10px;
    margin: 7px 0 5px;
    font-size: 28px;
    font-weight: 800;
    letter-spacing: -.8px;
}

.status-value.on { color: var(--success); }
.status-value.off { color: var(--danger); }

.status-dot {
    width: 13px;
    height: 13px;
    border-radius: 50%;
    background: currentColor;
    box-shadow: 0 0 0 6px color-mix(in srgb, currentColor 12%, transparent);
}

.clock {
    color: var(--muted);
    font-size: 13px;
}

.ac-orb {
    width: 78px;
    height: 78px;
    display: grid;
    place-items: center;
    border-radius: 50%;
    color: #2563eb;
    background: #dbeafe;
    font-size: 34px;
}

.controls {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 10px;
}

.power-btn {
    min-height: 52px;
    border-radius: 14px;
    color: white;
    font-weight: 800;
    box-shadow: 0 8px 18px rgba(15,23,42,.10);
}

.power-btn.on { background: linear-gradient(135deg, #16a34a, #22c55e); }
.power-btn.off { background: linear-gradient(135deg, #dc2626, #ef4444); }
.power-btn.toggle { background: linear-gradient(135deg, #334155, #475569); }

.power-btn.loading {
    opacity: .65;
    pointer-events: none;
}

.form-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 14px;
}

.field { margin-bottom: 15px; }

.field.full { grid-column: 1 / -1; }

label.field-label {
    display: block;
    margin-bottom: 7px;
    color: #334155;
    font-size: 12px;
    font-weight: 800;
}

input[type="number"], select {
    width: 100%;
    height: 46px;
    padding: 0 13px;
    color: var(--text);
    background: white;
    border: 1px solid var(--border);
    border-radius: 12px;
    outline: none;
    transition: border-color .18s, box-shadow .18s;
}

input[type="number"]:focus, select:focus {
    border-color: #60a5fa;
    box-shadow: 0 0 0 4px rgba(37,99,235,.10);
}

.days {
    display: grid;
    grid-template-columns: repeat(7, 1fr);
    gap: 7px;
}

.day {
    position: relative;
}

.day input {
    position: absolute;
    opacity: 0;
    pointer-events: none;
}

.day span {
    display: grid;
    place-items: center;
    min-height: 42px;
    border: 1px solid var(--border);
    border-radius: 11px;
    color: var(--muted);
    background: white;
    font-size: 12px;
    font-weight: 800;
    cursor: pointer;
    transition: .18s ease;
}

.day input:checked + span {
    color: white;
    border-color: var(--primary);
    background: var(--primary);
    box-shadow: 0 7px 14px rgba(37,99,235,.18);
}

.create-btn {
    width: 100%;
    min-height: 50px;
    margin-top: 4px;
    border-radius: 14px;
    color: white;
    background: linear-gradient(135deg, var(--primary), #06b6d4);
    font-weight: 800;
    box-shadow: 0 10px 20px rgba(37,99,235,.18);
}

.schedule-list {
    display: grid;
    gap: 11px;
}

.schedule {
    display: grid;
    grid-template-columns: auto 1fr auto;
    align-items: center;
    gap: 15px;
    padding: 15px;
    border: 1px solid var(--border);
    border-radius: 16px;
    background: #f8fafc;
}

.schedule-time {
    min-width: 78px;
    text-align: center;
    padding: 10px 8px;
    border-radius: 12px;
    color: #1d4ed8;
    background: #dbeafe;
    font-size: 21px;
    font-weight: 900;
    letter-spacing: -.5px;
}

.schedule-info strong {
    display: block;
    margin-bottom: 5px;
    font-size: 14px;
}

.schedule-days {
    color: var(--muted);
    font-size: 12px;
}

.schedule-action {
    display: inline-flex;
    align-items: center;
    gap: 5px;
    margin-top: 4px;
    font-size: 11px;
    font-weight: 800;
}

.schedule-action.on { color: #15803d; }
.schedule-action.off { color: #b91c1c; }

.delete {
    padding: 9px 12px;
    border-radius: 10px;
    color: #b91c1c;
    background: #fee2e2;
    font-size: 12px;
    font-weight: 800;
}

.empty {
    padding: 28px 15px;
    text-align: center;
    color: var(--muted);
    border: 1px dashed #cbd5e1;
    border-radius: 16px;
    background: #f8fafc;
}

.toast {
    position: fixed;
    left: 50%;
    bottom: 22px;
    z-index: 10;
    transform: translate(-50%, 120px);
    max-width: calc(100% - 28px);
    padding: 12px 17px;
    border-radius: 12px;
    color: white;
    background: #0f172a;
    box-shadow: 0 15px 35px rgba(15,23,42,.22);
    font-size: 13px;
    font-weight: 700;
    transition: transform .25s ease;
}

.toast.show {
    transform: translate(-50%, 0);
}

.footer {
    padding: 2px 0 0;
    text-align: center;
    color: #94a3b8;
    font-size: 11px;
}

@media (max-width: 620px) {
    .container {
        width: min(100% - 20px, 820px);
        padding-top: 18px;
    }

    .header {
        align-items: flex-start;
    }

    .connection {
        padding: 8px 10px;
    }

    .status-panel {
        grid-template-columns: 1fr auto;
        padding: 17px;
    }

    .ac-orb {
        width: 62px;
        height: 62px;
        font-size: 28px;
    }

    .controls {
        grid-template-columns: 1fr;
    }

    .form-grid {
        grid-template-columns: 1fr;
        gap: 0;
    }

    .field.full {
        grid-column: auto;
    }

    .days {
        grid-template-columns: repeat(4, 1fr);
    }

    .schedule {
        grid-template-columns: auto 1fr;
    }

    .schedule .delete {
        grid-column: 1 / -1;
        width: 100%;
    }

    .card {
        padding: 18px;
        border-radius: 18px;
    }
}
</style>
</head>

<body>
<div class="container">

    <header class="header">
        <div class="brand">
            <div class="brand-icon">❄</div>
            <div>
                <h1>AC Controller</h1>
                <p>Controle inteligente do ar-condicionado</p>
            </div>
        </div>

        <div class="connection">
            <span class="connection-dot"></span>
            ESP32 online
        </div>
    </header>

    <section class="card">
        <div class="card-title">
            <div>
                <h2>Controle do ar-condicionado</h2>
                <p>Controle remoto via infravermelho</p>
            </div>
        </div>

        <div class="status-panel">
            <div>
                <div class="status-label">Estado atual</div>
                <div id="statusValue" class="status-value">
                    <span class="status-dot"></span>
                    <span>Carregando...</span>
                </div>
                <div id="clock" class="clock">Sincronizando horário...</div>
            </div>
            <div class="ac-orb">❄</div>
        </div>

        <div class="controls">
            <button id="btnOn" class="power-btn on" onclick="power('on')">LIGAR</button>
            <button id="btnOff" class="power-btn off" onclick="power('off')">DESLIGAR</button>
            <button id="btnToggle" class="power-btn toggle" onclick="power('toggle')">TOGGLE</button>
        </div>
    </section>

    <section class="card">
        <div class="card-title">
            <div>
                <h2>Novo agendamento</h2>
                <p>Automatize o controle por horário e dias da semana.</p>
            </div>
        </div>

        <div class="form-grid">
            <div class="field">
                <label class="field-label" for="hour">HORA</label>
                <input id="hour" type="number" min="0" max="23" value="18">
            </div>

            <div class="field">
                <label class="field-label" for="minute">MINUTO</label>
                <input id="minute" type="number" min="0" max="59" value="0">
            </div>

            <div class="field full">
                <label class="field-label" for="action">AÇÃO</label>
                <select id="action">
                    <option value="on">❄ Ligar ar-condicionado</option>
                    <option value="off" selected>⏻ Desligar ar-condicionado</option>
                </select>
            </div>

            <div class="field full">
                <label class="field-label">DIAS DA SEMANA</label>

                <div class="days">
                    <label class="day">
                        <input type="checkbox" class="dayInput" value="0">
                        <span>DOM</span>
                    </label>
                    <label class="day">
                        <input type="checkbox" class="dayInput" value="1" checked>
                        <span>SEG</span>
                    </label>
                    <label class="day">
                        <input type="checkbox" class="dayInput" value="2" checked>
                        <span>TER</span>
                    </label>
                    <label class="day">
                        <input type="checkbox" class="dayInput" value="3" checked>
                        <span>QUA</span>
                    </label>
                    <label class="day">
                        <input type="checkbox" class="dayInput" value="4" checked>
                        <span>QUI</span>
                    </label>
                    <label class="day">
                        <input type="checkbox" class="dayInput" value="5" checked>
                        <span>SEX</span>
                    </label>
                    <label class="day">
                        <input type="checkbox" class="dayInput" value="6">
                        <span>SÁB</span>
                    </label>
                </div>
            </div>
        </div>

        <button class="create-btn" onclick="createSchedule()">＋ Criar agendamento</button>
    </section>

    <section class="card">
        <div class="card-title">
            <div>
                <h2>Agendamentos</h2>
                <p>Rotinas configuradas no ESP32.</p>
            </div>
        </div>

        <div id="schedules" class="schedule-list">
            <div class="empty">Carregando agendamentos...</div>
        </div>
    </section>

    <div class="footer">AC Controller · ESP32 · Controle local</div>
</div>

<div id="toast" class="toast"></div>

<script>
const API_KEY = "123456";

async function api(url, options = {}) {
    options.headers = {
        ...(options.headers || {}),
        "X-API-Key": API_KEY
    };

    const response = await fetch(url, options);

    if (!response.ok) {
        throw new Error("HTTP " + response.status);
    }

    return await response.json();
}

function showToast(message) {
    const toast = document.getElementById("toast");
    toast.textContent = message;
    toast.classList.add("show");

    clearTimeout(window.toastTimer);
    window.toastTimer = setTimeout(() => {
        toast.classList.remove("show");
    }, 2200);
}

function setButtonsLoading(loading) {
    document.querySelectorAll(".power-btn").forEach(button => {
        button.classList.toggle("loading", loading);
    });
}

async function updateStatus() {
    try {
        const data = await api("/status");

        const value = document.getElementById("statusValue");
        const clock = document.getElementById("clock");
        const isOn = data.power === "on";

        value.className = "status-value " + (isOn ? "on" : "off");
        value.innerHTML =
            '<span class="status-dot"></span>' +
            '<span>' + (isOn ? "LIGADO" : "DESLIGADO") + '</span>';

        clock.textContent = "Horário: " + (data.time || "--");

    } catch (error) {
        const value = document.getElementById("statusValue");
        value.className = "status-value off";
        value.innerHTML =
            '<span class="status-dot"></span><span>OFFLINE</span>';

        document.getElementById("clock").textContent =
            "Não foi possível conectar ao ESP32";
    }
}

async function power(action) {
    setButtonsLoading(true);

    try {
        await api("/power/" + action, { method: "POST" });

        const labels = {
            on: "Ar-condicionado ligado",
            off: "Ar-condicionado desligado",
            toggle: "Estado alterado"
        };

        showToast(labels[action] || "Comando enviado");
        await updateStatus();

    } catch (error) {
        showToast("Erro ao enviar comando");
    } finally {
        setButtonsLoading(false);
    }
}

function getDaysString() {
    let days = "0000000";

    document.querySelectorAll(".dayInput").forEach(input => {
        if (input.checked) {
            const index = parseInt(input.value);
            days =
                days.substring(0, index) +
                "1" +
                days.substring(index + 1);
        }
    });

    return days;
}

async function createSchedule() {
    const hour = document.getElementById("hour").value;
    const minute = document.getElementById("minute").value;
    const action = document.getElementById("action").value;
    const days = getDaysString();

    if (days === "0000000") {
        showToast("Selecione pelo menos um dia");
        return;
    }

    try {
        await api(
            `/schedule?hour=${hour}&minute=${minute}&action=${action}&days=${days}`,
            { method: "POST" }
        );

        showToast("Agendamento criado");
        await loadSchedules();

    } catch (error) {
        showToast("Não foi possível criar o agendamento");
    }
}

async function deleteSchedule(id) {
    try {
        await api("/schedule?id=" + id, { method: "DELETE" });

        showToast("Agendamento excluído");
        await loadSchedules();

    } catch (error) {
        showToast("Erro ao excluir agendamento");
    }
}

function formatDays(value) {
    const names = ["DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SÁB"];
    const selected = [];

    for (let i = 0; i < 7; i++) {
        if (value[i] === "1") {
            selected.push(names[i]);
        }
    }

    if (selected.length === 7) return "Todos os dias";
    if (selected.length === 0) return "Nenhum dia";

    return selected.join(" · ");
}

async function loadSchedules() {
    const container = document.getElementById("schedules");

    try {
        const data = await api("/schedules");

        if (data.length === 0) {
            container.innerHTML =
                '<div class="empty">📅<br><br>Nenhum agendamento configurado.</div>';
            return;
        }

        container.innerHTML = "";

        data.forEach(schedule => {
            const div = document.createElement("div");
            div.className = "schedule";

            const hour = String(schedule.hour).padStart(2, "0");
            const minute = String(schedule.minute).padStart(2, "0");
            const isOn = schedule.action === "on";

            div.innerHTML =
                '<div class="schedule-time">' +
                    hour + ":" + minute +
                '</div>' +

                '<div class="schedule-info">' +
                    '<strong>' +
                        (isOn ? "Ligar ar-condicionado" : "Desligar ar-condicionado") +
                    '</strong>' +

                    '<div class="schedule-days">' +
                        formatDays(schedule.days) +
                    '</div>' +

                    '<div class="schedule-action ' + (isOn ? "on" : "off") + '">' +
                        (isOn ? "● LIGAR" : "● DESLIGAR") +
                    '</div>' +
                '</div>' +

                '<button class="delete" onclick="deleteSchedule(' +
                    schedule.id +
                ')">Excluir</button>';

            container.appendChild(div);
        });

    } catch (error) {
        container.innerHTML =
            '<div class="empty">Erro ao carregar os agendamentos.</div>';
    }
}

updateStatus();
loadSchedules();

setInterval(updateStatus, 10000);
</script>
</body>
</html>
)rawliteral";


// ============================================================
//                     PÁGINA PRINCIPAL
// ============================================================

void handleRoot() {

  server.send(
    200,
    "text/html",
    HTML_PAGE);
}


// ============================================================
//                     VERIFICAR AGENDAMENTOS
// ============================================================

void checkSchedules() {

  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {

    return;
  }


  int currentHour =
    timeinfo.tm_hour;

  int currentMinute =
    timeinfo.tm_min;


  int currentDay =
    timeinfo.tm_wday;


  int currentYear =
    timeinfo.tm_year + 1900;


  int currentYDay =
    timeinfo.tm_yday;


  for (int i = 0; i < MAX_SCHEDULES; i++) {

    Schedule& schedule =
      schedules[i];


    if (!schedule.active) {
      continue;
    }


    // Verifica se o dia está habilitado.

    if (!(schedule.days & (1 << currentDay))) {

      continue;
    }


    // Verifica horário.

    if (schedule.hour != currentHour || schedule.minute != currentMinute) {

      continue;
    }


    // Evita executar várias vezes no mesmo minuto.

    if (schedule.lastYear == currentYear && schedule.lastYDay == currentYDay) {

      continue;
    }


    Serial.println();
    Serial.println("==============================");
    Serial.println("AGENDAMENTO EXECUTADO");
    Serial.println("==============================");


    if (schedule.action == 1) {

      Serial.println("Ação: LIGAR");

      ligarAC();

    } else {

      Serial.println("Ação: DESLIGAR");

      desligarAC();
    }


    schedule.lastYear =
      currentYear;

    schedule.lastYDay =
      currentYDay;


    saveSchedules();
  }
}


// ============================================================
//                     WIFI
// ============================================================


bool verificarPortal() {

  Serial.println();
  Serial.println("======================================");
  Serial.println("      VERIFICANDO PORTAL CATIVO");
  Serial.println("======================================");
  Serial.println();

  Serial.println("Tentativa de resolucao DNS...");

  IPAddress ipTeste;

  if (WiFi.hostByName("connectivitycheck.gstatic.com", ipTeste)) {
    Serial.print("DNS OK: ");
    Serial.println(ipTeste);
  } else {
    Serial.println("ERRO: DNS nao conseguiu resolver o dominio.");
    return false;
  }

  Serial.println();
  Serial.println("Testando conexao TCP na porta 80...");

  WiFiClient tcpClient;

  unsigned long inicio = millis();

  if (tcpClient.connect(ipTeste, 80)) {
    Serial.println("TCP OK: conexao estabelecida!");
    tcpClient.stop();
  } else {
    Serial.print("TCP FALHOU apos ");
    Serial.print(millis() - inicio);
    Serial.println(" ms");

    return false;
  }

  Serial.println();
  Serial.println("Testando HTTP...");

  HTTPClient http;

  WiFiClient client;

  const char* checkUrl =
      "http://connectivitycheck.gstatic.com/generate_204";

  if (!http.begin(client, checkUrl)) {
    Serial.println("ERRO: http.begin() falhou.");
    return false;
  }

  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  http.addHeader("User-Agent", "Mozilla/5.0");

  Serial.println("Executando HTTP GET...");

  int httpCode = http.GET();

  Serial.print("HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    Serial.println("HTTP respondeu!");

    String payload = http.getString();

    Serial.print("Resposta: ");
    Serial.println(payload);

    http.end();
    return true;
  }

  Serial.print("Erro HTTP: ");
  Serial.println(http.errorToString(httpCode));

  http.end();

  return false;
}



bool connectWiFi() {

  Serial.println();
  Serial.println("======================================");
  Serial.println("      CONEXAO WI-FI UESC");
  Serial.println("======================================");

  WiFi.mode(WIFI_STA);

  WiFi.disconnect(true);
  delay(500);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  Serial.print(
    "Conectando ao Wi-Fi"
  );

  unsigned long inicio = millis();

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);

    Serial.print(".");

    if (millis() - inicio > 30000) {

      Serial.println();

      Serial.println(
        "ERRO: timeout ao conectar ao Wi-Fi."
      );

      return false;
    }
  }

  Serial.println();

  Serial.println(
    "Wi-Fi conectado!"
  );

  Serial.print(
    "IP: "
  );

  Serial.println(
    WiFi.localIP()
  );

  Serial.print(
    "Gateway: "
  );

  Serial.println(
    WiFi.gatewayIP()
  );

  Serial.print(
    "DNS: "
  );

  Serial.println(
    WiFi.dnsIP()
  );


  // ==========================================================
  // PORTAL CATIVO
  // ==========================================================

  bool autenticado =
    verificarPortal();


  if (!autenticado) {

    Serial.println();

    Serial.println(
      "ATENCAO: portal UESC nao foi autenticado."
    );

    return false;
  }


  Serial.println();

  Serial.println(
    "Wi-Fi pronto para uso."
  );

  return true;
}



// ============================================================
//                     NTP
// ============================================================

void setupTime() {

  configTime(
    GMT_OFFSET_SEC,
    DAYLIGHT_OFFSET_SEC,
    NTP_SERVER
  );

  Serial.println(
    "Sincronizando horário..."
  );

  struct tm timeinfo;

  unsigned long inicio = millis();

  while (!getLocalTime(&timeinfo)) {

    Serial.print(".");

    delay(500);

    // Timeout de 20 segundos
    if (millis() - inicio >= 20000) {

      Serial.println();
      Serial.println(
        "AVISO: Nao foi possivel sincronizar o horario."
      );

      Serial.println(
        "O ESP32 continuara executando."
      );

      return;
    }
  }

  Serial.println();

  Serial.println(
    "Horario sincronizado!"
  );

  Serial.println(
    &timeinfo,
    "%d/%m/%Y %H:%M:%S"
  );
}

// ============================================================
//                     API ROUTES
// ============================================================

void setupRoutes() {

  // ==========================================================
  // Headers que o servidor deve coletar
  // ==========================================================

  const char* headerKeys[] = {
    "X-API-Key"
  };

  server.collectHeaders(
    headerKeys,
    1);


  // ==========================================================
  // Página web
  // ==========================================================

  server.on(
    "/",
    HTTP_GET,
    handleRoot);


  // ==========================================================
  // Status
  // ==========================================================

  server.on(
    "/status",
    HTTP_GET,
    handleStatus);


  // ==========================================================
  // Power ON
  // ==========================================================

  server.on(
    "/power/on",
    HTTP_POST,
    handlePowerOn);


  // ==========================================================
  // Power OFF
  // ==========================================================

  server.on(
    "/power/off",
    HTTP_POST,
    handlePowerOff);


  // ==========================================================
  // Toggle
  // ==========================================================

  server.on(
    "/power/toggle",
    HTTP_POST,
    handlePowerToggle);


  // ==========================================================
  // Agendamentos
  // ==========================================================

  server.on(
    "/schedules",
    HTTP_GET,
    handleGetSchedules);


  server.on(
    "/schedule",
    HTTP_POST,
    handleCreateSchedule);


  server.on(
    "/schedule",
    HTTP_DELETE,
    handleDeleteSchedule);


  // ==========================================================
  // Página inexistente
  // ==========================================================

  server.onNotFound([]() {
    server.send(
      404,
      "application/json",
      "{\"error\":\"not found\"}");
  });


  // ==========================================================
  // Inicia servidor
  // ==========================================================

  server.begin();

  Serial.println(
    "Servidor HTTP iniciado.");
}


// ============================================================
//                     SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(1000);


  Serial.println();
  Serial.println("==============================");
  Serial.println("      AC CONTROLLER ESP32");
  Serial.println("==============================");


  // IR

  irsend.begin();

  Serial.println(
    "Emissor IR inicializado.");


  // Wi-Fi

bool wifiOK = connectWiFi();

if (wifiOK) {

  setupTime();

} else {

  Serial.println();
  Serial.println(
    "Wi-Fi/portal ainda nao esta pronto."
  );

  Serial.println(
    "O ESP32 continuara executando."
  );
}

  // Memória

  loadSchedules();


  // mDNS

  if (
    MDNS.begin("ac-controller")) {

    Serial.println(
      "mDNS disponível:");

    Serial.println(
      "http://ac-controller.local");
  }


  // API

  setupRoutes();


  Serial.println();
  Serial.println("==============================");

  Serial.print(
    "Painel: http://");

  Serial.println(
    WiFi.localIP());

  Serial.println(
    "==============================");
}


// ============================================================
//                     LOOP
// ============================================================

void loop() {

  // Processa requisições HTTP

  server.handleClient();


  // Verifica agendamentos

  static unsigned long lastScheduleCheck = 0;


  if (
    millis() - lastScheduleCheck >= 1000) {

    lastScheduleCheck =
      millis();


    checkSchedules();
  }


  // Reconecta Wi-Fi se cair

  static unsigned long lastWiFiCheck = 0;


  if (
    millis() - lastWiFiCheck >= 10000) {

    lastWiFiCheck =
      millis();


  if (WiFi.status() != WL_CONNECTED) {

  Serial.println(
    "Wi-Fi desconectado. Reconectando..."
  );

  bool wifiOK = connectWiFi();

  if (wifiOK) {

    setupTime();
  }
}
  }
}