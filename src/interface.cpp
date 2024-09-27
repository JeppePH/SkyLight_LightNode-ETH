#include "interface.h"
#include "config.h"

EthernetServer server(80); // Web server on port 80

const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Teensy ArtNet Node Configuration</title>
</head>
<body>
    <h1>Teensy ArtNet Node Configuration</h1>
    <form action="/submit" method="get">
        <label for="ip">Static IP:</label>
        <input type="text" id="ip" name="ip" value="%IP%"><br><br>

        <label for="subnet">Subnet Mask:</label>
        <input type="text" id="subnet" name="subnet" value="%SUBNET%"><br><br>

        <label for="gateway">Gateway:</label>
        <input type="text" id="gateway" name="gateway" value="%GATEWAY%"><br><br>

        <label for="ledtype">LED Type:</label>
        <select id="ledtype" name="ledtype">
            <option value="WS2811" %WS2811_SELECTED%>WS2811</option>
            <option value="WS2812" %WS2812_SELECTED%>WS2812</option>
            <option value="WS2813" %WS2813_SELECTED%>WS2813</option>
        </select><br><br>

        <label for="colororder">Color Order:</label>
        <select id="colororder" name="colororder">
            <option value="GRB" %GRB_SELECTED%>GRB</option>
            <option value="RGB" %RGB_SELECTED%>RGB</option>
            <option value="BRG" %BRG_SELECTED%>BRG</option>
        </select><br><br>

        <label for="updateSpeed">Update Speed (Hz):</label>
        <input type="number" id="updateSpeed" name="updateSpeed" value="%UPDATE_SPEED%"><br><br>

        <input type="submit" value="Submit">
    </form>
</body>
</html>
)rawliteral";

void setupWebServer()
{
    server.begin();
    Serial.print("Web server is at ");
    Serial.println(Ethernet.localIP());
}

void handleWebServer()
{
    EthernetClient client = server.available();
    if (client)
    {
        Serial.println("Client connected");
        String request = client.readStringUntil('\r');
        Serial.println(request);

        // Serve the configuration page
        if (request.indexOf("GET / ") >= 0)
        {
            serveConfigPage(client);
        }
        // Handle form submission
        else if (request.indexOf("GET /submit") >= 0)
        {
            handleFormSubmission(request, client);
        }
        // Not found
        else
        {
            client.println("HTTP/1.1 404 Not Found");
            client.println("Content-Type: text/html");
            client.println("Connection: close");
            client.println();
            client.println("<html><body><h1>404 Not Found</h1></body></html>");
        }

        delay(1); // Give the web browser time to receive the data
        client.stop(); // Close the connection
        Serial.println("Client disconnected");
    }
}

void serveConfigPage(EthernetClient &client)
{
    String s = htmlPage;

    // Replace placeholders with actual values
    s.replace("%IP%", ipToString(staticIP));
    s.replace("%SUBNET%", ipToString(subnetMask));
    s.replace("%GATEWAY%", ipToString(gateway));
    s.replace("%UPDATE_SPEED%", String(updateSpeed));

    // LED Type selection
    s.replace("%WS2811_SELECTED%", (ledType == "WS2811") ? "selected" : "");
    s.replace("%WS2812_SELECTED%", (ledType == "WS2812") ? "selected" : "");
    s.replace("%WS2813_SELECTED%", (ledType == "WS2813") ? "selected" : "");

    // Color Order selection
    s.replace("%GRB_SELECTED%", (colorOrder == "GRB") ? "selected" : "");
    s.replace("%RGB_SELECTED%", (colorOrder == "RGB") ? "selected" : "");
    s.replace("%BRG_SELECTED%", (colorOrder == "BRG") ? "selected" : "");

    // Send the HTTP response
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.print(s);
}

void handleFormSubmission(String request, EthernetClient &client)
{
    // Parse the request
    int paramsStart = request.indexOf("GET /submit?") + String("GET /submit?").length();
    int paramsEnd = request.indexOf(" ", paramsStart);
    String params = request.substring(paramsStart, paramsEnd);

    // Split parameters
    char paramArray[params.length() + 1];
    params.toCharArray(paramArray, params.length() + 1);
    char *token = strtok(paramArray, "&");
    while (token != NULL)
    {
        String pair = String(token);
        int equalPos = pair.indexOf('=');
        if (equalPos > 0)
        {
            String key = pair.substring(0, equalPos);
            String value = pair.substring(equalPos + 1);

            // Decode URL encoding
            value.replace("%3A", ":");
            value.replace("%2E", ".");
            value.replace("%2F", "/");
            value.replace("%2C", ",");
            value.replace("%20", " ");
            value.replace("+", " ");

            // Update configuration variables
            if (key == "ip")
            {
                stringToIP(value, staticIP);
            }
            else if (key == "subnet")
            {
                stringToIP(value, subnetMask);
            }
            else if (key == "gateway")
            {
                stringToIP(value, gateway);
            }
            else if (key == "ledtype")
            {
                ledType = value;
            }
            else if (key == "colororder")
            {
                colorOrder = value;
            }
            else if (key == "updateSpeed")
            {
                updateSpeed = value.toInt();
            }
        }
        token = strtok(NULL, "&");
    }

    // Save settings to SD card
    saveSettingsToSD();

    // Respond to the client with a page that will auto-refresh
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();

    client.println("<html>");
    client.println("<head>");
    client.println("<title>Settings Updated</title>");
    client.println("<meta http-equiv=\"refresh\" content=\"10;url=http://");
    client.print(ipToString(staticIP));
    client.println("/\">");
    client.println("</head>");
    client.println("<body>");
    client.println("<h1>Settings Updated</h1>");
    client.println("<p>The device will reboot to apply new settings.</p>");
    client.println("<p>Please wait while the device restarts.</p>");
    client.println("</body>");
    client.println("</html>");

    delay(1000); // Give the client time to receive the response

    // Reboot to apply new settings
    SCB_AIRCR = 0x05FA0004; // System reset request
}
