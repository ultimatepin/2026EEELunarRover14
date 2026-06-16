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
  unsigned long lastMotorCommandSeq = 0; // Dashboard adds seq so late old move packets cannot override a newer stop.

  const char ROOT_TEXT[] =
      "EEELunarRover raw WiFi production build\n"
      "Open /dashboard for WASD controls.\n"
      "Radio debug: /radio. Test /api/ping first.\n";

  const char DASHBOARD_HTML[] = R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EEELunarRover Control</title>
<style>
:root{font-family:system-ui,Arial;background:#0f1218;color:#f2f5fb}body{margin:0;padding:16px;max-width:760px}h1{margin:0 0 6px}.sub{color:#9aa6b8;margin:0 0 14px}.grid{display:grid;gap:12px}.card{background:#171c25;border:1px solid #303848;border-radius:14px;padding:14px}.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.big{font-size:32px;font-weight:800}.pill{display:inline-block;border:1px solid #4b5568;border-radius:999px;padding:7px 12px;background:#202838}.ok{color:#4ade80}.warn{color:#facc15}.bad{color:#fb7185}button{font:inherit;font-weight:700;border:1px solid #4b5568;border-radius:10px;background:#202838;color:#f2f5fb;padding:10px 14px}button:active{background:#2f6feb}.keys{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:360px;margin-top:12px}.key{border:1px solid #4b5568;border-radius:12px;padding:12px;text-align:center;background:#202838;font-weight:800;user-select:none}.key span{display:block;font-size:12px;color:#9aa6b8;font-weight:500}.key.on{background:#2f6feb;border-color:#8ab4ff;color:white}.key.on span{color:white}.metric{display:grid;grid-template-columns:110px 1fr;gap:5px 10px}.label{color:#9aa6b8}pre{white-space:pre-wrap;background:#0b0e13;border-radius:10px;padding:10px;overflow:auto}details{color:#cbd5e1}summary{cursor:pointer;color:#9aa6b8}.footer{font-size:13px;color:#9aa6b8}
</style>
</head>
<body tabindex="0">
<h1>EEELunarRover</h1>
<p class="sub">Hold WASD / arrows to drive. Release keys to stop. Press Z to toggle 110/255 PWM.</p>
<div class="grid">
  <section class="card">
    <div class="row">
      <span>Command</span><span id="cmdText" class="big warn">STOP</span>
      <span class="pill">Speed <b id="speedText">110</b></span>
      <button id="speedBtn" onclick="toggleSpeed('button')">Z: speed</button>
      <button onclick="emergencyStop('button')">EMERGENCY STOP</button>
    </div>
    <div class="keys">
      <div></div><div id="keyW" class="key">W<span>forward</span></div><div></div>
      <div id="keyA" class="key">A<span>left</span></div><div id="keyS" class="key">S<span>back</span></div><div id="keyD" class="key">D<span>right</span></div>
      <div id="keyQ" class="key">Q<span>rotate L</span></div><div id="keyZ" class="key">Z<span>110/255</span></div><div id="keyE" class="key">E<span>rotate R</span></div>
    </div>
    <p id="motorStatus" class="footer">ready</p>
  </section>
  <section class="card">
    <h2>Rock telemetry</h2>
    <div class="metric">
      <div class="label">Rock</div><div id="rock">--</div>
      <div class="label">Confidence</div><div id="fusion">--</div>
      <div class="label">Age</div><div id="age">--</div>
      <div class="label">Radio</div><div id="radio">--</div>
      <div class="label">IR</div><div id="ir">--</div>
      <div class="label">Ultrasonic</div><div id="us">--</div>
      <div class="label">Magnetic</div><div id="mag">--</div>
    </div>
  </section>
  <section class="card">
    <div class="row">
      <button onclick="ping()">Ping</button>
      <button onclick="loadSensor()">Refresh sensor</button>
      <button onclick="loadDebug()">Debug</button>
      <button onclick="loadRadio()">Radio</button>
      <button onclick="loadI2c()">I2C</button>
      <button onclick="sendMotor('w','test forward');setTimeout(function(){sendMotor('stop','test auto-stop');},350)">Motor test</button>
      <span id="conn" class="footer">not tested</span>
    </div>
    <details><summary>Raw JSON</summary><pre id="rawOut">not loaded</pre></details>
  </section>
</div>
<p class="footer">Safety: blur, tab switch, page close, or motor watchdog all stop the rover.</p>
<script>
const pressed={};
const speeds=[110,255];
let speedIndex=0,currentCmd='stop',motorSeq=0,keepaliveId=null,apiBusy=false,apiPending=null;
const motorRequests=[];
function speed(){return speeds[speedIndex];}
function setText(id,v){document.getElementById(id).textContent=v;}
function setClass(id,c){document.getElementById(id).className=c;}
function req(path,cb){
  if(apiBusy){apiPending=[path,cb];return;}
  apiBusy=true;
  const x=new XMLHttpRequest();x.timeout=3000;
  x.onreadystatechange=function(){if(x.readyState===4){apiBusy=false;cb(x.status,x.responseText);if(apiPending){const p=apiPending;apiPending=null;req(p[0],p[1]);}}};
  x.ontimeout=function(){apiBusy=false;cb(0,'timeout');};
  x.open('GET',path);x.send();
}
function sendMotor(c,why){
  motorSeq++;
  const seq=motorSeq;
  const url='/motor?cmd='+encodeURIComponent(c)+'&speed='+speed()+'&seq='+seq+'&_t='+Date.now();
  const x=new XMLHttpRequest();

  // Keep the XHR object in motorRequests until it finishes.
  // A temporary Image() request can be garbage-collected by the browser before
  // the GET reliably reaches a tiny embedded server. Retained XHR is more
  // predictable while still being fast enough for WASD control.
  motorRequests.push(x);
  while(motorRequests.length>8){motorRequests.shift();}

  x.timeout=700;
  x.onreadystatechange=function(){
    if(x.readyState===4){
      const i=motorRequests.indexOf(x); if(i>=0)motorRequests.splice(i,1);
      if(seq===motorSeq){
        const body=(x.responseText||'').trim();
        setText('motorStatus','ack '+(body||c).toUpperCase()+' | speed '+speed()+' | HTTP '+x.status+' | #'+seq);
      }
    }
  };
  x.ontimeout=function(){
    const i=motorRequests.indexOf(x); if(i>=0)motorRequests.splice(i,1);
    if(seq===motorSeq)setText('motorStatus','motor request timeout | '+c.toUpperCase()+' | #'+seq);
  };
  x.onerror=function(){
    const i=motorRequests.indexOf(x); if(i>=0)motorRequests.splice(i,1);
    if(seq===motorSeq)setText('motorStatus','motor request failed | '+c.toUpperCase()+' | #'+seq);
  };
  x.open('GET',url,true);
  x.send();
  setText('motorStatus','sending '+c.toUpperCase()+' | speed '+speed()+' | '+why+' | #'+seq);
}
function commandFromKeys(){
  const w=!!pressed.w,a=!!pressed.a,s=!!pressed.s,d=!!pressed.d,q=!!pressed.q,e=!!pressed.e;
  if(q&&!e)return 'q'; if(e&&!q)return 'e';
  const y=(w?1:0)-(s?1:0), x=(d?1:0)-(a?1:0);
  if(y===1&&x===0)return 'w'; if(y===-1&&x===0)return 's'; if(y===0&&x===-1)return 'a'; if(y===0&&x===1)return 'd';
  if(y===1&&x===-1)return 'wa'; if(y===1&&x===1)return 'wd'; if(y===-1&&x===-1)return 'sa'; if(y===-1&&x===1)return 'sd';
  return 'stop';
}
function refreshUi(){
  ['w','a','s','d','q','e'].forEach(k=>{const el=document.getElementById('key'+k.toUpperCase()); if(el)el.className=pressed[k]?'key on':'key';});
  setText('cmdText',currentCmd.toUpperCase()); setClass('cmdText',currentCmd==='stop'?'big warn':'big ok');
  setText('speedText',speed()); document.getElementById('keyZ').className=speed()===255?'key on':'key';
}
function updateDrive(why){
  const next=commandFromKeys();
  if(next!==currentCmd){currentCmd=next;refreshUi();sendMotor(next,why); if(next==='stop')stopKeepalive(); else startKeepalive();}
  else refreshUi();
}
function keyName(ev){
  const k=(ev.key||'').toLowerCase();
  if(['w','a','s','d','q','e','z'].includes(k))return k;
  if(ev.code==='ArrowUp')return 'w'; if(ev.code==='ArrowDown')return 's'; if(ev.code==='ArrowLeft')return 'a'; if(ev.code==='ArrowRight')return 'd'; return '';
}
function toggleSpeed(why){
  speedIndex=1-speedIndex; refreshUi();
  if(currentCmd!=='stop')sendMotor(currentCmd,'speed toggle '+why); else setText('motorStatus','speed set to '+speed());
}
window.addEventListener('keydown',function(ev){
  const k=keyName(ev); if(!k)return; ev.preventDefault();
  if(k==='z'){if(!ev.repeat)toggleSpeed('z');return;}
  if(!pressed[k]){pressed[k]=true;updateDrive('keydown '+k);}
});
window.addEventListener('keyup',function(ev){
  const k=keyName(ev); if(!k||k==='z')return; ev.preventDefault();
  if(pressed[k]){delete pressed[k];updateDrive('keyup '+k);}
});
function emergencyStop(why){['w','a','s','d','q','e'].forEach(k=>delete pressed[k]);currentCmd='stop';refreshUi();sendMotor('stop','stop '+why);stopKeepalive();}
function startKeepalive(){if(keepaliveId)return;keepaliveId=setInterval(function(){if(currentCmd!=='stop')sendMotor(currentCmd,'keepalive');},600);}
function stopKeepalive(){if(keepaliveId){clearInterval(keepaliveId);keepaliveId=null;}}
window.addEventListener('blur',function(){emergencyStop('window blur');});
document.addEventListener('visibilitychange',function(){if(document.hidden)emergencyStop('page hidden');});
window.addEventListener('beforeunload',function(){sendMotor('stop','page unload');});
function fmtConf(v){return v==null?'--':Math.round(v*100)+'%';}
function showSensor(t){
  setText('rawOut',t);
  try{const j=JSON.parse(t);setText('rock',j.rockType||'UNKNOWN');setText('fusion',fmtConf(j.fusionConf));setText('age',j.age||'--');setText('ir',(j.ir&&j.ir.label)||'--');setText('us',(j.ultrasonic&&j.ultrasonic.label)||'--');setText('mag',(j.magnetic&&j.magnetic.label)||'--');if(j.radio){setText('radio','bytes '+j.radio.bytes+' | valid '+j.radio.valid_packets+' | last '+(j.radio.last_byte_age_ms==null?'--':j.radio.last_byte_age_ms+'ms'));}}
  catch(e){setText('rawOut','sensor parse failed: '+e+'\n'+t);}
}
function ping(){req('/api/ping?_t='+Date.now(),function(s,t){setText('conn',s===200?'online':'failed: '+t);setClass('conn',s===200?'ok':'bad');});}
function loadSensor(){req('/sensor?_t='+Date.now(),function(s,t){if(s===200)showSensor(t);else setText('rawOut','sensor failed: '+t);});}
function loadDebug(){req('/debug?_t='+Date.now(),function(s,t){setText('rawOut',t);});}
function loadRadio(){req('/radio?_t='+Date.now(),function(s,t){setText('rawOut',t);});}
function loadI2c(){req('/i2cscan?_t='+Date.now(),function(s,t){setText('rawOut',t);});}
refreshUi();ping();loadSensor();setInterval(loadSensor,1500);document.body.focus();
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

    /*
      Write String responses in small chunks too. This matters when the rover is
      standalone on a phone hotspot: large writes can occasionally block longer
      than the browser's XHR timeout, even though the route itself is simple.
    */
    const size_t CHUNK_SIZE = 64;
    size_t index = 0;
    while (index < body.length())
    {
      const size_t remaining = body.length() - index;
      const size_t n = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
      client.write((const uint8_t *)body.c_str() + index, n);
      index += n;
      delay(1);
    }
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
           targetMatchesPath(target, "/radio") ||
           targetMatchesPath(target, "/i2cscan") ||
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

  void handleRadio(WiFiClient &client)
  {
    requestCount++;
    String json = roverRadioJson();
    json += '\n';
    sendString(client, 200, "OK", "application/json", json);
  }


  void handleI2cScan(WiFiClient &client)
  {
    requestCount++;
    String json = roverI2cScanJson();
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

    /*
      The WASD dashboard adds seq=1,2,3... to every motor request.
      If the hotspot/browser delivers an older move request after a newer stop,
      this guard prevents that old request from re-starting the rover.
      Manual URLs without seq still work normally for testing.
    */
    const char *seqValue = findQueryValue(target, "seq");
    if (seqValue)
    {
      const unsigned long seq = strtoul(seqValue, nullptr, 10);
      if (seq > 0 && seq <= lastMotorCommandSeq)
      {
        sendText(client, 200, "OK", "text/plain", "stale\n");
        return;
      }
      if (seq > 0) lastMotorCommandSeq = seq;
    }

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
  else if (targetMatchesPath(target, "/radio")) handleRadio(client);
  else if (targetMatchesPath(target, "/i2cscan")) handleI2cScan(client);
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
