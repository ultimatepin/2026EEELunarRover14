#include "AppConfig.h"
#include "BoardState.h"
#include "Network.h"
#include "WebRoutes.h"

extern WiFiServer server;

namespace
{
  unsigned long requestCount = 0;

  const char ROOT_TEXT[] =
      "Metro M0 raw API server v3.5\n"
      "Use /api/ping, /api/status, /api/led?value=1, or /api/led?value=0\n";

  bool readHttpLine(WiFiClient &client, char *buffer, size_t bufferSize, unsigned long timeoutMs)
  {
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

  void drainHeadersBriefly(WiFiClient &client)
  {
    char line[HTTP_REQUEST_LINE_MAX];

    // For GET requests we only need the request line. Do not wait long for
    // browser/hotspot header noise; the connection will be closed anyway.
    while (readHttpLine(client, line, sizeof(line), 40))
    {
      if (line[0] == '\0')
      {
        return;
      }
    }
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
    client.println();
  }

  void sendText(WiFiClient &client, int code, const char *status, const char *contentType, const char *body)
  {
    sendHeaders(client, code, status, contentType, strlen(body));
    client.print(body);
  }

  void sendString(WiFiClient &client, int code, const char *status, const char *contentType, const String &body)
  {
    sendHeaders(client, code, status, contentType, body.length());
    client.print(body);
  }

  bool targetMatchesPath(const char *target, const char *path)
  {
    const size_t pathLen = strlen(path);

    if (strncmp(target, path, pathLen) != 0)
    {
      return false;
    }

    return target[pathLen] == '\0' || target[pathLen] == '?';
  }

  bool isExpectedGetTarget(const char *target)
  {
    return strcmp(target, "/") == 0 ||
           targetMatchesPath(target, "/api/ping") ||
           targetMatchesPath(target, "/api/status") ||
           targetMatchesPath(target, "/api/state") ||
           targetMatchesPath(target, "/api/led") ||
           targetMatchesPath(target, "/health") ||
           targetMatchesPath(target, "/favicon.ico");
  }

  const char *findQueryValue(const char *target, const char *name)
  {
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
    // Keep root tiny. The previous full HTML page could block while writing
    // through some phone hotspots, which then prevented later /api/ping hits.
    requestCount++;
    sendText(client, 200, "OK", "text/plain", ROOT_TEXT);
  }

  void handlePing(WiFiClient &client)
  {
    const unsigned long startMs = millis();
    requestCount++;
    sendText(client, 200, "OK", "text/plain", "pong\n");

    Serial.print(F("handlePing ms: "));
    Serial.println(millis() - startMs);
  }

  void handleStatus(WiFiClient &client)
  {
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
    Serial.print(F("Skipping unexpected request: "));
    Serial.print(reason);

    if (detail && detail[0] != '\0')
    {
      Serial.print(F(" | "));
      Serial.print(detail);
    }

    Serial.println();
  }
}

void handleHttpClient()
{
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

  char method[8] = {0};
  char target[HTTP_TARGET_MAX] = {0};

  if (sscanf(requestLine, "%7s %63s", method, target) != 2)
  {
    skipUnexpectedRequest("malformed request line", requestLine);
    client.stop();
    return;
  }

  Serial.print(F("HTTP route: "));
  Serial.println(target);

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

  drainHeadersBriefly(client);

  if (strcmp(target, "/") == 0)
  {
    handleRoot(client);
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

  delay(1);
  client.stop();

  Serial.print(F("HTTP client closed ms: "));
  Serial.println(millis() - requestStartMs);
}
