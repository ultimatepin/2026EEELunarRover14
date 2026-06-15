#include "AppConfig.h"
#include "Network.h"
#include "RoverState.h"
#include "WebRoutes.h"

/*
  WebRoutes.cpp
  =============
  Strict raw WiFi HTTP router.

  This replaces the large dashboard route from the first integrated draft with a
  deliberately small diagnostic dashboard. The rover still uses the same API
  endpoints, but the page is now light enough that the WINC1500/hotspot socket
  stack is much less likely to wedge.
*/

extern WiFiServer server;

namespace
{
  unsigned long requestCount = 0;

  const char ROOT_TEXT[] =
      "EEELunarRover raw WiFi debug build\n"
      "Open /dashboard for controls.\n"
      "Test /api/ping first.\n";

  const char DASHBOARD_HTML[] = R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EEELunarRover Debug Dashboard</title>
<style>
body{font-family:system-ui,Arial;margin:20px;max-width:900px;background:#10131a;color:#e8edf7}
.card{border:1px solid #303848;border-radius:12px;padding:14px;margin:12px 0;background:#171c25}
button{font-size:16px;padding:10px 14px;margin:4px;border:1px solid #4b5568;border-radius:8px;background:#202838;color:#e8edf7}
button:active{background:#2f6feb}.grid{display:grid;grid-template-columns:repeat(3,72px);gap:4px;align-items:center}
pre{white-space:pre-wrap;background:#0b0e13;border-radius:8px;padding:12px;overflow:auto}.muted{color:#9aa6b8}
input[type=range]{width:260px}.ok{color:#4ade80}.bad{color:#fb7185}
</style>
</head>
<body>
<h1>EEELunarRover Debug Dashboard</h1>
<p class="muted">This page sends one request at a time and closes every socket. Test APIs directly if anything looks wrong.</p>

<div class="card">
  <h2>Connection</h2>
  <button onclick="ping()">Ping</button>
  <button onclick="loadSensor()">Read sensor</button>
  <button onclick="loadDebug()">Debug JSON</button>
  <span id="conn" class="muted">not tested</span>
</div>

<div class="card">
  <h2>Motor test</h2>
  <div>Speed: <b id="speedText">180</b></div>
  <input id="speed" type="range" min="0" max="255" value="180" oninput="speedText.textContent=this.value">
  <div class="grid">
    <button onclick="motor('wa')">↖</button><button onclick="motor('w')">↑</button><button onclick="motor('wd')">↗</button>
    <button onclick="motor('a')">←</button><button onclick="motor('stop')">■</button><button onclick="motor('d')">→</button>
    <button onclick="motor('sa')">↙</button><button onclick="motor('s')">↓</button><button onclick="motor('sd')">↘</button>
  </div>
  <button onclick="motor('q')">Rotate CCW</button>
  <button onclick="motor('e')">Rotate CW</button>
  <button onclick="motor('stop')">STOP</button>
</div>

<div class="card">
  <h2>Sensor JSON</h2>
  <pre id="sensorOut">not loaded</pre>
</div>

<div class="card">
  <h2>Debug JSON</h2>
  <pre id="debugOut">not loaded</pre>
</div>

<script>
let busy=false,pending=null;
function api(path){return path;}
function req(path,cb){
  if(busy){pending=[path,cb];return;}
  busy=true;
  const x=new XMLHttpRequest();
  x.timeout=1500;
  x.onreadystatechange=function(){
    if(x.readyState===4){
      busy=false;
      cb(x.status,x.responseText);
      if(pending){const p=pending;pending=null;req(p[0],p[1]);}
    }
  };
  x.ontimeout=function(){busy=false;cb(0,'timeout');};
  x.open('GET',api(path));
  x.send();
}
function ping(){req('/api/ping?_t='+Date.now(),function(s,t){conn.textContent=s===200?'pong OK':'failed: '+t;conn.className=s===200?'ok':'bad';});}
function loadSensor(){req('/sensor?_t='+Date.now(),function(s,t){sensorOut.textContent=t;});}
function loadDebug(){req('/debug?_t='+Date.now(),function(s,t){debugOut.textContent=t;});}
function motor(c){const sp=document.getElementById('speed').value;req('/motor?cmd='+encodeURIComponent(c)+'&speed='+sp+'&_t='+Date.now(),function(s,t){conn.textContent='motor '+c+' -> '+t.trim();});}
setInterval(loadSensor,3000);
ping();loadSensor();loadDebug();
</script>
</body>
</html>)HTML";

  bool readHttpLine(WiFiClient &client, char *buffer, size_t bufferSize, unsigned long timeoutMs)
  {
    size_t length = 0;
    const unsigned long startMs = millis();

    while (millis() - startMs < timeoutMs)
    {
      while (client.available())
      {
        const char c = client.read();
        if (c == '\r') continue;
        if (c == '\n')
        {
          buffer[length] = '\0';
          return true;
        }
        if (length < bufferSize - 1)
        {
          buffer[length++] = c;
        }
      }

      if (!client.connected()) break;
      delay(1);
    }

    buffer[length] = '\0';
    return length > 0;
  }

  bool parseRequestLine(const char *line, char *method, size_t methodSize, char *target, size_t targetSize)
  {
    if (!line || !method || !target || methodSize == 0 || targetSize == 0) return false;
    method[0] = '\0';
    target[0] = '\0';

    const char *p = line;
    while (*p == ' ') p++;

    size_t i = 0;
    while (*p && *p != ' ' && i < methodSize - 1) method[i++] = *p++;
    method[i] = '\0';

    while (*p == ' ') p++;

    i = 0;
    while (*p && *p != ' ' && i < targetSize - 1) target[i++] = *p++;
    target[i] = '\0';

    return method[0] != '\0' && target[0] != '\0';
  }

  void discardAlreadyBufferedBytes(WiFiClient &client)
  {
    while (client.available()) client.read();
  }

  void sendHeaders(WiFiClient &client, int code, const char *status, const char *contentType, size_t contentLength)
  {
    client.print(F("HTTP/1.1 "));
    client.print(code);
    client.print(' ');
    client.println(status);
    client.print(F("Content-Type: "));
    client.println(contentType);
    client.print(F("Content-Length: "));
    client.println(contentLength);
    client.println(F("Connection: close"));
    client.println(F("Cache-Control: no-store"));
    client.println(F("Access-Control-Allow-Origin: *"));
    client.println();
  }

  void sendRawBodyInChunks(WiFiClient &client, const char *body)
  {
    const size_t CHUNK_SIZE = 64;
    size_t remaining = strlen(body);
    const char *p = body;

    while (remaining > 0)
    {
      const size_t n = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
      client.write((const uint8_t *)p, n);
      p += n;
      remaining -= n;
      delay(1);
    }
  }

  void sendText(WiFiClient &client, int code, const char *status, const char *contentType, const char *body)
  {
    sendHeaders(client, code, status, contentType, strlen(body));
    sendRawBodyInChunks(client, body);
  }

  void sendString(WiFiClient &client, int code, const char *status, const char *contentType, const String &body)
  {
    sendHeaders(client, code, status, contentType, body.length());
    client.print(body);
  }

  bool targetMatchesPath(const char *target, const char *path)
  {
    const size_t pathLen = strlen(path);
    if (strncmp(target, path, pathLen) != 0) return false;
    return target[pathLen] == '\0' || target[pathLen] == '?';
  }

  bool isExpectedGetTarget(const char *target)
  {
    return strcmp(target, "/") == 0 ||
           targetMatchesPath(target, "/dashboard") ||
           targetMatchesPath(target, "/api/ping") ||
           targetMatchesPath(target, "/api/status") ||
           targetMatchesPath(target, "/api/state") ||
           targetMatchesPath(target, "/sensor") ||
           targetMatchesPath(target, "/debug") ||
           targetMatchesPath(target, "/motor") ||
           targetMatchesPath(target, "/health") ||
           targetMatchesPath(target, "/favicon.ico");
  }

  const char *findQueryValue(const char *target, const char *name)
  {
    const char *query = strchr(target, '?');
    if (!query) return nullptr;
    query++;
    const size_t nameLen = strlen(name);

    while (*query)
    {
      if (strncmp(query, name, nameLen) == 0 && query[nameLen] == '=')
      {
        return query + nameLen + 1;
      }
      query = strchr(query, '&');
      if (!query) return nullptr;
      query++;
    }
    return nullptr;
  }

  void copyQueryValue(const char *value, char *out, size_t outSize)
  {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!value) return;

    size_t i = 0;
    while (*value && *value != '&' && i < outSize - 1)
    {
      out[i++] = *value++;
    }
    out[i] = '\0';
  }

  void handleRoot(WiFiClient &client)
  {
    requestCount++;
    sendText(client, 200, "OK", "text/plain", ROOT_TEXT);
  }

  void handleDashboard(WiFiClient &client)
  {
    requestCount++;
    sendText(client, 200, "OK", "text/html; charset=utf-8", DASHBOARD_HTML);
  }

  void handlePing(WiFiClient &client)
  {
    requestCount++;
    sendText(client, 200, "OK", "text/plain", "pong\n");
  }

  void handleStatus(WiFiClient &client)
  {
    requestCount++;
    String json = "{";
    json += "\"uptime_ms\":";
    json += millis();
    json += ",\"wifi_rssi\":";
    json += WiFi.RSSI();
    json += ",\"ip\":\"";
    json += ipAddressToString(WiFi.localIP());
    json += "\",\"request_count\":";
    json += requestCount;
    json += ",\"motor\":";
    json += roverMotorJson();
    json += "}\n";
    sendString(client, 200, "OK", "application/json", json);
  }

  void handleSensor(WiFiClient &client)
  {
    requestCount++;
    String json = roverSensorJson();
    json += '\n';
    sendString(client, 200, "OK", "application/json", json);
  }

  void handleDebug(WiFiClient &client)
  {
    requestCount++;
    String json = roverDebugJson();
    json += '\n';
    sendString(client, 200, "OK", "application/json", json);
  }

  void handleMotor(WiFiClient &client, const char *target)
  {
    requestCount++;

    char cmd[8];
    copyQueryValue(findQueryValue(target, "cmd"), cmd, sizeof(cmd));

    int speed = -1;
    const char *speedValue = findQueryValue(target, "speed");
    if (speedValue) speed = atoi(speedValue);

    char normalized[8];
    bool known = true;
    applyMotorCommand(cmd, speed, normalized, sizeof(normalized), &known);

    String body = known ? normalized : "stop";
    body += '\n';
    sendString(client, 200, "OK", "text/plain", body);
  }

  void skipUnexpectedRequest(const char *reason, const char *detail = nullptr)
  {
    Serial.print(F("Skipping unexpected request: "));
    Serial.print(reason);
    if (detail && detail[0])
    {
      Serial.print(F(" | ["));
      Serial.print(detail);
      Serial.print(F("]"));
    }
    Serial.println();
  }
}

void handleHttpClient()
{
  WiFiClient client = server.available();
  if (!client) return;

  const unsigned long requestStartMs = millis();

  char requestLine[HTTP_REQUEST_LINE_MAX];
  if (!readHttpLine(client, requestLine, sizeof(requestLine), HTTP_CLIENT_TIMEOUT_MS))
  {
    Serial.println(F("Empty/stalled client; closing."));
    client.stop();
    return;
  }

  char method[8];
  char target[HTTP_TARGET_MAX];
  if (!parseRequestLine(requestLine, method, sizeof(method), target, sizeof(target)))
  {
    skipUnexpectedRequest("malformed request", requestLine);
    client.stop();
    return;
  }

  Serial.print(F("HTTP request line: "));
  Serial.println(requestLine);
  Serial.print(F("HTTP route: ["));
  Serial.print(target);
  Serial.println(F("]"));

  if (strcmp(method, "GET") != 0)
  {
    skipUnexpectedRequest("non-GET method", method);
    client.stop();
    return;
  }

  if (!isExpectedGetTarget(target))
  {
    skipUnexpectedRequest("unknown path", target);
    client.stop();
    return;
  }

  discardAlreadyBufferedBytes(client);

  if (strcmp(target, "/") == 0) handleRoot(client);
  else if (targetMatchesPath(target, "/dashboard")) handleDashboard(client);
  else if (targetMatchesPath(target, "/api/ping")) handlePing(client);
  else if (targetMatchesPath(target, "/api/status") || targetMatchesPath(target, "/api/state")) handleStatus(client);
  else if (targetMatchesPath(target, "/sensor")) handleSensor(client);
  else if (targetMatchesPath(target, "/debug")) handleDebug(client);
  else if (targetMatchesPath(target, "/motor")) handleMotor(client, target);
  else if (targetMatchesPath(target, "/health"))
  {
    requestCount++;
    sendText(client, 200, "OK", "text/plain", "ok\n");
  }
  else if (targetMatchesPath(target, "/favicon.ico"))
  {
    requestCount++;
    sendHeaders(client, 204, "No Content", "text/plain", 0);
  }
  else skipUnexpectedRequest("unexpected fallthrough", target);

  delay(1);
  client.stop();

  Serial.print(F("HTTP client closed ms: "));
  Serial.println(millis() - requestStartMs);
}
