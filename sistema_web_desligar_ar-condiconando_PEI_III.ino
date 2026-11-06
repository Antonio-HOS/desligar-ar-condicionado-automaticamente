#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Tcl.h>  // Protocolo TCL112AC (ar-condicionado TCL)

// ============================================================
//                     CONFIGURAÇÕES
// ============================================================

// ---------------- Wi-Fi ----------------

const char* WIFI_SSID = "Uesc2";
const char* WIFI_PASSWORD = "Uesc@Convidado";

// Portal cativo UESC
const char* WIFI_USERNAME = "01202320094";
const char* WIFI_USER_PASSWORD = "@PC2026.2";

// ---------------- API ----------------

// Troque esta senha (e a do HTML abaixo, const API_KEY no <script>).
const char* API_KEY = "123456";

// ---------------- NTP ----------------

const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = -10800;  // UTC-3
const int DAYLIGHT_OFFSET_SEC = 0;

// ---------------- Rede / timeouts ----------------

const uint16_t HTTP_TIMEOUT_MS = 8000;            // 3000 era curto demais (log: -1 em exatos 3 s)
const uint16_t PORTAL_TIMEOUT_MS = 8000;          // roda em task propria: nao trava mais a interface
const unsigned long WIFI_RETRY_MS = 30000;        // reinicia tentativa de Wi-Fi
const unsigned long NET_CHECK_OK_MS = 60000;      // checa internet a cada 60 s se estiver ok
const unsigned long NET_CHECK_FAIL_MS = 15000;    // tenta reautenticar a cada 15 s se falhar

// URLs de teste de conectividade (mesmas do Windows)
const char* URL_CONNECT_TEST = "http://www.msftconnecttest.com/connecttest.txt";
const char* URL_REDIRECT_TEST = "http://www.msftconnecttest.com/redirect";

// ---------------- IR ----------------

const uint16_t IR_LED_PIN = 4;
const uint8_t IR_REPEATS = 1;           // antes: 10
const uint16_t IR_REPEAT_DELAY_MS = 40; // antes: 100

// Temperatura/modo usados ao LIGAR (o DESLIGAR manda o mesmo estado,
// apenas com o bit Power apagado, igual ao controle original TCL).
const float TCL_TEMPERATURA_C = 24.0;  // ajuste aqui: 16 a 31 °C (passos de 0,5)

IRTcl112Ac acTcl(IR_LED_PIN);  // Emissor IR já no protocolo TCL112AC

// ============================================================
//                     COMANDOS IR
// ============================================================

// ------------------------------------------------------------
// CODIGOS DO CONTROLE TCL - Protocolo TCL112AC (112 bits, 38 kHz)
// ------------------------------------------------------------
// Compativel com os ar-condicionados TCL que usam esse protocolo,
// ex.: linha TAC-09/12/18/24CHSD, TAC-09CHSA e controles GYKQ-52.
// (Se o aparelho for da linha com controle GYKQ-58, o protocolo e
//  o TCL96AC e estes estados nao servem - precisa capturar o
//  controle original com um receptor IR.)
//
// O controle TCL nao manda "teclas" isoladas: manda o ESTADO
// COMPLETO do aparelho (14 bytes). LIGAR e DESLIGAR sao o mesmo
// estado, mudando so o bit Power (byte 5) e o checksum (byte 13):
//
//   Byte 0-2 : 23 CB 26  -> cabecalho fixo TCL
//   Byte 3   : 01        -> tipo de mensagem (normal)
//   Byte 5   : 24 = LIGADO  |  20 = DESLIGADO   (bit 2 = Power)
//   Byte 6   : 03        -> modo Cool (resfriar)
//   Byte 7   : 07        -> 24 C  (temp = 31 - 7)
//   Byte 8   : 40        -> ventilador automatico
//   Byte 12  : 80        -> marca TCL (modelo TAC09CHSD)
//   Byte 13  : checksum  -> soma dos bytes 0-12 (& 0xFF),
//                           recalculado pela biblioteca no envio
//
// Fonte dos estados (estado conhecido/bom da biblioteca
// IRremoteESP8266, capturado de controle TCL real):
//   LIGAR    : 23 CB 26 01 00 24 03 07 40 00 00 00 80 03
//   DESLIGAR : 23 CB 26 01 00 20 03 07 40 00 00 00 80 FF

// COMANDO LIGAR - Cool, 24 C, ventilador automatico
const uint8_t TCL_LIGAR[] = {
  0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x07,
  0x40, 0x00, 0x00, 0x00, 0x80, 0x03
};

// COMANDO DESLIGAR - mesmo estado, bit Power apagado (byte 5: 0x20)
const uint8_t TCL_DESLIGAR[] = {
  0x23, 0xCB, 0x26, 0x01, 0x00, 0x20, 0x03, 0x07,
  0x40, 0x00, 0x00, 0x00, 0x80, 0xFF
};


// ============================================================
//                     SERVIDOR
// ============================================================

WebServer server(80);
Preferences preferences;

// ============================================================
//                     ESTADO
// ============================================================

bool acLigado = false;

// Comando IR pendente: é enviado no loop(), DEPOIS de responder o HTTP.
// -1 = nenhum, 0 = desligar, 1 = ligar
volatile int8_t pendingIR = -1;

// Estado de rede (mexido pela task de rede no nucleo 0 e lido no loop)
bool wifiConnecting = false;
bool mdnsStarted = false;
bool ntpConfigured = false;
volatile bool internetOK = false;
unsigned long wifiStartMs = 0;
unsigned long wifiConnectedAtMs = 0;  // quando o Wi-Fi conectou (espera o DHCP estabilizar)
unsigned long wifiDownSinceMs = 0;    // desde quando esta caido (debounce p/ reconectar)
unsigned long lastNetCheckMs = 0;

// ============================================================
//                     AGENDAMENTOS
// ============================================================

#define MAX_SCHEDULES 10

struct Schedule {
  bool active;
  uint8_t hour;
  uint8_t minute;
  uint8_t action;  // 0 = OFF, 1 = ON
  uint8_t days;    // bit 0 = domingo ... bit 6 = sábado
  int lastYear;
  int lastYDay;
};

Schedule schedules[MAX_SCHEDULES];

// ============================================================
//                     UTILITÁRIOS
// ============================================================

String actionToString(uint8_t action) {
  return action == 1 ? "on" : "off";
}

uint8_t stringToAction(String action) {
  action.toLowerCase();
  return action == "on" ? 1 : 0;
}

String daysToString(uint8_t days) {
  String result = "";
  for (int i = 0; i < 7; i++) {
    result += (days & (1 << i)) ? "1" : "0";
  }
  return result;
}

uint8_t stringToDays(String value) {
  uint8_t result = 0;
  for (int i = 0; i < 7 && i < (int)value.length(); i++) {
    if (value[i] == '1') {
      result |= (1 << i);
    }
  }
  return result;
}

String urlEncode(const String& value) {
  String encoded = "";
  const char* hex = "0123456789ABCDEF";

  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];

    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }

  return encoded;
}

// Hora local SEM bloquear (getLocalTime padrão espera até 5 s!)
bool getTimeNow(struct tm* info) {
  return getLocalTime(info, 0);
}

// ============================================================
//                     PERSISTÊNCIA
// ============================================================

void saveSchedules() {
  preferences.begin("ac-controller", false);
  preferences.putBytes("schedules", schedules, sizeof(schedules));
  preferences.end();
  Serial.println("Agendamentos salvos.");
}

void loadSchedules() {
  preferences.begin("ac-controller", true);
  size_t size = preferences.getBytes("schedules", schedules, sizeof(schedules));
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

    // lastYear/lastYDay só vivem em RAM
    for (int i = 0; i < MAX_SCHEDULES; i++) {
      schedules[i].lastYear = -1;
      schedules[i].lastYDay = -1;
    }
  }
}

// ============================================================
//                     CONTROLE IR
// ============================================================

void enviarIR(bool ligar) {
  Serial.println(ligar ? "Enviando IR (TCL): LIGAR" : "Enviando IR (TCL): DESLIGAR");

  // Carrega o estado TCL correspondente e reaplica as configuracoes.
  // setTemp/setMode/setFan ajustam os bytes e a biblioteca recalcula
  // o checksum (byte 13) na hora do envio: nao precisa mexer na mao.
  acTcl.setRaw(ligar ? TCL_LIGAR : TCL_DESLIGAR);
  acTcl.setMode(kTcl112AcCool);        // Resfriar
  acTcl.setTemp(TCL_TEMPERATURA_C);    // 24 °C (TCL_TEMPERATURA_C)
  acTcl.setFan(kTcl112AcFanAuto);      // Ventilador automatico
  acTcl.setPower(ligar);               // Bit Power (byte 5)

  Serial.println("Estado TCL: " + acTcl.toString());

  for (int i = 0; i < IR_REPEATS; i++) {
    acTcl.send();  // TCL112AC: header 3000/1650 us, 112 bits, 38 kHz
    delay(IR_REPEAT_DELAY_MS);
  }
}

// Usado pelos agendamentos (executa na hora)
void ligarAC() {
  enviarIR(true);
  acLigado = true;
}

void desligarAC() {
  enviarIR(false);
  acLigado = false;
}

// Usado pela API: atualiza o estado e agenda o envio para depois da resposta HTTP
void solicitarAC(bool ligar) {
  acLigado = ligar;
  pendingIR = ligar ? 1 : 0;
}

void processarIRPendente() {
  if (pendingIR < 0) return;

  int8_t cmd = pendingIR;
  pendingIR = -1;

  enviarIR(cmd == 1);
}

// ============================================================
//                     AUTENTICAÇÃO
// ============================================================

bool authenticated() {
  if (!server.hasHeader("X-API-Key")) {
    return false;
  }
  return server.header("X-API-Key") == API_KEY;
}

bool requireAuth() {
  if (!authenticated()) {
    server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return false;
  }
  return true;
}

// ============================================================
//                     API
// ============================================================

void handleStatus() {
  if (!requireAuth()) return;

  struct tm timeinfo;
  String currentTime = "";

  if (getTimeNow(&timeinfo)) {
    char buffer[30];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    currentTime = buffer;
  }

  String json = "{";
  json += "\"power\":\"";
  json += acLigado ? "on" : "off";
  json += "\",";
  json += "\"wifi\":true,";
  json += "\"internet\":";
  json += internetOK ? "true" : "false";
  json += ",";
  json += "\"ip\":\"";
  json += WiFi.localIP().toString();
  json += "\",";
  json += "\"time\":\"";
  json += currentTime;
  json += "\",";
  json += "\"schedules\":";
  json += String(MAX_SCHEDULES);
  json += "}";

  server.send(200, "application/json", json);
}

void handlePowerOn() {
  if (!requireAuth()) return;

  server.send(200, "application/json", "{\"success\":true,\"power\":\"on\"}");
  solicitarAC(true);  // IR é enviado no loop, depois da resposta
}

void handlePowerOff() {
  if (!requireAuth()) return;

  server.send(200, "application/json", "{\"success\":true,\"power\":\"off\"}");
  solicitarAC(false);
}

void handlePowerToggle() {
  if (!requireAuth()) return;

  bool novoEstado = !acLigado;

  String json = "{\"success\":true,\"power\":\"";
  json += novoEstado ? "on" : "off";
  json += "\"}";

  server.send(200, "application/json", json);
  solicitarAC(novoEstado);
}

void handleGetSchedules() {
  if (!requireAuth()) return;

  String json = "[";
  bool first = true;

  for (int i = 0; i < MAX_SCHEDULES; i++) {
    if (!schedules[i].active) continue;

    if (!first) json += ",";
    first = false;

    json += "{";
    json += "\"id\":" + String(i) + ",";
    json += "\"hour\":" + String(schedules[i].hour) + ",";
    json += "\"minute\":" + String(schedules[i].minute) + ",";
    json += "\"action\":\"" + actionToString(schedules[i].action) + "\",";
    json += "\"days\":\"" + daysToString(schedules[i].days) + "\"";
    json += "}";
  }

  json += "]";

  server.send(200, "application/json", json);
}

// POST /schedule?hour=18&minute=0&action=off&days=0111110
void handleCreateSchedule() {
  if (!requireAuth()) return;

  if (!server.hasArg("hour") || !server.hasArg("minute") || !server.hasArg("action") || !server.hasArg("days")) {
    server.send(400, "application/json", "{\"error\":\"missing parameters\"}");
    return;
  }

  int hour = server.arg("hour").toInt();
  int minute = server.arg("minute").toInt();
  String action = server.arg("action");
  String days = server.arg("days");

  if (hour < 0 || hour > 23) {
    server.send(400, "application/json", "{\"error\":\"invalid hour\"}");
    return;
  }

  if (minute < 0 || minute > 59) {
    server.send(400, "application/json", "{\"error\":\"invalid minute\"}");
    return;
  }

  if (action != "on" && action != "off") {
    server.send(400, "application/json", "{\"error\":\"invalid action\"}");
    return;
  }

  if (days.length() != 7) {
    server.send(400, "application/json", "{\"error\":\"days must have 7 characters\"}");
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
    server.send(507, "application/json", "{\"error\":\"schedule limit reached\"}");
    return;
  }

  schedules[slot].active = true;
  schedules[slot].hour = hour;
  schedules[slot].minute = minute;
  schedules[slot].action = stringToAction(action);
  schedules[slot].days = stringToDays(days);
  schedules[slot].lastYear = -1;
  schedules[slot].lastYDay = -1;

  String json = "{";
  json += "\"success\":true,";
  json += "\"id\":" + String(slot) + ",";
  json += "\"hour\":" + String(hour) + ",";
  json += "\"minute\":" + String(minute) + ",";
  json += "\"action\":\"" + action + "\",";
  json += "\"days\":\"" + days + "\"";
  json += "}";

  server.send(201, "application/json", json);

  saveSchedules();  // grava na flash depois de responder
}

// DELETE /schedule?id=0
void handleDeleteSchedule() {
  if (!requireAuth()) return;

  if (!server.hasArg("id")) {
    server.send(400, "application/json", "{\"error\":\"missing id\"}");
    return;
  }

  int id = server.arg("id").toInt();

  if (id < 0 || id >= MAX_SCHEDULES) {
    server.send(400, "application/json", "{\"error\":\"invalid id\"}");
    return;
  }

  schedules[id].active = false;

  server.send(200, "application/json", "{\"success\":true}");

  saveSchedules();
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

button:hover { transform: translateY(-1px); }
button:active { transform: translateY(0); }

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

.day { position: relative; }

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

.toast.show { transform: translate(-50%, 0); }

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

    .header { align-items: flex-start; }
    .connection { padding: 8px 10px; }

    .status-panel {
        grid-template-columns: 1fr auto;
        padding: 17px;
    }

    .ac-orb {
        width: 62px;
        height: 62px;
        font-size: 28px;
    }

    .controls { grid-template-columns: 1fr; }

    .form-grid {
        grid-template-columns: 1fr;
        gap: 0;
    }

    .field.full { grid-column: auto; }

    .days { grid-template-columns: repeat(4, 1fr); }

    .schedule { grid-template-columns: auto 1fr; }

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
                    <label class="day"><input type="checkbox" class="dayInput" value="0"><span>DOM</span></label>
                    <label class="day"><input type="checkbox" class="dayInput" value="1" checked><span>SEG</span></label>
                    <label class="day"><input type="checkbox" class="dayInput" value="2" checked><span>TER</span></label>
                    <label class="day"><input type="checkbox" class="dayInput" value="3" checked><span>QUA</span></label>
                    <label class="day"><input type="checkbox" class="dayInput" value="4" checked><span>QUI</span></label>
                    <label class="day"><input type="checkbox" class="dayInput" value="5" checked><span>SEX</span></label>
                    <label class="day"><input type="checkbox" class="dayInput" value="6"><span>SÁB</span></label>
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
const API_TIMEOUT_MS = 6000;

async function api(url, options = {}) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), API_TIMEOUT_MS);

    options.headers = {
        ...(options.headers || {}),
        "X-API-Key": API_KEY
    };
    options.signal = controller.signal;

    try {
        const response = await fetch(url, options);

        if (!response.ok) {
            throw new Error("HTTP " + response.status);
        }

        return await response.json();
    } finally {
        clearTimeout(timer);
    }
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

let statusBusy = false;

async function updateStatus() {
    if (statusBusy) return;
    statusBusy = true;

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
    } finally {
        statusBusy = false;
    }
}

async function power(action) {
    setButtonsLoading(true);

    try {
        const data = await api("/power/" + action, { method: "POST" });

        const labels = {
            on: "Ar-condicionado ligado",
            off: "Ar-condicionado desligado",
            toggle: "Estado alterado"
        };

        showToast(labels[action] || "Comando enviado");

        // Atualiza a tela na hora com a resposta, sem esperar outra requisição
        const value = document.getElementById("statusValue");
        const isOn = data.power === "on";
        value.className = "status-value " + (isOn ? "on" : "off");
        value.innerHTML =
            '<span class="status-dot"></span>' +
            '<span>' + (isOn ? "LIGADO" : "DESLIGADO") + '</span>';

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

void handleRoot() {
  server.send(200, "text/html", HTML_PAGE);
}

// ============================================================
//                     VERIFICAR AGENDAMENTOS
// ============================================================

void checkSchedules() {
  struct tm timeinfo;

  if (!getTimeNow(&timeinfo)) {
    return;
  }

  int currentHour = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;
  int currentDay = timeinfo.tm_wday;
  int currentYear = timeinfo.tm_year + 1900;
  int currentYDay = timeinfo.tm_yday;

  for (int i = 0; i < MAX_SCHEDULES; i++) {
    Schedule& schedule = schedules[i];

    if (!schedule.active) continue;
    if (!(schedule.days & (1 << currentDay))) continue;
    if (schedule.hour != currentHour || schedule.minute != currentMinute) continue;

    // Evita executar várias vezes no mesmo minuto
    if (schedule.lastYear == currentYear && schedule.lastYDay == currentYDay) continue;

    Serial.println();
    Serial.println("AGENDAMENTO EXECUTADO");

    if (schedule.action == 1) {
      Serial.println("Ação: LIGAR");
      ligarAC();
    } else {
      Serial.println("Ação: DESLIGAR");
      desligarAC();
    }

    // Só em RAM: não grava na flash a cada execução
    schedule.lastYear = currentYear;
    schedule.lastYDay = currentYDay;
  }
}

// ============================================================
//                     PORTAL CATIVO / INTERNET
// ============================================================

// Retorna true se há internet de verdade (sem portal interceptando).
bool temInternet() {
  WiFiClient client;
  HTTPClient http;

  if (!http.begin(client, URL_CONNECT_TEST)) {
    return false;
  }

  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("User-Agent", "Mozilla/5.0");

  int code = http.GET();
  bool ok = false;

  if (code == 200) {
    String body = http.getString();
    // Portais cativos às vezes respondem 200 com a página de login
    ok = body.startsWith("Microsoft Connect Test");
  }

  http.end();

  return ok;
}

// Extrai o MAGIC de um Location/corpo FortiGate:
//   .../fgtauth?1a2b3c...   ou   ...?magic=1a2b3c...   ou   ...?1a2b3c...
String extrairMagic(const String& texto) {
  String trecho = "";
  // Formulario de login: <input name="magic" value="abcd1234...">
  int nome = texto.indexOf("\"magic\"");
  if (nome >= 0) {
    int v = texto.indexOf("value=\"", nome);
    if (v >= 0 && v - nome < 80) trecho = texto.substring(v + 7);
  }
  if (trecho.length() == 0) {
    int fg = texto.indexOf("fgtauth?");
    if (fg >= 0) {
      trecho = texto.substring(fg + 8);
    } else {
      int mg = texto.indexOf("magic=");
      if (mg >= 0) {
        trecho = texto.substring(mg + 6);
      } else {
        int q = texto.indexOf('?');
        if (q < 0) return "";
        trecho = texto.substring(q + 1);
      }
    }
  }
  // O magic e' alfanumerico: corta em aspas, '&', espaco, '<', etc.
  String magic = "";
  for (size_t i = 0; i < trecho.length(); i++) {
    char c = trecho[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_') {
      magic += c;
    } else {
      break;
    }
  }
  return magic;
}

// GET numa URL de teste SEM seguir o redirect. Retorna true se achou o MAGIC.
// Antes da autenticacao o DNS externo pode falhar/atrasar, por isso o
// chamador tenta primeiro uma URL por IP literal (dispensa DNS).
bool tentarObterMagic(const String& urlTeste, String& magicOut, String& locationOut) {
  WiFiClient client;
  HTTPClient http;
  http.setReuse(false);

  if (!http.begin(client, urlTeste)) {
    Serial.println("[Portal] http.begin falhou: " + urlTeste);
    return false;
  }

  http.setConnectTimeout(PORTAL_TIMEOUT_MS);
  http.setTimeout(PORTAL_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  const char* headers[] = { "Location" };
  http.collectHeaders(headers, 1);
  http.addHeader("User-Agent", "Mozilla/5.0");
  http.addHeader("Cache-Control", "no-cache");

  int httpCode = http.GET();

  // -1 = HTTPC_ERROR_CONNECTION_FAILED (conexao/DNS/timeout), nao e' resposta HTTP!
  Serial.printf("[Portal] GET %s -> %d (%s)\n", urlTeste.c_str(), httpCode,
                http.errorToString(httpCode).c_str());

  if (httpCode <= 0) {
    http.end();
    return false;
  }

  locationOut = http.header("Location");
  String body = http.getString();  // portais as vezes mandam o magic no corpo (meta-refresh/JS)
  http.end();

  if (locationOut.length() > 0) {
    Serial.println("[Portal] Location: " + locationOut);
    magicOut = extrairMagic(locationOut);
    if (magicOut.length() > 0) return true;
  }

  if (body.length() > 0) {
    Serial.println("[Portal] Corpo (" + String(body.length()) + " bytes): " +
                   body.substring(0, 300));
    // No corpo so' aceita fgtauth/magic explicitos, senao um '?' qualquer
    // de uma pagina comum viraria um magic falso.
    if (body.indexOf("fgtauth") >= 0 || body.indexOf("magic=") >= 0) {
      magicOut = extrairMagic(body);
      if (magicOut.length() > 0) return true;
    }
  }

  return false;
}

// Igual a tentarObterMagic(), mas via HTTPS (certificado nao verificado).
// Necessario porque o firewall da UESC esta RECUSANDO (RST) o HTTP externo
// em vez de redirecionar: pode ser que ele so' intercepte HTTPS, ou que o
// portal so' atenda em https://gateway:1003/.
bool tentarObterMagicHttps(const String& urlTeste, String& magicOut, String& locationOut) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setReuse(false);

  if (!http.begin(client, urlTeste)) {
    Serial.println("[Portal] http.begin (HTTPS) falhou: " + urlTeste);
    return false;
  }

  http.setConnectTimeout(PORTAL_TIMEOUT_MS);
  http.setTimeout(PORTAL_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  const char* headers[] = { "Location" };
  http.collectHeaders(headers, 1);
  http.addHeader("User-Agent", "Mozilla/5.0");
  http.addHeader("Cache-Control", "no-cache");

  int httpCode = http.GET();
  Serial.printf("[Portal] GET %s -> %d (%s)\n", urlTeste.c_str(), httpCode,
                http.errorToString(httpCode).c_str());

  if (httpCode <= 0) {
    http.end();
    return false;
  }

  locationOut = http.header("Location");
  String body = http.getString();
  http.end();

  if (locationOut.length() > 0) {
    Serial.println("[Portal] Location: " + locationOut);
    magicOut = extrairMagic(locationOut);
    if (magicOut.length() > 0) return true;
  }

  if (body.length() > 0) {
    Serial.println("[Portal] Corpo (" + String(body.length()) + " bytes): " +
                   body.substring(0, 300));
    if (body.indexOf("fgtauth") >= 0 || body.indexOf("magic") >= 0) {
      magicOut = extrairMagic(body);
      if (magicOut.length() > 0) return true;
    }
  }

  return false;
}

bool verificarPortal() {
  // Se o Wi-Fi caiu no meio da tentativa anterior, nem tenta: era isso que
  // gerava o "Gateway: 0.0.0.0" logo depois do "Wi-Fi conectado!".
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Portal] Wi-Fi nao esta conectado. Abortando tentativa.");
    return false;
  }

  Serial.println();
  Serial.println("=== PORTAL CATIVO UESC ===");

  IPAddress gateway = WiFi.gatewayIP();

  Serial.print("Gateway: ");
  Serial.println(gateway);
  Serial.print("Mascara: ");
  Serial.println(WiFi.subnetMask());
  Serial.print("DNS: ");
  Serial.println(WiFi.dnsIP());

  if (gateway == IPAddress(0, 0, 0, 0)) {
    Serial.println("ERRO: gateway nao foi obtido.");
    return false;
  }

  // Diagnostico: se o DNS externo nao resolver, e' esperado antes do login;
  // nesse caso so' a URL por IP consegue provocar o redirect do FortiGate.
  IPAddress ipTeste;
  if (WiFi.hostByName("www.msftconnecttest.com", ipTeste) == 1) {
    Serial.print("[Portal] DNS msftconnecttest.com -> ");
    Serial.println(ipTeste);
  } else {
    Serial.println("[Portal] DNS externo NAO resolveu (normal antes de autenticar).");
  }

  const String redirectOriginal = URL_REDIRECT_TEST;

  // ---- 1. Descobre o MAGIC pelo redirect do FortiGate ----
  // Ordem: IP literal (sem DNS) -> redirect Microsoft -> Google -> neverssl.
  const char* urlsTeste[] = {
    "http://1.1.1.1/",
    URL_REDIRECT_TEST,
    "http://connectivitycheck.gstatic.com/generate_204",
    "http://neverssl.com/"
  };

  String magic = "";
  String location = "";

  for (int i = 0; i < 4 && magic.length() == 0; i++) {
    tentarObterMagic(urlsTeste[i], magic, location);
    if (magic.length() == 0) delay(1000);  // respira entre tentativas
  }

  // HTTP externo recusado (-1 em tudo)? Tenta disparar o portal via HTTPS.
  if (magic.length() == 0) {
    Serial.println("[Portal] HTTP externo nao gerou redirect. Tentando HTTPS...");
    tentarObterMagicHttps("https://1.1.1.1/", magic, location);
    if (magic.length() == 0) {
      tentarObterMagicHttps("https://connectivitycheck.gstatic.com/generate_204", magic, location);
    }
  }

  // Ainda nada? Sonda o proprio gateway nas portas classicas do FortiGate.
  // Mesmo sem magic, o corpo/Location dessas respostas revela a URL real
  // do portal (anote no log e confira no navegador do celular/notebook).
  if (magic.length() == 0) {
    Serial.println("[Portal] Sondando o gateway diretamente...");
    String gw = gateway.toString();
    tentarObterMagic("http://" + gw + ":1000/", magic, location);
    if (magic.length() == 0) tentarObterMagicHttps("https://" + gw + ":1003/", magic, location);
    if (magic.length() == 0) tentarObterMagic("http://" + gw + "/", magic, location);
    if (magic.length() == 0) tentarObterMagicHttps("https://" + gw + "/", magic, location);
  }

  if (magic.length() == 0) {
    Serial.println("ERRO: nao foi possivel obter o MAGIC (veja os GET acima).");
    Serial.println("Dica: -1 repetido = portal nao interceptou a conexao ou DNS/timeout;");
    Serial.println("      200 sem fgtauth = pagina diferente da esperada (veja o corpo).");
    return false;
  }

  Serial.print("MAGIC: ");
  Serial.println(magic);

  // ---- 2. POST de autenticacao ----
  // Deriva esquema/host/porta do Location real (ex.: https://gw:1003/fgtauth?...)
  // em vez de assumir sempre http://gateway:1000/.
  String postURL = "http://" + gateway.toString() + ":1000/";
  bool usaHttps = false;

  if (location.startsWith("https://")) {
    usaHttps = true;
    int barra = location.indexOf('/', 8);
    if (barra >= 0) postURL = location.substring(0, barra + 1);
  } else if (location.startsWith("http://")) {
    int barra = location.indexOf('/', 7);
    if (barra >= 0) postURL = location.substring(0, barra + 1);
  }

  Serial.println("[Portal] POST em: " + postURL + (usaHttps ? " (HTTPS, certificado nao verificado)" : ""));

  String postData =
    "4Tredir=" + urlEncode(redirectOriginal) + "&magic=" + urlEncode(magic) + "&username=" + urlEncode(WIFI_USERNAME) + "&password=" + urlEncode(WIFI_USER_PASSWORD);

  int authCode = -1;
  String authBody = "";
  String authErro = "";

  if (usaHttps) {
    WiFiClientSecure authClient;
    authClient.setInsecure();  // portal interno: certificado autoassinado / da instituicao
    HTTPClient authHttp;
    if (!authHttp.begin(authClient, postURL)) {
      Serial.println("ERRO: nao foi possivel conectar ao portal (HTTPS).");
      return false;
    }
    authHttp.setConnectTimeout(PORTAL_TIMEOUT_MS);
    authHttp.setTimeout(PORTAL_TIMEOUT_MS);
    authHttp.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    authHttp.addHeader("Content-Type", "application/x-www-form-urlencoded");
    authHttp.addHeader("User-Agent", "Mozilla/5.0");
    Serial.println("Enviando autenticacao...");
    authCode = authHttp.POST(postData);
    authErro = authHttp.errorToString(authCode);
    if (authCode > 0) authBody = authHttp.getString();
    authHttp.end();
  } else {
    WiFiClient authClient;
    HTTPClient authHttp;
    if (!authHttp.begin(authClient, postURL)) {
      Serial.println("ERRO: nao foi possivel conectar ao portal.");
      return false;
    }
    authHttp.setConnectTimeout(PORTAL_TIMEOUT_MS);
    authHttp.setTimeout(PORTAL_TIMEOUT_MS);
    authHttp.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    authHttp.addHeader("Content-Type", "application/x-www-form-urlencoded");
    authHttp.addHeader("User-Agent", "Mozilla/5.0");
    Serial.println("Enviando autenticacao...");
    authCode = authHttp.POST(postData);
    authErro = authHttp.errorToString(authCode);
    if (authCode > 0) authBody = authHttp.getString();
    authHttp.end();
  }

  Serial.printf("[Portal] Resposta do POST: %d (%s)\n", authCode, authErro.c_str());
  if (authBody.length() > 0) {
    Serial.println("[Portal] Corpo da resposta: " + authBody.substring(0, 300));
  }

  if (authCode <= 0) {
    Serial.println("ERRO: POST de autenticacao nao chegou ao portal.");
    return false;
  }

  // 301/302/303 e' o sucesso classico, mas alguns portais respondem 200.
  // Quem confirma de verdade e' o teste de internet abaixo.
  if (authCode != 301 && authCode != 302 && authCode != 303 && authCode != 200) {
    Serial.println("Resposta inesperada do portal (seguindo para confirmar internet).");
  }

  // ---- 3. Confirma que a internet foi liberada ----

  delay(1500);

  bool ok = temInternet();

  Serial.println(ok ? "PORTAL AUTENTICADO! INTERNET LIBERADA" : "ATENCAO: autenticacao nao confirmada.");

  return ok;
}

// ============================================================
//                     REDE (NÃO BLOQUEANTE)
// ============================================================

// Lista todos os APs com o SSID da UESC visiveis daqui, do mais forte ao
// mais fraco. Se existir outro BSSID bem mais forte que o D4:76:A0:8C:EB:7A
// atual, o problema e' o ESP estar preso no AP errado/longe.
void escanearAPs() {
  Serial.printf("[WiFi-scan] Escaneando redes (procurando '%s')...\n", WIFI_SSID);
  int n = WiFi.scanNetworks(false, false);
  if (n <= 0) {
    Serial.println("[WiFi-scan] Nenhuma rede encontrada.");
    return;
  }
  int achados = 0;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != WIFI_SSID) continue;
    achados++;
    Serial.printf("[WiFi-scan] %s | RSSI=%d dBm | BSSID=%s | canal=%d\n",
                  WiFi.SSID(i).c_str(), (int)WiFi.RSSI(i),
                  WiFi.BSSIDstr(i).c_str(), (int)WiFi.channel(i));
  }
  if (achados == 0) Serial.printf("[WiFi-scan] Nenhum AP '%s' visivel daqui.\n", WIFI_SSID);
  WiFi.scanDelete();
}

void iniciarWiFi() {
  Serial.println("Conectando ao Wi-Fi (em segundo plano)...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  wifiConnecting = true;
  wifiStartMs = millis();
}

void networkTick() {
  unsigned long now = millis();

  // ---- Sem Wi-Fi ----
  if (WiFi.status() != WL_CONNECTED) {
    internetOK = false;

    if (wifiConnecting) {
      if (now - wifiStartMs > WIFI_RETRY_MS) {
        Serial.println("Timeout no Wi-Fi. Tentando novamente...");
        WiFi.disconnect();
        iniciarWiFi();
      }
    } else {
      // Estava conectado e caiu: espera 5 s antes de forcar WiFi.begin().
      // Quedas curtas o proprio setAutoReconnect(true) resolve; chamar
      // begin() na hora so' atrapalha e gerava o loop de reconexao do log.
      // O MOTIVO da queda aparece na linha "[WiFi-event] Desconectado".
      if (wifiDownSinceMs == 0) {
        wifiDownSinceMs = now;
        Serial.println("Wi-Fi caiu. Aguardando 5 s (auto-reconnect)...");
      } else if (now - wifiDownSinceMs > 5000) {
        wifiDownSinceMs = 0;
        iniciarWiFi();
      }
    }

    return;
  }

  // ---- Com Wi-Fi ----
  if (wifiConnecting || wifiConnectedAtMs == 0) {
    wifiConnecting = false;
    wifiDownSinceMs = 0;
    wifiConnectedAtMs = now;

    Serial.println("Wi-Fi conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    // RSSI/BSSID/canal sao decisivos p/ interpretar o reason=34 (LOW_ACK):
    // RSSI >= -65 otimo | -70 a -80 fraco | < -80 o enlace cai com trafego.
    // Anote o BSSID: com varios APs "Uesc2", o ESP pode estar preso num longe.
    Serial.printf("[WiFi] RSSI=%d dBm | BSSID=%s | canal=%d\n",
                  (int)WiFi.RSSI(), WiFi.BSSIDstr().c_str(), (int)WiFi.channel());

    lastNetCheckMs = 0;  // força checagem de internet imediata
  }

  // Qualidade do enlace a cada 15 s (enquanto conectado)
  {
    static unsigned long lastRssiLogMs = 0;
    if (now - wifiConnectedAtMs >= 3000 && now - lastRssiLogMs >= 15000) {
      lastRssiLogMs = now;
      Serial.printf("[WiFi] RSSI=%d dBm | BSSID=%s | canal=%d | uptime Wi-Fi=%lu s\n",
                    (int)WiFi.RSSI(), WiFi.BSSIDstr().c_str(), (int)WiFi.channel(),
                    (unsigned long)((now - wifiConnectedAtMs) / 1000));
    }
  }

  // DHCP pode anunciar WL_CONNECTED antes de IP/gateway validos (era o
  // "Gateway: 0.0.0.0" 1 s apos conectar). Espera estabilizar.
  if (now - wifiConnectedAtMs < 3000) return;
  if (WiFi.localIP() == IPAddress(0, 0, 0, 0) || WiFi.gatewayIP() == IPAddress(0, 0, 0, 0)) {
    return;
  }

  if (!mdnsStarted) {
    if (MDNS.begin("ac-controller")) {
      mdnsStarted = true;
      Serial.println("mDNS: http://ac-controller.local");
    }
  }

  if (!ntpConfigured) {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    ntpConfigured = true;
    Serial.println("NTP configurado (sincroniza em segundo plano).");
  }

  // ---- Checagem periódica de internet / reautenticação ----
  unsigned long interval = internetOK ? NET_CHECK_OK_MS : NET_CHECK_FAIL_MS;

  if (lastNetCheckMs != 0 && now - lastNetCheckMs < interval) {
    return;
  }

  lastNetCheckMs = now;

  if (temInternet()) {
    internetOK = true;
    return;
  }

  Serial.println("Sem internet. Tentando autenticar no portal...");

  internetOK = verificarPortal();
}

// Task de rede no nucleo 0 (o loop/Arduino roda no nucleo 1).
// verificarPortal() BLOQUEIA por dezenas de segundos (varios GETs com
// timeout). Rodando aqui, o server.handleClient() do loop() nunca para:
// era esse bloqueio que fazia a interface "abrir e fechar" no navegador.
void networkTask(void* param) {
  (void)param;
  for (;;) {
    networkTick();
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

// ============================================================
//                     API ROUTES
// ============================================================

void setupRoutes() {
  const char* headerKeys[] = { "X-API-Key" };
  server.collectHeaders(headerKeys, 1);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);

  server.on("/power/on", HTTP_POST, handlePowerOn);
  server.on("/power/off", HTTP_POST, handlePowerOff);
  server.on("/power/toggle", HTTP_POST, handlePowerToggle);

  server.on("/schedules", HTTP_GET, handleGetSchedules);
  server.on("/schedule", HTTP_POST, handleCreateSchedule);
  server.on("/schedule", HTTP_DELETE, handleDeleteSchedule);

  server.onNotFound([]() {
    server.send(404, "application/json", "{\"error\":\"not found\"}");
  });

  server.begin();

  Serial.println("Servidor HTTP iniciado.");
}

// ============================================================
//                     SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("==============================");
  Serial.println("      AC CONTROLLER ESP32");
  Serial.println("==============================");

  // IR (TCL112AC)
  acTcl.begin();
  Serial.println("Emissor IR inicializado (TCL112AC, pino " + String(IR_LED_PIN) + ").");

  // Memória
  loadSchedules();

  // Wi-Fi: configura, mas NÃO espera conectar
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // reduz latência das respostas HTTP
  // Com RSSI em -81/-84 dBm (limite do radio), baixar a potencia so' piora:
  // o AP mal ouve o ESP e os ACKs se perdem (reason=34). Usa o maximo.
  // Pre-requisito: fonte decente (carregador 5 V/1 A+), nao USB de PC.
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  Serial.printf("Potencia TX Wi-Fi: %.1f dBm\n", WiFi.getTxPower());

  // Registra o MOTIVO de cada queda (reason): sem isso, status=6 nao diz nada.
  // 2=AUTH_EXPIRE 3=AUTH_LEAVE 4=ASSOC_EXPIRE 6=NOT_AUTHED 7=NOT_ASSOCED
  // 8=ASSOC_LEAVE (AP expulsou / nosso WiFi.disconnect()) 15=4WAY_TIMEOUT
  // 34=DISASSOC_LOW_ACK (AP nao confirma os quadros: sinal fraco, AP longe,
  //    antena obstruida ou fonte caindo no TX) 200=BEACON_TIMEOUT
  // 201=NO_AP_FOUND 202=AUTH_FAIL 204=HANDSHAKE_TIMEOUT 205=CONNECTION_FAIL
  // 0/8 logo apos "Timeout no Wi-Fi" sao provocados pelo nosso disconnect().
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.printf("[WiFi-event] Desconectado. Motivo (reason) = %d\n",
                    (int)info.wifi_sta_disconnected.reason);
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      Serial.print("[WiFi-event] IP obtido: ");
      Serial.println(WiFi.localIP());
    }
  });

  escanearAPs();  // diagnostico: quais APs Uesc2 existem e com que forca
  iniciarWiFi();

  // Servidor sobe imediatamente
  setupRoutes();

  // Rede/portal passam a rodar no nucleo 0, sem travar a interface web
  xTaskCreatePinnedToCore(networkTask, "networkTask", 16384, NULL, 1, NULL, 0);
  Serial.println("Task de rede iniciada (nucleo 0).");

  Serial.println("Sistema pronto. Rede conectando em segundo plano.");
}

// ============================================================
//                     LOOP
// ============================================================

void loop() {
  // Requisições HTTP
  server.handleClient();

  // Envia IR pendente (depois que o HTTP já respondeu)
  processarIRPendente();

  // Agendamentos
  static unsigned long lastScheduleCheck = 0;

  if (millis() - lastScheduleCheck >= 1000) {
    lastScheduleCheck = millis();
    checkSchedules();
  }

  // Wi-Fi / portal / NTP / mDNS: rodam na networkTask (nucleo 0).
  // NAO chamar networkTick() aqui: verificarPortal() bloqueia por
  // dezenas de segundos e congelava server.handleClient() (a tela abria
  // e "fechava" porque o /status parava de responder).

  delay(2);  // nao girar em vazio: alimenta o watchdog e economiza CPU
}
