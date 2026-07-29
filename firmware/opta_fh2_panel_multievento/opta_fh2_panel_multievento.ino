/*
  Arduino Opta AFX00002 - alarma de incendio demostrativa + FlightHub 2

  Entradas:
    I1 / A0: ARMADO. Debe permanecer ON.
    I2 / A1: INCENDIO.
    I3 / A2: INTRUSION.
    I4 / A3: EVENTO SISMICO.
    Debe permanecer activa una sola entrada de evento durante 2 segundos.

  Red:
    - Panel local HTTP en el puerto 80.
    - POST HTTPS/TLS a DJI FlightHub 2.

  Seguridad:
    - El dashboard es exclusivamente de monitoreo; no dispara eventos.
    - Los cuatro relevadores D0-D3 permanecen forzados en OFF.
    - Solo se procesa un evento por activacion.
    - I2-I4 deben liberarse antes de generar otro evento.
    - Varias entradas simultaneas bloquean el envio.
    - Cooldown de 60 segundos entre envios reales.
*/

#include <SPI.h>
#include <PortentaEthernet.h>
#include <Ethernet.h>
#include <EthernetSSLClient.h>
#include <EthernetUdp.h>
#include <mbed_rtc_time.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "arduino_secrets.h"
#include "fire_event_config.h"

const pin_size_t RELAY_PINS[4] = { RELAY1, RELAY2, RELAY3, RELAY4 };
const pin_size_t RELAY_LED_PINS[4] = { LED_D0, LED_D1, LED_D2, LED_D3 };

constexpr char FH2_HOST[] = "es-flight-api-us.djigate.com";
constexpr char FH2_PATH[] = "/openapi/v0.1/workflow";
constexpr uint16_t FH2_PORT = 443;

constexpr char NTP_HOST[] = "time.cloudflare.com";
constexpr uint16_t NTP_LOCAL_PORT = 2390;
constexpr uint16_t NTP_SERVER_PORT = 123;
constexpr uint32_t NTP_TO_UNIX = 2208988800UL;

constexpr float INPUT_FULL_SCALE_V = 10.88f;
constexpr float ADC_COUNTS = 4095.0f;
constexpr float DIGITAL_HIGH_THRESHOLD_V = 6.6f;
constexpr unsigned long TRIGGER_HOLD_MS = 2000;
constexpr unsigned long LIVE_COOLDOWN_MS = 60000;
constexpr unsigned long DRY_RUN_COOLDOWN_MS = 5000;
constexpr unsigned long NTP_TIMEOUT_MS = 5000;
constexpr unsigned long HTTP_TIMEOUT_MS = 12000;
constexpr unsigned long WEB_CLIENT_TIMEOUT_MS = 700;
constexpr unsigned long SERIAL_STATUS_PERIOD_MS = 2000;

constexpr char AMAZON_ROOT_CA_1[] = R"CERT(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)CERT";

enum EventPhase : uint8_t {
  PHASE_READY,
  PHASE_ARMED,
  PHASE_CONFIRMING,
  PHASE_GENERATED,
  PHASE_SENDING,
  PHASE_SENT,
  PHASE_ERROR,
  PHASE_COOLDOWN
};

struct AlarmEvent {
  pin_size_t pin;
  uint8_t inputNumber;
  uint8_t level;
  const char *shortName;
  const char *eventName;
  const char *description;
  const char *latitude;
  const char *longitude;
};

const AlarmEvent ALARM_EVENTS[] = {
  {
    A1, 2, 5, "Incendio", "Alarma de incendio - Arduino Opta",
    "Alarma de incendio demostrativa generada por pulsador fisico",
    I2_EVENT_LATITUDE, I2_EVENT_LONGITUDE
  },
  {
    A2, 3, 3, "Intrusion", "Alarma de intrusion - Arduino Opta",
    "Evento de intrusion generado por pulsador fisico",
    I3_EVENT_LATITUDE, I3_EVENT_LONGITUDE
  },
  {
    A3, 4, 4, "Evento sismico", "Evento sismico - Arduino Opta",
    "Evento sismico generado por pulsador fisico",
    I4_EVENT_LATITUDE, I4_EVENT_LONGITUDE
  }
};
constexpr uint8_t ALARM_EVENT_COUNT =
  sizeof(ALARM_EVENTS) / sizeof(ALARM_EVENTS[0]);

EthernetServer dashboardServer(80);

bool ethernetReady = false;
bool clockSynchronized = false;
bool triggerTiming = false;
bool triggerConsumed = false;
bool eventGenerated = false;
bool eventAccepted = false;
int8_t activeEventIndex = -1;
int8_t lastEventIndex = -1;
unsigned long triggerStartedMs = 0;
unsigned long lastActionMs = 0;
unsigned long lastSerialStatusMs = 0;
int lastHttpStatus = 0;
uint32_t eventCounter = 0;
uint32_t eventCounters[ALARM_EVENT_COUNT] = { 0 };
EventPhase eventPhase = PHASE_READY;
char lastEventUtc[32] = "Aun no se generan eventos";
char lastResult[96] = "Sistema listo. Esperando armado.";

const char DASHBOARD_PAGE[] = R"HTML(
<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Panel multievento Opta</title>
  <style>
    :root{color-scheme:dark;--bg:#080c12;--panel:#111923;--line:#273444;
      --text:#f3f6fa;--muted:#91a0b2;--green:#28d17c;--amber:#ffb020;
      --red:#ff455d;--blue:#38bdf8}
    *{box-sizing:border-box}
    body{margin:0;background:radial-gradient(circle at 50% -20%,#202b38 0,
      var(--bg) 45%);color:var(--text);font-family:Arial,Helvetica,sans-serif}
    main{max-width:980px;margin:auto;padding:28px 18px 42px}
    header{display:flex;align-items:center;justify-content:space-between;
      gap:18px;margin-bottom:18px}
    h1{font-size:clamp(1.55rem,4vw,2.45rem);margin:0;letter-spacing:-.03em}
    h1 small{display:block;color:var(--red);font-size:.75rem;letter-spacing:.16em;
      margin-bottom:7px}
    .connection{display:flex;align-items:center;gap:8px;color:var(--muted);
      white-space:nowrap}.dot{width:10px;height:10px;border-radius:50%;
      background:var(--green);box-shadow:0 0 14px var(--green)}
    .dot.off{background:var(--red);box-shadow:0 0 14px var(--red)}
    .alarm{background:linear-gradient(110deg,#171f29,#111923);border:1px solid
      var(--line);border-left:6px solid var(--green);border-radius:15px;
      padding:18px 20px;display:flex;align-items:center;gap:16px;
      transition:.2s;margin-bottom:16px}.alarm.fire{border-left-color:var(--red);
      box-shadow:0 0 30px #ff455d1f}.alarm.error{border-left-color:var(--amber)}
    .icon{width:54px;height:54px;border-radius:50%;display:grid;place-items:center;
      background:#20302b;font-size:1.65rem}.fire .icon{background:#421a22}
    .alarm h2{margin:0 0 4px;font-size:1.25rem}.alarm p{margin:0;color:var(--muted)}
    .grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}
    .card{background:var(--panel);border:1px solid var(--line);border-radius:15px;
      padding:18px;min-height:148px;position:relative;overflow:hidden}
    .card::after{content:"";position:absolute;inset:auto 0 0;height:3px;
      background:#334155}.card.on::after{background:var(--green)}
    .card.trigger::after{background:var(--red)}
    .card.intrusion::after{background:var(--amber)}
    .card.seismic::after{background:var(--blue)}.label{color:var(--muted);
      text-transform:uppercase;letter-spacing:.11em;font-size:.72rem;font-weight:bold}
    .value{font-size:2rem;font-weight:800;margin:13px 0 4px}.voltage{color:var(--muted)}
    .sequence{margin-top:14px;background:var(--panel);border:1px solid var(--line);
      border-radius:15px;padding:20px}.sequence h2{font-size:1rem;margin:0 0 18px}
    .steps{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}
    .step{border-top:3px solid #334155;padding-top:12px;color:var(--muted)}
    .step.done{border-color:var(--green);color:var(--text)}
    .step.active{border-color:var(--amber);color:var(--text)}
    .step.failed{border-color:var(--red);color:var(--text)}
    .step b{display:block;margin-bottom:5px}.number{display:inline-grid;place-items:center;
      width:24px;height:24px;border-radius:50%;background:#263445;margin-right:5px}
    .details{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-top:14px}
    .detail{background:#0c121a;border:1px solid var(--line);border-radius:11px;padding:13px}
    .detail span{display:block;color:var(--muted);font-size:.75rem;margin-bottom:5px}
    .detail strong{overflow-wrap:anywhere}
    .notice{color:var(--muted);font-size:.82rem;line-height:1.5;margin:15px 3px 0}
    @media(max-width:650px){header{align-items:flex-start;flex-direction:column}
      .grid,.details{grid-template-columns:1fr}.steps{grid-template-columns:1fr 1fr}}
  </style>
</head>
<body>
<main>
  <header>
    <h1><small>DEMO INDUSTRIAL</small>Panel multievento FlightHub 2</h1>
    <div class="connection"><span id="netDot" class="dot"></span>
      <span id="netText">Opta conectado</span></div>
  </header>

  <section id="alarmBanner" class="alarm">
    <div class="icon">&#128737;</div>
    <div><h2 id="alarmTitle">Sistema en espera</h2>
      <p id="alarmText">Active I1 para armar la estación.</p></div>
  </section>

  <div class="grid">
    <section id="i1Card" class="card">
      <div class="label">Entrada física I1</div>
      <div id="i1Value" class="value">OFF</div>
      <div class="voltage"><span id="i1Voltage">0.00</span> V · ARMADO</div>
    </section>
    <section id="i2Card" class="card">
      <div class="label">Entrada física I2</div>
      <div id="i2Value" class="value">OFF</div>
      <div class="voltage"><span id="i2Voltage">0.00</span> V · INCENDIO</div>
    </section>
    <section id="i3Card" class="card">
      <div class="label">Entrada física I3</div>
      <div id="i3Value" class="value">OFF</div>
      <div class="voltage"><span id="i3Voltage">0.00</span> V · INTRUSIÓN</div>
    </section>
    <section id="i4Card" class="card">
      <div class="label">Entrada física I4</div>
      <div id="i4Value" class="value">OFF</div>
      <div class="voltage"><span id="i4Voltage">0.00</span> V · EVENTO SÍSMICO</div>
    </section>
  </div>

  <section class="sequence">
    <h2>Secuencia del evento</h2>
    <div class="steps">
      <div id="stepArm" class="step"><b><span class="number">1</span>Armado</b>I1 habilita</div>
      <div id="stepTrigger" class="step"><b><span class="number">2</span>Alarma</b>I2, I3 o I4 durante 2 s</div>
      <div id="stepEvent" class="step"><b><span class="number">3</span>Evento</b>Solicitud generada</div>
      <div id="stepFh2" class="step"><b><span class="number">4</span>FlightHub 2</b>Confirmación HTTP</div>
    </div>
    <div class="details">
      <div class="detail"><span>ÚLTIMO RESULTADO</span><strong id="result">Sin eventos</strong></div>
      <div class="detail"><span>CÓDIGO HTTP</span><strong id="httpStatus">—</strong></div>
      <div class="detail"><span>EVENTOS GENERADOS</span><strong id="eventCount">0</strong></div>
      <div class="detail"><span>TIPO DE EVENTO</span><strong id="eventName">Sin eventos</strong></div>
      <div class="detail"><span>COORDENADAS ENVIADAS</span><strong id="coordinates">—</strong></div>
      <div class="detail"><span>CONTEO: I2 / I3 / I4</span><strong id="eventBreakdown">0 / 0 / 0</strong></div>
      <div class="detail"><span>ÚLTIMO EVENTO UTC</span><strong id="eventTime">—</strong></div>
      <div class="detail"><span>COOLDOWN</span><strong id="cooldown">Disponible</strong></div>
      <div class="detail"><span>MODO</span><strong id="mode">ENVÍO REAL</strong></div>
    </div>
  </section>
  <p class="notice">Panel de demostración y monitoreo. La interfaz web no puede
    generar eventos: el disparo requiere I1 ON y solamente una entrada entre
    I2, I3 o I4 durante dos segundos. No sustituye sistemas certificados.</p>
</main>
<script>
const $=id=>document.getElementById(id);
const step=(id,state)=>$(id).className='step '+state;
function paint(s){
  $('i1Value').textContent=s.i1?'ON':'OFF';
  $('i2Value').textContent=s.i2?'ON':'OFF';
  $('i3Value').textContent=s.i3?'ON':'OFF';
  $('i4Value').textContent=s.i4?'ON':'OFF';
  $('i1Voltage').textContent=s.i1Voltage.toFixed(2);
  $('i2Voltage').textContent=s.i2Voltage.toFixed(2);
  $('i3Voltage').textContent=s.i3Voltage.toFixed(2);
  $('i4Voltage').textContent=s.i4Voltage.toFixed(2);
  $('i1Card').className='card '+(s.i1?'on':'');
  $('i2Card').className='card '+(s.i2?'trigger':'');
  $('i3Card').className='card '+(s.i3?'intrusion':'');
  $('i4Card').className='card '+(s.i4?'seismic':'');
  $('result').textContent=s.result;
  $('httpStatus').textContent=s.httpStatus||'—';
  $('eventCount').textContent=s.eventCount;
  $('eventName').textContent=s.eventName;
  $('coordinates').textContent=s.eventLatitude==='-'?'—':
    s.eventLatitude+', '+s.eventLongitude;
  $('eventBreakdown').textContent=s.fireCount+' / '+s.intrusionCount+
    ' / '+s.seismicCount;
  $('eventTime').textContent=s.eventTime;
  $('cooldown').textContent=s.cooldownSeconds>0?s.cooldownSeconds+' s':'Disponible';
  $('mode').textContent=s.liveMode?'ENVÍO REAL':'DRY RUN';
  step('stepArm',s.i1?'done':'');
  const anyTrigger=s.i2||s.i3||s.i4;
  step('stepTrigger',s.phase==='CONFIRMANDO'?'active':(anyTrigger?'done':''));
  step('stepEvent',s.eventGenerated?'done':'');
  step('stepFh2',s.eventAccepted?'done':(s.phase==='ERROR'?'failed':
    (s.phase==='ENVIANDO'?'active':'')));
  const banner=$('alarmBanner');
  banner.className='alarm '+(s.phase==='ERROR'?'error':
    (anyTrigger||s.phase==='GENERADO'||s.phase==='ENVIANDO'?'fire':''));
  if(s.phase==='CONFIRMANDO'){
    $('alarmTitle').textContent='Evento detectado en I'+s.activeInput;
    $('alarmText').textContent='Mantenga una sola entrada durante 2 segundos.';
  }else if(s.phase==='ENVIANDO'||s.phase==='GENERADO'){
    $('alarmTitle').textContent=s.eventName+' generado';
    $('alarmText').textContent='Transmitiendo coordenadas a FlightHub 2.';
  }else if(s.phase==='ENVIADO'){
    $('alarmTitle').textContent='Evento enviado correctamente';
    $('alarmText').textContent='FlightHub 2 aceptó la solicitud.';
  }else if(s.phase==='ERROR'){
    $('alarmTitle').textContent='Fallo de comunicación';
    $('alarmText').textContent=s.result;
  }else if(s.phase==='BLOQUEO'){
    $('alarmTitle').textContent='Protección de repetición activa';
    $('alarmText').textContent='Espere a que termine el cooldown.';
  }else if(s.i1){
    $('alarmTitle').textContent='Sistema armado';
    $('alarmText').textContent='Esperando incendio, intrusión o evento sísmico.';
  }else{
    $('alarmTitle').textContent='Sistema en espera';
    $('alarmText').textContent='Active I1 para armar la estación.';
  }
  $('netDot').className='dot';$('netText').textContent='Datos en vivo';
}
async function update(){
  try{
    const r=await fetch('/api/status',{cache:'no-store'});
    if(!r.ok)throw new Error();
    paint(await r.json());
  }catch(e){
    $('netDot').className='dot off';$('netText').textContent='Sin comunicación';
  }finally{setTimeout(update,350)}
}
update();
</script>
</body></html>
)HTML";

float inputVoltage(const pin_size_t pin) {
  return analogRead(pin) * (INPUT_FULL_SCALE_V / ADC_COUNTS);
}

bool inputOn(const pin_size_t pin) {
  return inputVoltage(pin) >= DIGITAL_HIGH_THRESHOLD_V;
}

void forceRelaysOff() {
  for (uint8_t i = 0; i < 4; ++i) {
    digitalWrite(RELAY_PINS[i], LOW);
    digitalWrite(RELAY_LED_PINS[i], LOW);
  }
}

void setLastResult(const char *text) {
  strncpy(lastResult, text, sizeof(lastResult) - 1);
  lastResult[sizeof(lastResult) - 1] = '\0';
}

void saveEventUtc() {
  const time_t now = time(nullptr);
  struct tm *utc = gmtime(&now);
  if (utc == nullptr) {
    strncpy(lastEventUtc, "Hora UTC no disponible", sizeof(lastEventUtc) - 1);
    lastEventUtc[sizeof(lastEventUtc) - 1] = '\0';
    return;
  }
  strftime(lastEventUtc, sizeof(lastEventUtc), "%Y-%m-%d %H:%M:%S UTC", utc);
}

bool syncNtp() {
  IPAddress ntpIp;
  if (Ethernet.hostByName(NTP_HOST, ntpIp) != 1) {
    Serial.println("ERROR NTP: fallo DNS.");
    return false;
  }

  EthernetUDP udp;
  if (udp.begin(NTP_LOCAL_PORT) == 0) {
    Serial.println("ERROR NTP: no se pudo abrir UDP.");
    return false;
  }

  byte packet[48] = { 0 };
  packet[0] = 0b11100011;
  packet[2] = 6;
  packet[3] = 0xEC;
  packet[12] = 49;
  packet[13] = 0x4E;
  packet[14] = 49;
  packet[15] = 52;

  if (!udp.beginPacket(ntpIp, NTP_SERVER_PORT) ||
      udp.write(packet, sizeof(packet)) != sizeof(packet) ||
      !udp.endPacket()) {
    udp.stop();
    Serial.println("ERROR NTP: no se pudo enviar la solicitud.");
    return false;
  }

  const unsigned long started = millis();
  int packetSize = 0;
  while (millis() - started < NTP_TIMEOUT_MS) {
    packetSize = udp.parsePacket();
    if (packetSize >= 48) break;
    forceRelaysOff();
    delay(10);
  }

  if (packetSize < 48 || udp.read(packet, sizeof(packet)) < 48) {
    udp.stop();
    Serial.println("ERROR NTP: sin respuesta.");
    return false;
  }
  udp.stop();

  const uint32_t seconds1900 =
    (static_cast<uint32_t>(packet[40]) << 24) |
    (static_cast<uint32_t>(packet[41]) << 16) |
    (static_cast<uint32_t>(packet[42]) << 8) |
    static_cast<uint32_t>(packet[43]);
  if (seconds1900 <= NTP_TO_UNIX) {
    Serial.println("ERROR NTP: fecha invalida.");
    return false;
  }

  set_time(static_cast<time_t>(seconds1900 - NTP_TO_UNIX));
  clockSynchronized = true;
  Serial.println("NTP OK");
  return true;
}

bool credentialsConfigured() {
  return strlen(FH2_USER_TOKEN) >= 20 &&
         strlen(FH2_PROJECT_UUID) >= 20 &&
         strcmp(FH2_USER_TOKEN, "PEGA_AQUI_TU_X_USER_TOKEN") != 0 &&
         strcmp(FH2_PROJECT_UUID, "PEGA_AQUI_TU_X_PROJECT_UUID") != 0;
}

bool buildJson(const AlarmEvent &event, char *json, const size_t capacity) {
  const int written = snprintf(
    json,
    capacity,
    "{\"workflow_uuid\":\"%s\",\"trigger_type\":0,\"name\":\"%s\","
    "\"params\":{\"creator\":\"%s\",\"latitude\":%s,\"longitude\":%s,"
    "\"level\":%d,\"desc\":\"%s\"}}",
    WORKFLOW_UUID,
    event.eventName,
    CREATOR_ID,
    event.latitude,
    event.longitude,
    event.level,
    event.description
  );
  return written > 0 && static_cast<size_t>(written) < capacity;
}

int readHttpResponse(EthernetSSLClient &client) {
  char statusLine[128] = { 0 };
  size_t used = 0;
  const unsigned long started = millis();

  while (!client.available() && client.connected() &&
         millis() - started < HTTP_TIMEOUT_MS) {
    forceRelaysOff();
    delay(5);
  }

  while (client.available() && used < sizeof(statusLine) - 1) {
    const char c = client.read();
    if (c == '\n') break;
    if (c != '\r') statusLine[used++] = c;
  }
  statusLine[used] = '\0';
  Serial.print("Estado FH2: ");
  Serial.println(statusLine[0] ? statusLine : "sin respuesta HTTP");

  int status = 0;
  sscanf(statusLine, "HTTP/%*s %d", &status);

  size_t discarded = 0;
  const unsigned long responseStarted = millis();
  while ((client.connected() || client.available()) &&
         millis() - responseStarted < 3000 && discarded < 2048) {
    while (client.available() && discarded < 2048) {
      client.read();
      ++discarded;
    }
    forceRelaysOff();
  }
  client.stop();
  return status;
}

void executeEvent(const int8_t eventIndex) {
  const AlarmEvent &event = ALARM_EVENTS[eventIndex];
  ++eventCounter;
  ++eventCounters[eventIndex];
  lastEventIndex = eventIndex;
  eventGenerated = true;
  eventAccepted = false;
  lastHttpStatus = 0;
  eventPhase = PHASE_GENERATED;
  saveEventUtc();
  snprintf(lastResult, sizeof(lastResult), "%s generado.", event.shortName);

  char json[512];
  if (!buildJson(event, json, sizeof(json))) {
    eventPhase = PHASE_ERROR;
    setLastResult("Error: buffer JSON insuficiente.");
    return;
  }

  Serial.println();
  Serial.print(event.shortName);
  Serial.print(" #");
  Serial.println(eventCounter);
  Serial.println(json);

  if (!FH2_LIVE_SEND_ENABLED) {
    eventPhase = PHASE_SENT;
    snprintf(lastResult, sizeof(lastResult),
             "DRY RUN: %s generado, no transmitido.", event.shortName);
    return;
  }
  if (!credentialsConfigured()) {
    eventPhase = PHASE_ERROR;
    setLastResult("Envio bloqueado: faltan credenciales.");
    return;
  }
  if (!clockSynchronized && !syncNtp()) {
    eventPhase = PHASE_ERROR;
    setLastResult("Envio bloqueado: reloj UTC no confiable.");
    return;
  }

  eventPhase = PHASE_SENDING;
  snprintf(lastResult, sizeof(lastResult),
           "Enviando %s a FlightHub 2...", event.shortName);

  EthernetSSLClient client;
  client.setCACert(AMAZON_ROOT_CA_1);
  if (!client.connect(FH2_HOST, FH2_PORT)) {
    eventPhase = PHASE_ERROR;
    setLastResult("No se pudo establecer la conexion TLS.");
    return;
  }

  client.print("POST ");
  client.print(FH2_PATH);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(FH2_HOST);
  client.println("User-Agent: Arduino-Opta-Fire-Dashboard/1.0");
  client.println("Content-Type: application/json");
  client.print("X-User-Token: ");
  client.println(FH2_USER_TOKEN);
  client.print("x-project-uuid: ");
  client.println(FH2_PROJECT_UUID);
  client.print("Content-Length: ");
  client.println(strlen(json));
  client.println("Connection: close");
  client.println();
  client.print(json);

  lastHttpStatus = readHttpResponse(client);
  if (lastHttpStatus >= 200 && lastHttpStatus < 300) {
    eventAccepted = true;
    eventPhase = PHASE_SENT;
    snprintf(lastResult, sizeof(lastResult),
             "%s aceptado por FlightHub 2.", event.shortName);
    Serial.println("EVENTO ACEPTADO POR FLIGHTHUB 2.");
  } else {
    eventPhase = PHASE_ERROR;
    setLastResult("FlightHub 2 no acepto el evento.");
    Serial.println("EVENTO NO ACEPTADO POR FLIGHTHUB 2.");
  }
}

unsigned long cooldownRemainingSeconds() {
  if (lastActionMs == 0) return 0;
  const unsigned long cooldown = FH2_LIVE_SEND_ENABLED
    ? LIVE_COOLDOWN_MS
    : DRY_RUN_COOLDOWN_MS;
  const unsigned long elapsed = millis() - lastActionMs;
  if (elapsed >= cooldown) return 0;
  return (cooldown - elapsed + 999) / 1000;
}

const char *phaseText() {
  switch (eventPhase) {
    case PHASE_ARMED: return "ARMADO";
    case PHASE_CONFIRMING: return "CONFIRMANDO";
    case PHASE_GENERATED: return "GENERADO";
    case PHASE_SENDING: return "ENVIANDO";
    case PHASE_SENT: return "ENVIADO";
    case PHASE_ERROR: return "ERROR";
    case PHASE_COOLDOWN: return "BLOQUEO";
    default: return "LISTO";
  }
}

uint8_t activeAlarmCount(int8_t &selectedEventIndex) {
  uint8_t count = 0;
  selectedEventIndex = -1;
  for (uint8_t i = 0; i < ALARM_EVENT_COUNT; ++i) {
    if (inputOn(ALARM_EVENTS[i].pin)) {
      ++count;
      selectedEventIndex = static_cast<int8_t>(i);
    }
  }
  return count;
}

void updatePhysicalInputs() {
  const bool armed = inputOn(A0);
  int8_t selectedEventIndex = -1;
  const uint8_t activeCount = activeAlarmCount(selectedEventIndex);
  const unsigned long now = millis();

  if (activeCount == 0) {
    if (triggerTiming || triggerConsumed) {
      Serial.println("Entradas I2-I4 liberadas. Listo para una nueva alarma.");
    }
    triggerTiming = false;
    triggerConsumed = false;
    activeEventIndex = -1;

    if (cooldownRemainingSeconds() > 0) {
      if (eventPhase != PHASE_SENT && eventPhase != PHASE_ERROR) {
        eventPhase = PHASE_COOLDOWN;
      }
    } else {
      eventPhase = armed ? PHASE_ARMED : PHASE_READY;
    }
    return;
  }

  if (activeCount > 1) {
    triggerTiming = false;
    if (!triggerConsumed) {
      eventPhase = PHASE_ERROR;
      setLastResult("Bloqueado: active solamente una entrada de alarma.");
      Serial.println("BLOQUEADO: hay varias entradas I2-I4 activas.");
      triggerConsumed = true;
    }
    return;
  }

  const AlarmEvent &event = ALARM_EVENTS[selectedEventIndex];

  if (!armed) {
    if (!triggerConsumed) {
      snprintf(lastResult, sizeof(lastResult),
               "I%u ignorado: active primero I1.", event.inputNumber);
      Serial.println("Alarma ignorada: I1 debe estar ON.");
      triggerConsumed = true;
    }
    return;
  }

  if (cooldownRemainingSeconds() > 0) {
    if (!triggerConsumed) {
      eventPhase = PHASE_COOLDOWN;
      setLastResult("Cooldown activo. Evento bloqueado.");
      triggerConsumed = true;
    }
    return;
  }

  if (triggerTiming && activeEventIndex != selectedEventIndex) {
    triggerTiming = false;
    triggerConsumed = true;
    eventPhase = PHASE_ERROR;
    setLastResult("Cambio de entrada detectado. Libere I2-I4.");
    return;
  }

  if (!triggerTiming && !triggerConsumed) {
    triggerTiming = true;
    activeEventIndex = selectedEventIndex;
    triggerStartedMs = now;
    eventPhase = PHASE_CONFIRMING;
    snprintf(lastResult, sizeof(lastResult),
             "%s detectado. Confirmando durante 2 segundos.",
             event.shortName);
    Serial.print("I1 ARM + I");
    Serial.print(event.inputNumber);
    Serial.print(" ");
    Serial.print(event.shortName);
    Serial.println(". Mantener durante 2 segundos...");
  }

  if (triggerTiming && now - triggerStartedMs >= TRIGGER_HOLD_MS) {
    triggerTiming = false;
    triggerConsumed = true;
    lastActionMs = now;
    executeEvent(selectedEventIndex);
  }
}

void sendNoContent(EthernetClient &client) {
  client.println("HTTP/1.1 204 No Content");
  client.println("Cache-Control: no-store");
  client.println("Connection: close");
  client.println();
}

void sendDashboard(EthernetClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=utf-8");
  client.println("Cache-Control: no-store");
  client.println("Connection: close");
  client.println();
  client.print(DASHBOARD_PAGE);
}

void sendJsonStatus(EthernetClient &client) {
  const float i1Voltage = inputVoltage(A0);
  const float i2Voltage = inputVoltage(A1);
  const float i3Voltage = inputVoltage(A2);
  const float i4Voltage = inputVoltage(A3);
  const AlarmEvent *lastEvent =
    lastEventIndex >= 0 ? &ALARM_EVENTS[lastEventIndex] : nullptr;

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Cache-Control: no-store");
  client.println("Connection: close");
  client.println();
  client.print("{\"i1\":");
  client.print(i1Voltage >= DIGITAL_HIGH_THRESHOLD_V ? "true" : "false");
  client.print(",\"i2\":");
  client.print(i2Voltage >= DIGITAL_HIGH_THRESHOLD_V ? "true" : "false");
  client.print(",\"i3\":");
  client.print(i3Voltage >= DIGITAL_HIGH_THRESHOLD_V ? "true" : "false");
  client.print(",\"i4\":");
  client.print(i4Voltage >= DIGITAL_HIGH_THRESHOLD_V ? "true" : "false");
  client.print(",\"i1Voltage\":");
  client.print(i1Voltage, 2);
  client.print(",\"i2Voltage\":");
  client.print(i2Voltage, 2);
  client.print(",\"i3Voltage\":");
  client.print(i3Voltage, 2);
  client.print(",\"i4Voltage\":");
  client.print(i4Voltage, 2);
  client.print(",\"activeInput\":");
  client.print(activeEventIndex >= 0
                 ? ALARM_EVENTS[activeEventIndex].inputNumber
                 : 0);
  client.print(",\"phase\":\"");
  client.print(phaseText());
  client.print("\",\"eventGenerated\":");
  client.print(eventGenerated ? "true" : "false");
  client.print(",\"eventAccepted\":");
  client.print(eventAccepted ? "true" : "false");
  client.print(",\"httpStatus\":");
  client.print(lastHttpStatus);
  client.print(",\"eventCount\":");
  client.print(eventCounter);
  client.print(",\"fireCount\":");
  client.print(eventCounters[0]);
  client.print(",\"intrusionCount\":");
  client.print(eventCounters[1]);
  client.print(",\"seismicCount\":");
  client.print(eventCounters[2]);
  client.print(",\"eventName\":\"");
  client.print(lastEvent != nullptr ? lastEvent->shortName : "Sin eventos");
  client.print("\",\"eventLatitude\":\"");
  client.print(lastEvent != nullptr ? lastEvent->latitude : "-");
  client.print("\",\"eventLongitude\":\"");
  client.print(lastEvent != nullptr ? lastEvent->longitude : "-");
  client.print("\",\"eventTime\":\"");
  client.print(lastEventUtc);
  client.print("\",\"cooldownSeconds\":");
  client.print(cooldownRemainingSeconds());
  client.print(",\"liveMode\":");
  client.print(FH2_LIVE_SEND_ENABLED ? "true" : "false");
  client.print(",\"result\":\"");
  client.print(lastResult);
  client.println("\"}");
}

void serviceDashboard() {
  EthernetClient client = dashboardServer.accept();
  if (!client) return;

  char requestLine[112] = { 0 };
  size_t requestLength = 0;
  bool firstLine = true;
  bool currentLineBlank = true;
  const unsigned long connectedAt = millis();

  while (client.connected() &&
         millis() - connectedAt < WEB_CLIENT_TIMEOUT_MS) {
    forceRelaysOff();

    while (client.available()) {
      const char c = client.read();

      if (firstLine && c != '\r' && c != '\n' &&
          requestLength < sizeof(requestLine) - 1) {
        requestLine[requestLength++] = c;
        requestLine[requestLength] = '\0';
      }
      if (c == '\n' && firstLine) firstLine = false;

      if (c == '\n' && currentLineBlank) {
        if (strstr(requestLine, "GET /api/status ") != nullptr) {
          sendJsonStatus(client);
        } else if (strstr(requestLine, "GET /favicon.ico ") != nullptr) {
          sendNoContent(client);
        } else {
          sendDashboard(client);
        }
        delay(1);
        client.stop();
        return;
      }

      if (c == '\n') {
        currentLineBlank = true;
      } else if (c != '\r') {
        currentLineBlank = false;
      }
    }
  }
  client.stop();
}

void printStatus() {
  const unsigned long now = millis();
  if (now - lastSerialStatusMs < SERIAL_STATUS_PERIOD_MS) return;
  lastSerialStatusMs = now;

  Serial.print("I1 ARM: ");
  Serial.print(inputOn(A0) ? "ON" : "OFF");
  Serial.print(" | I2 ALARMA: ");
  Serial.print(inputOn(A1) ? "ON" : "OFF");
  Serial.print(" | I3 INTRUSION: ");
  Serial.print(inputOn(A2) ? "ON" : "OFF");
  Serial.print(" | I4 SISMO: ");
  Serial.print(inputOn(A3) ? "ON" : "OFF");
  Serial.print(" | Fase: ");
  Serial.print(phaseText());
  Serial.print(" | HTTP: ");
  Serial.print(lastHttpStatus);
  Serial.print(" | Eventos: ");
  Serial.println(eventCounter);
}

void setup() {
  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(RELAY_PINS[i], OUTPUT);
    pinMode(RELAY_LED_PINS[i], OUTPUT);
  }
  forceRelaysOff();
  analogReadResolution(12);

  Serial.begin(115200);
  const unsigned long serialStarted = millis();
  while (!Serial && millis() - serialStarted < 3000) { }

  Serial.println();
  Serial.println("Arduino Opta - Panel multievento + FH2");
  Serial.println("I1=ARMADO; I2=INCENDIO; I3=INTRUSION; I4=SISMO.");
  Serial.println("Mantener una sola entrada de evento durante 2 segundos.");
  Serial.println(FH2_LIVE_SEND_ENABLED
                   ? "ATENCION: ENVIO REAL HABILITADO"
                   : "MODO DRY RUN");

  if (Ethernet.begin() == 0) {
    setLastResult("Sin direccion IP por DHCP.");
    Serial.println("ERROR DHCP. Revisa Ethernet y reinicia.");
    return;
  }
  ethernetReady = true;
  dashboardServer.begin();

  Serial.print("Dashboard: http://");
  Serial.println(Ethernet.localIP());
  syncNtp();
}

void loop() {
  forceRelaysOff();
  updatePhysicalInputs();
  serviceDashboard();
  printStatus();
  if (ethernetReady) Ethernet.maintain();
}
