#include "interface.h"
#include "config.h"

using namespace qindesign::network;

static EthernetServer server(80);

void stopWebServer() {
    server.end();
}

// ---------------- Utilities ----------------
static String urlDecode(const String &in) {
    String out; out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i) {
        char c = in[i];
        if (c == '+') { out += ' '; continue; }
        if (c == '%' && i + 2 < in.length()) {
            char h1 = in[i+1], h2 = in[i+2];
            auto hex = [](char h)->int{
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                return -1;
            };
            int v1 = hex(h1), v2 = hex(h2);
            if (v1 >= 0 && v2 >= 0) { out += char((v1<<4) | v2); i += 2; continue; }
        }
        out += c;
    }
    return out;
}

static void send404(EthernetClient &client) {
    client.println("HTTP/1.1 404 Not Found");
    client.println("Content-Type: text/html; charset=utf-8");
    client.println("Connection: close");
    client.println();
    client.println("<!doctype html><html><body><h1>404 Not Found</h1></body></html>");
}

static void send204(EthernetClient &client) {
    client.println("HTTP/1.1 204 No Content");
    client.println("Connection: close");
    client.println();
}

// --------------- HTML Page -----------------
static const char htmlPage[] =
R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>SkyLED Node Configuration</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
 body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu,sans-serif;margin:2rem;max-width:680px}
 label{display:block;margin-top:1rem}
 input,select{padding:.5rem;font-size:1rem;width:100%;max-width:360px}
 button,input[type=submit]{margin-top:1rem;padding:.6rem 1rem;font-size:1rem;cursor:pointer}
 .row{display:flex;gap:1rem;flex-wrap:wrap}
 .col{flex:1 1 280px}
 .card{padding:1rem;border:1px solid #ddd;border-radius:.5rem}
</style></head>
<body>
<h1>SkyLED Node Configuration</h1>
<form action="/submit" method="get" class="card">
  <div class="row">
    <div class="col">
      <label for="ip">Static IP</label>
      <input id="ip" name="ip" value="%IP%">
      <label for="subnet">Subnet Mask</label>
      <input id="subnet" name="subnet" value="%SUBNET%">
      <label for="gateway">Gateway</label>
      <input id="gateway" name="gateway" value="%GATEWAY%">
    </div>
    <div class="col">
      <label for="ledtype">LED Type</label>
      <select id="ledtype" name="ledtype">
        <option value="WS2811" %WS2811_SELECTED%>WS2811</option>
        <option value="WS2812" %WS2812_SELECTED%>WS2812</option>
        <option value="WS2813" %WS2813_SELECTED%>WS2813</option>
      </select>
      <label for="colororder">Color Order</label>
      <select id="colororder" name="colororder">
        <option value="GRB" %GRB_SELECTED%>GRB</option>
        <option value="RGB" %RGB_SELECTED%>RGB</option>
        <option value="BRG" %BRG_SELECTED%>BRG</option>
      </select>
      <label for="updateSpeed">Update Speed (Hz)</label>
      <input type="number" id="updateSpeed" name="updateSpeed" value="%UPDATE_SPEED%">
    </div>
  </div>
  <input type="submit" value="Save">
</form>
</body></html>)HTML";

// ------------- Page builders ---------------
static void serveConfigPage(EthernetClient &client) {
    String s(htmlPage);

    // Fill placeholders
    s.replace("%IP%",       ipToString(staticIP));
    s.replace("%SUBNET%",   ipToString(subnetMask));
    s.replace("%GATEWAY%",  ipToString(gateway));
    s.replace("%UPDATE_SPEED%", String(updateSpeed));

    s.replace("%WS2811_SELECTED%", (ledType == "WS2811") ? "selected" : "");
    s.replace("%WS2812_SELECTED%", (ledType == "WS2812") ? "selected" : "");
    s.replace("%WS2813_SELECTED%", (ledType == "WS2813") ? "selected" : "");

    s.replace("%GRB_SELECTED%", (colorOrder == "GRB") ? "selected" : "");
    s.replace("%RGB_SELECTED%", (colorOrder == "RGB") ? "selected" : "");
    s.replace("%BRG_SELECTED%", (colorOrder == "BRG") ? "selected" : "");

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=utf-8");
    client.println("Connection: close");
    client.println();
    client.print(s);
}

static void sendRebootRedirect(EthernetClient &client) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=utf-8");
    client.println("Connection: close");
    client.println();
    client.print("<!doctype html><html><head><meta charset='utf-8'><title>Settings Updated</title>");
    client.print("<script>setTimeout(function(){location.href='http://");
    client.print(ipToString(staticIP));
    client.print("/';},15000);</script></head><body>");
    client.print("<h1>Settings Updated</h1><p>Rebooting to apply new settings...</p>");
    client.print("<p>You will be redirected automatically.</p></body></html>");
}

// -------------- HTTP routing ---------------
void setupWebServer() {
    server.begin();
    Serial.print("Web server is at ");
    Serial.println(Ethernet.localIP());
}

void handleWebServer() {
    EthernetClient client = server.available();
    if (!client) return;

    client.setTimeout(250);
    Serial.println("Client connected");

    // 1) Read request line: "GET /path?query HTTP/1.1"
    String reqLine = client.readStringUntil('\n');
    reqLine.trim();                    // drop trailing \r
    Serial.println(reqLine);

    // 2) Drain headers
    while (client.connected()) {
        String h = client.readStringUntil('\n');
        if (h == "\r" || h.length() == 0) break;
    }

    // 3) Basic parse
    int sp1 = reqLine.indexOf(' ');
    int sp2 = reqLine.indexOf(' ', sp1 + 1);
    if (sp1 <= 0 || sp2 <= sp1) { client.stop(); Serial.println("Bad request line"); return; }
    String method = reqLine.substring(0, sp1);
    String path   = reqLine.substring(sp1 + 1, sp2);

    if (method != "GET") { client.stop(); Serial.println("Non-GET -> close"); return; }

    // Split path/query
    String pathname = path;
    String query;
    int qpos = path.indexOf('?');
    if (qpos >= 0) {
        pathname = path.substring(0, qpos);
        query    = path.substring(qpos + 1);
    }

    // Route
    if (pathname == "/" || pathname == "/index.html") {
        serveConfigPage(client);
    } else if (pathname == "/submit") {
        // Parse query key=val&key=val...
        // Update config
        int start = 0;
        while (start < query.length()) {
            int amp = query.indexOf('&', start);
            if (amp < 0) amp = query.length();
            String pair = query.substring(start, amp);
            int eq = pair.indexOf('=');
            if (eq > 0) {
                String key = urlDecode(pair.substring(0, eq));
                String val = urlDecode(pair.substring(eq + 1));

                if (key == "ip")           { stringToIP(val, staticIP); }
                else if (key == "subnet")  { stringToIP(val, subnetMask); }
                else if (key == "gateway") { stringToIP(val, gateway); }
                else if (key == "ledtype") { ledType = val; }
                else if (key == "colororder") { colorOrder = val; }
                else if (key == "updateSpeed") { updateSpeed = (uint16_t)val.toInt(); }
            }
            start = amp + 1;
        }

        // Persist + respond + reboot
        saveSettingsToSD();
        sendRebootRedirect(client);
        delay(1000);
        SCB_AIRCR = 0x05FA0004; // Reboot
    } else if (pathname == "/favicon.ico") {
        send204(client);
    } else {
        send404(client);
    }

    delay(1);
    client.stop();
    Serial.println("Client disconnected");
}
