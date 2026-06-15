#include "AppConfig.h"
#include "BoardState.h"
#include "Network.h"
#include "WebRoutes.h"

/*
  WebRoutes.cpp
  =============

  This is the raw HTTP router.

  Django analogy:
  - isExpectedGetTarget() is a route allowlist, like a strict urls.py.
  - handlePing(), handleStatus(), handleLed(), etc. are view functions.
  - sendText()/sendString() are like returning HttpResponse/JsonResponse.

  Difference from Django:
  - Django receives a fully parsed request object.
  - Here, the Arduino receives raw bytes from a TCP socket. We manually read:

      GET /api/ping HTTP/1.1

    Then we manually extract:
      method = GET
      target = /api/ping

  Why so strict?
  - Phone hotspots and browsers can create partial, repeated, or odd requests.
  - The WINC1500 has limited sockets/resources.
  - If a connection is left open, future requests may hang.

  Therefore the rule is:
    accept one request line -> route only known GET paths -> respond -> close.
*/

extern WiFiServer server;

namespace
{
  /*
    Count known handled requests.
    This is shown in JSON so you can see requests increasing.
  */
  unsigned long requestCount = 0;

  /*
    Tiny root response.

    Important design choice:
      / is deliberately tiny and safe. Browsers often request / automatically,
      so root should never be expensive enough to wedge the board.
  */
  const char ROOT_TEXT[] =
      "Metro M0 + WINC1500 raw API server OK\n"
      "Test: /api/ping\n"
      "Dashboard: /dashboard\n";

  /*
    Small dashboard HTML.

    This is kept as one C string because Arduino can serve static text directly.

    Browser-side behavior:
      - Buttons call /api/led?value=1 or /api/led?value=0.
      - Status polling calls /api/status every 5 seconds.
      - A busy flag avoids overlapping status requests.

    Django analogy:
      This is like returning a simple template response, except the template is
      hard-coded into the microcontroller firmware.
  */
  const char DASHBOARD_HTML[] =
      "<!doctype html><html><head>"
      "<meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Metro M0</title>"
      "<style>body{font-family:system-ui,Arial;margin:20px;max-width:680px}"
      "button{font-size:16px;padding:8px 12px;margin:4px}"
      "pre{white-space:pre-wrap;background:#eee;padding:10px;border-radius:8px}"
      ".box{border:1px solid #ccc;border-radius:10px;padding:12px;margin:12px 0}"
      "</style></head><body>"
      "<h1>Metro M0 + WINC1500</h1>"
      "<div class='box'><h2>LED</h2>"
      "<p>State: <b id='l'>unknown</b></p>"
      "<button onclick='L(1)'>On</button><button onclick='L(0)'>Off</button>"
      "<p id='lt'></p></div>"
      "<div class='box'><h2>Status</h2><pre id='s'>loading...</pre></div>"
      "<script>let b=0;async function G(u){let t=performance.now();let r=await fetch(u,{cache:'no-store'});let x=await r.text();return{x,ms:Math.round(performance.now()-t)}}"
      "async function S(){if(b)return;b=1;try{let r=await G('/api/status');s.textContent=r.x+'\\nrequest_ms: '+r.ms}catch(e){s.textContent='status failed: '+e}b=0}"
      "async function L(v){try{let r=await G('/api/led?value='+v);let d=JSON.parse(r.x);l.textContent=d.led_on?'ON':'OFF';lt.textContent='request_ms: '+r.ms}catch(e){lt.textContent='LED failed: '+e}}"
      "S();setInterval(S,5000)</script></body></html>";

  bool readHttpLine(WiFiClient &client, char *buffer, size_t bufferSize, unsigned long timeoutMs)
  {
    /*
      Read one line from the TCP client.

      Parameters:
        client:     the browser/client socket.
        buffer:     char array where we store the line.
        bufferSize: maximum usable size of buffer.
        timeoutMs:  how long to wait before giving up.

      Expected input:
        GET /api/ping HTTP/1.1\r\n
      Output:
        buffer = "GET /api/ping HTTP/1.1"

      Return value:
        true  = got a non-empty line or at least partial line.
        false = client stalled or disconnected before sending anything useful.

      C note:
        C strings are char arrays ending with '\0'. We always add '\0' before
        returning so Serial.println(buffer) and parser functions know where the
        string ends.
    */
    size_t length = 0;
    const unsigned long startMs = millis();

    while (millis() - startMs < timeoutMs)
    {
      while (client.available())
      {
        const char c = client.read();

        if (c == '\r')
        {
          continue;
        }

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

      if (!client.connected())
      {
        break;
      }

      delay(1);
    }

    buffer[length] = '\0';
    return length > 0;
  }

  bool parseRequestLine(const char *line, char *method, size_t methodSize, char *target, size_t targetSize)
  {
    /*
      Parse the first HTTP request line.

      Input example:
        line = "GET /api/led?value=1 HTTP/1.1"

      Output:
        method = "GET"
        target = "/api/led?value=1"

      We intentionally do not parse HTTP version because this project does not
      need it. Simpler parser = fewer ways to break on a microcontroller.
    */
    if (!line || !method || !target || methodSize == 0 || targetSize == 0)
    {
      return false;
    }

    method[0] = '\0';
    target[0] = '\0';

    const char *p = line;

    while (*p == ' ')
    {
      p++;
    }

    size_t i = 0;
    while (*p && *p != ' ' && i < methodSize - 1)
    {
      method[i++] = *p++;
    }
    method[i] = '\0';

    while (*p == ' ')
    {
      p++;
    }

    i = 0;
    while (*p && *p != ' ' && i < targetSize - 1)
    {
      target[i++] = *p++;
    }
    target[i] = '\0';

    return method[0] != '\0' && target[0] != '\0';
  }

  void discardAlreadyBufferedBytes(WiFiClient &client)
  {
    /*
      HTTP requests contain headers after the first line:
        Host: 172.20.10.3
        User-Agent: ...
        etc.

      This project does not need them.

      Important hotspot fix:
        We discard only bytes already available. We do NOT wait for all headers.
        Waiting for more header data was one way the server got wedged.
    */
    while (client.available())
    {
      client.read();
    }
  }

  void sendHeaders(WiFiClient &client, int code, const char *status, const char *contentType, size_t contentLength)
  {
    /*
      Write minimal HTTP response headers.

      Parameters:
        code:          numeric HTTP status, e.g. 200, 400, 204.
        status:        text phrase, e.g. "OK".
        contentType:   MIME type, e.g. "application/json".
        contentLength: number of body bytes that follow.

      Connection: close tells the browser that this board will close the socket
      after the response. That matches our client.stop() behavior.
    */
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
    client.println();
  }

  void sendRawBodyInChunks(WiFiClient &client, const char *body)
  {
    /*
      Send static C-string body in small chunks.

      Why chunks?
        Small boards + WiFi shields may behave better with smaller writes than
        one large write. The 1 ms delay gives the WiFi module time to flush.
    */
    const size_t CHUNK_SIZE = 48;
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
    /*
      Send a response where the body is a normal C string.
      Used for static text, root, dashboard, and simple errors.
    */
    sendHeaders(client, code, status, contentType, strlen(body));
    sendRawBodyInChunks(client, body);
  }

  void sendString(WiFiClient &client, int code, const char *status, const char *contentType, const String &body)
  {
    /*
      Send a response where the body is an Arduino String.
      Used for JSON generated at runtime.

      C++ note:
        const String &body passes the object by reference without copying it.
    */
    sendHeaders(client, code, status, contentType, body.length());
    client.print(body);
  }

  bool targetMatchesPath(const char *target, const char *path)
  {
    /*
      Check whether a request target matches a path, allowing query strings.

      Examples:
        targetMatchesPath("/api/led?value=1", "/api/led") -> true
        targetMatchesPath("/api/led",         "/api/led") -> true
        targetMatchesPath("/api/ledx",        "/api/led") -> false
    */
    const size_t pathLen = strlen(path);

    if (strncmp(target, path, pathLen) != 0)
    {
      return false;
    }

    return target[pathLen] == '\0' || target[pathLen] == '?';
  }

  bool isExpectedGetTarget(const char *target)
  {
    /*
      Route allowlist.

      Django analogy:
        These are the only URL patterns this tiny server accepts.

      Why close unknown paths instead of returning a fancy 404?
        On this fragile hotspot setup, the safest response to unexpected traffic
        is to close quickly and preserve sockets.
    */
    return strcmp(target, "/") == 0 ||
           targetMatchesPath(target, "/dashboard") ||
           targetMatchesPath(target, "/api/ping") ||
           targetMatchesPath(target, "/api/status") ||
           targetMatchesPath(target, "/api/state") ||
           targetMatchesPath(target, "/api/led") ||
           targetMatchesPath(target, "/health") ||
           targetMatchesPath(target, "/favicon.ico");
  }

  const char *findQueryValue(const char *target, const char *name)
  {
    /*
      Find query parameter value in the target string.

      Example:
        target = "/api/led?value=1"
        name   = "value"
        returns pointer to "1"

      Django comparison:
        request.GET.get("value")

      C difference:
        We return a pointer into the original target string. We do not allocate
        a new string. queryValueEquals() later compares until '&' or string end.
    */
    const char *query = strchr(target, '?');

    if (!query)
    {
      return nullptr;
    }

    query++;
    const size_t nameLen = strlen(name);

    while (*query)
    {
      if (strncmp(query, name, nameLen) == 0 && query[nameLen] == '=')
      {
        return query + nameLen + 1;
      }

      query = strchr(query, '&');

      if (!query)
      {
        return nullptr;
      }

      query++;
    }

    return nullptr;
  }

  bool queryValueEquals(const char *value, const char *expected)
  {
    /*
      Compare query value to an expected string, case-insensitive.

      Examples:
        "ON" equals "on"
        "true" equals "true"
        "1&x=abc" equals "1" because the value ends at '&'
    */
    if (!value)
    {
      return false;
    }

    while (*expected && *value && *value != '&')
    {
      const char a = tolower(*value++);
      const char b = tolower(*expected++);

      if (a != b)
      {
        return false;
      }
    }

    return *expected == '\0' && (*value == '\0' || *value == '&');
  }

  void handleRoot(WiFiClient &client)
  {
    /* View: GET / */
    const unsigned long startMs = millis();
    requestCount++;
    sendText(client, 200, "OK", "text/plain", ROOT_TEXT);

    Serial.print(F("handleRoot ms: "));
    Serial.println(millis() - startMs);
  }

  void handleDashboard(WiFiClient &client)
  {
    /* View: GET /dashboard */
    const unsigned long startMs = millis();
    requestCount++;
    sendText(client, 200, "OK", "text/html", DASHBOARD_HTML);

    Serial.print(F("handleDashboard ms: "));
    Serial.println(millis() - startMs);
  }

  void handlePing(WiFiClient &client)
  {
    /*
      View: GET /api/ping

      Usage:
        http://BOARD_IP/api/ping

      Purpose:
        Minimal route to prove browser -> hotspot -> board -> server works.
    */
    const unsigned long startMs = millis();
    requestCount++;
    sendText(client, 200, "OK", "text/plain", "pong\n");

    Serial.print(F("handlePing ms: "));
    Serial.println(millis() - startMs);
  }

  void handleStatus(WiFiClient &client)
  {
    /*
      View: GET /api/status

      Returns board status JSON without led_on.
      The dashboard polls this route every 5 seconds.
    */
    const unsigned long startMs = millis();
    requestCount++;

    BoardSnapshot snapshot = readBoardSnapshot();
    String json = boardStatusToJson(snapshot, requestCount);
    json += '\n';

    sendString(client, 200, "OK", "application/json", json);

    Serial.print(F("handleStatus ms: "));
    Serial.println(millis() - startMs);
  }

  void handleState(WiFiClient &client)
  {
    /*
      View: GET /api/state

      Returns full combined JSON, including LED state.
      Kept as a convenient debug/backwards-compatible route.
    */
    const unsigned long startMs = millis();
    requestCount++;

    BoardSnapshot snapshot = readBoardSnapshot();
    String json = boardFullStateToJson(snapshot, requestCount);
    json += '\n';

    sendString(client, 200, "OK", "application/json", json);

    Serial.print(F("handleState ms: "));
    Serial.println(millis() - startMs);
  }

  void handleLed(WiFiClient &client, const char *target)
  {
    /*
      View: GET /api/led?value=...

      Usage:
        /api/led?value=1
        /api/led?value=0
        /api/led?value=true
        /api/led?value=false
        /api/led?value=on
        /api/led?value=off

      Parameter:
        value controls the LED state.

      Returns:
        {"led_on":true}
        or
        {"led_on":false}
    */
    const unsigned long startMs = millis();
    requestCount++;

    const char *value = findQueryValue(target, "value");

    if (!value)
    {
      sendText(client, 400, "Bad Request", "text/plain", "Missing query parameter: value. Use /api/led?value=1 or /api/led?value=0\n");
      return;
    }

    if (queryValueEquals(value, "1") || queryValueEquals(value, "true") || queryValueEquals(value, "on"))
    {
      setLedState(true);
    }
    else if (queryValueEquals(value, "0") || queryValueEquals(value, "false") || queryValueEquals(value, "off"))
    {
      setLedState(false);
    }
    else
    {
      sendText(client, 400, "Bad Request", "text/plain", "Invalid value. Use 1, 0, true, false, on, or off.\n");
      return;
    }

    String json = boardLedToJson();
    json += '\n';
    sendString(client, 200, "OK", "application/json", json);

    Serial.print(F("handleLed ms: "));
    Serial.println(millis() - startMs);
  }

  void skipUnexpectedRequest(const char *reason, const char *detail = nullptr)
  {
    /*
      Log skipped traffic.

      We usually do not send a body for skipped traffic because the goal is to
      preserve the board's socket resources. handleHttpClient() closes the client
      immediately after calling this.
    */
    Serial.print(F("Skipping unexpected request: "));
    Serial.print(reason);

    if (detail && detail[0] != '\0')
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
  /*
    Main raw HTTP request handler.

    Called repeatedly from loop(). It handles at most one client per call.

    Flow:
      1. Check whether a client is waiting.
      2. Read first request line with timeout.
      3. Parse method and target.
      4. Reject non-GET or unknown paths.
      5. Call the matching view function.
      6. Close the client socket.
  */
  WiFiClient client = server.available();

  if (!client)
  {
    return;
  }

  const unsigned long requestStartMs = millis();
  Serial.println(F("HTTP client connected"));

  char requestLine[HTTP_REQUEST_LINE_MAX];

  if (!readHttpLine(client, requestLine, sizeof(requestLine), HTTP_CLIENT_TIMEOUT_MS))
  {
    Serial.println(F("Empty/stalled client; closing."));
    client.stop();
    return;
  }

  Serial.print(F("HTTP request line: "));
  Serial.println(requestLine);

  char method[8];
  char target[HTTP_TARGET_MAX];

  if (!parseRequestLine(requestLine, method, sizeof(method), target, sizeof(target)))
  {
    skipUnexpectedRequest("malformed request line", requestLine);
    client.stop();
    return;
  }

  Serial.print(F("HTTP method: ["));
  Serial.print(method);
  Serial.println(F("]"));

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

  /*
    Route dispatch.

    Django analogy:
      This if/else chain is our tiny urls.py + view resolver.
  */
  if (strcmp(target, "/") == 0)
  {
    handleRoot(client);
  }
  else if (targetMatchesPath(target, "/dashboard"))
  {
    handleDashboard(client);
  }
  else if (targetMatchesPath(target, "/api/ping"))
  {
    handlePing(client);
  }
  else if (targetMatchesPath(target, "/api/status"))
  {
    handleStatus(client);
  }
  else if (targetMatchesPath(target, "/api/state"))
  {
    handleState(client);
  }
  else if (targetMatchesPath(target, "/api/led"))
  {
    handleLed(client, target);
  }
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
  else
  {
    skipUnexpectedRequest("unexpected fallthrough", target);
  }

  /*
    Critical stability rule:
      Always close the client.

    This releases the WINC1500 socket so the next request can be accepted.
  */
  delay(1);
  client.stop();

  Serial.print(F("HTTP client closed ms: "));
  Serial.println(millis() - requestStartMs);
}
