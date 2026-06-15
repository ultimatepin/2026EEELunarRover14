#include "AppConfig.h"
#include "BoardState.h"
#include "WebRoutes.h"

/*
  The server object is created once in the .ino file.

  extern means:
  "This variable exists somewhere else; let this file use it."
*/
extern WiFiWebServer server;

namespace
{
  unsigned long requestCount = 0;

  /*
    Basic HTML page.

    This page uses browser JavaScript to call:
    - GET /api/state
    - GET /api/led?value=1
    - GET /api/led?value=0

    That gives you a base for later:
    read pins on Arduino -> return JSON -> render on webpage.
  */
  const char INDEX_HTML[] =
      "<!doctype html>"
      "<html>"
      "<head>"
      "<meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<title>Metro M0 Web Base</title>"
      "<style>"
      "body{font-family:system-ui,Arial;margin:2rem;max-width:760px}"
      ".card{border:1px solid #ccc;border-radius:12px;padding:1rem;margin:1rem 0}"
      "button{font-size:1rem;padding:.6rem 1rem;margin-right:.5rem}"
      "code{background:#eee;padding:.15rem .35rem;border-radius:4px}"
      "</style>"
      "</head>"
      "<body>"
      "<h1>Metro M0 + WINC1500 Web Base</h1>"
      "<p>This page is served directly from the Adafruit Metro M0.</p>"

      "<div class='card'>"
      "<h2>LED control</h2>"
      "<button onclick='setLed(1)'>LED on</button>"
      "<button onclick='setLed(0)'>LED off</button>"
      "</div>"

      "<div class='card'>"
      "<h2>Live board state</h2>"
      "<pre id='state'>Loading...</pre>"
      "</div>"

      "<div class='card'>"
      "<h2>Useful API routes</h2>"
      "<p><code>/api/state</code> returns JSON.</p>"
      "<p><code>/api/led?value=1</code> turns LED on.</p>"
      "<p><code>/api/led?value=0</code> turns LED off.</p>"
      "</div>"

      "<script>"
      "async function loadState(){"
      "  const response = await fetch('/api/state');"
      "  const data = await response.json();"
      "  document.getElementById('state').textContent = JSON.stringify(data, null, 2);"
      "}"
      "async function setLed(value){"
      "  await fetch('/api/led?value=' + value);"
      "  await loadState();"
      "}"
      "loadState();"
      "setInterval(loadState, 1000);"
      "</script>"
      "</body>"
      "</html>";

  void sendPlainText(int code, const __FlashStringHelper *message)
  {
    server.send(code, F("text/plain"), message);
  }

  void handleIndex()
  {
    requestCount++;

    /*
      server.send(statusCode, contentType, body)

      Django analogy:
      This is like HttpResponse(body, content_type="text/html", status=200).

      Parameters:
      - 200: HTTP status code, meaning OK
      - text/html: tells browser to render body as HTML
      - INDEX_HTML: response body
    */
    server.send(200, F("text/html"), INDEX_HTML);
  }

  void handleApiState()
  {
    requestCount++;

    BoardSnapshot snapshot = readBoardSnapshot();
    String json = boardSnapshotToJson(snapshot, requestCount);

    server.send(200, F("application/json"), json);
  }

  void handleLed()
  {
    requestCount++;

    /*
      Query string example:
      /api/led?value=1

      server.hasArg("value")
      - true if the URL contains a query parameter named value

      server.arg("value")
      - returns the string value of that query parameter
    */
    if (!server.hasArg("value"))
    {
      sendPlainText(400, F("Missing query parameter: value. Use /api/led?value=1 or /api/led?value=0"));
      return;
    }

    String value = server.arg("value");
    value.toLowerCase();

    if (value == "1" || value == "true" || value == "on")
    {
      setLedState(true);
    }
    else if (value == "0" || value == "false" || value == "off")
    {
      setLedState(false);
    }
    else
    {
      sendPlainText(400, F("Invalid value. Use 1, 0, true, false, on, or off."));
      return;
    }

    BoardSnapshot snapshot = readBoardSnapshot();
    String json = boardSnapshotToJson(snapshot, requestCount);

    server.send(200, F("application/json"), json);
  }

  void handleHealth()
  {
    requestCount++;
    sendPlainText(200, F("ok"));
  }

  void handleNotFound()
  {
    requestCount++;

    /*
      Useful debug response.

      When you mistype a URL, this shows:
      - requested URI
      - method
      - query arguments

      This is helpful while developing APIs from the browser.
    */
    String message;

    message += F("404 Not Found\n\n");
    message += F("URI: ");
    message += server.uri();
    message += F("\nMethod: ");
    message += (server.method() == HTTP_GET) ? F("GET") : F("POST");
    message += F("\nArguments: ");
    message += server.args();
    message += F("\n");

    for (uint8_t i = 0; i < server.args(); i++)
    {
      message += F("  ");
      message += server.argName(i);
      message += F(": ");
      message += server.arg(i);
      message += F("\n");
    }

    server.send(404, F("text/plain"), message);
  }
}

void configureWebRoutes()
{
  /*
    server.on(path, handler)

    Django analogy:
    path("/", views.index)
    path("/api/state", views.state)

    The handler function runs when a browser requests that path.
  */
  server.on(F("/"), handleIndex);
  server.on(F("/api/state"), handleApiState);
  server.on(F("/api/led"), handleLed);
  server.on(F("/health"), handleHealth);

  /*
    Fallback route.
    Runs when no registered route matches.
  */
  server.onNotFound(handleNotFound);
}
