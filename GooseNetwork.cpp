#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <WebSocketsServer.h>
#include <map>
#include <vector>

//Hi :)
// Wi-Fi credentials for ESP32 as AP
const char* ssid = "GooseNetwork";
const char* password = "honk";

// Create AsyncWebServer object and WebSocket server
AsyncWebServer server(80);
WebSocketsServer webSocket(81);

// DNS Server to redirect all requests to the ESP32 IP
DNSServer dnsServer;

// Store message history and users
std::map<String, String> userDatabase = {
  {"guest", "123"},
  {"admin", "8384"},
  {"CrowzRule", "8384"},
  {"sys", "8384"}
};

std::map<String, std::vector<uint8_t>> connectedClients; // Map of usernames to WebSocket client IDs
std::map<uint8_t, String> clientSessions;               // Map of WebSocket client IDs to usernames

struct Message {
  String username;
  String message;
  String timestamp;
};

std::vector<Message> messageHistory;

String forbiddenWords[] = {"badword1", "badword2", "badword3"};

void addMessageToHistory(Message msg) {
  if (messageHistory.size() >= 100) {
    messageHistory.erase(messageHistory.begin());
  }
  messageHistory.push_back(msg);
}

bool containsForbiddenWords(String message) {
  for (String word : forbiddenWords) {
    if (message.indexOf(word) >= 0) {
      return true;
    }
  }
  return false;
}

// WebSocket Event Handler
void handleWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_CONNECTED) {
    IPAddress ip = webSocket.remoteIP(clientNum);
    String query = ""; // Extract username from WebSocket connection query
    String username = "guest"; // Placeholder: extract actual username from query

    if (userDatabase.find(username) != userDatabase.end()) {
      connectedClients[username].push_back(clientNum);
      clientSessions[clientNum] = username;
      Serial.println("User " + username + " connected.");
    } else {
      webSocket.disconnect(clientNum);
      Serial.println("Connection refused: Unauthorized user.");
    }

  } else if (type == WStype_TEXT) {
    String message = String((char *)payload);
    Serial.println("Received: " + message);

    // Check if it's a private message
    if (message.startsWith("@")) {
      int colonIndex = message.indexOf(":");
      if (colonIndex > 1) {
        String recipient = message.substring(1, colonIndex).trim();
        String privateMessage = message.substring(colonIndex + 1).trim();

        if (connectedClients.find(recipient) != connectedClients.end()) {
          for (uint8_t recipientClient : connectedClients[recipient]) {
            webSocket.sendTXT(recipientClient, "Private from " + clientSessions[clientNum] + ": " + privateMessage);
          }
          webSocket.sendTXT(clientNum, "You (to " + recipient + "): " + privateMessage);
        } else {
          webSocket.sendTXT(clientNum, "Error: User '" + recipient + "' is not online.");
        }
        return;
      }
    }

    // Broadcast public messages
    String sender = clientSessions[clientNum];
    if (!containsForbiddenWords(message)) {
      String formattedMessage = sender + ": " + message;
      for (const auto& pair : connectedClients) {
        for (uint8_t clientID : pair.second) {
          webSocket.sendTXT(clientID, formattedMessage);
        }
      }
      addMessageToHistory({sender, message, "timestamp_placeholder"});
    } else {
      webSocket.sendTXT(clientNum, "Error: Message contains forbidden words.");
    }
  } else if (type == WStype_DISCONNECTED) {
    if (clientSessions.find(clientNum) != clientSessions.end()) {
      String username = clientSessions[clientNum];
      auto& clientList = connectedClients[username];
      clientList.erase(std::remove(clientList.begin(), clientList.end(), clientNum), clientList.end());
      if (clientList.empty()) {
        connectedClients.erase(username);
      }
      clientSessions.erase(clientNum);
      Serial.println("Client " + String(clientNum) + " disconnected (" + username + ").");
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Set up the ESP32 as an Access Point
  WiFi.softAP(ssid, password);
  Serial.println("Access Point Started");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());

  // Start DNS server
  dnsServer.start(53, "*", WiFi.softAPIP());

  // Start WebSocket server
  webSocket.begin();
  webSocket.onEvent(handleWebSocketEvent);

  // Serve the login page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<html><head><style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f4f4f9; color: #333; text-align: center; margin: 0; padding: 0; }";
    html += "h1 { color: #4CAF50; }";
    html += "form { display: inline-block; margin-top: 20px; padding: 20px; background: #fff; border: 1px solid #ddd; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }";
    html += "input[type='text'], input[type='password'] { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #ccc; border-radius: 4px; }";
    html += "input[type='submit'] { background-color: #4CAF50; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; }";
    html += "input[type='submit']:hover { background-color: #45a049; }";
    html += "</style></head><body><h1>Login to your GooseNetwork account...</h1>";
    html += "<form method='POST' action='/login'>";
    html += "<input type='text' name='username' placeholder='Username' required>";
    html += "<input type='password' name='password' placeholder='Password' required>";
    html += "<input type='submit' value='Login'>";
    html += "</form></body></html>";
    request->send(200, "text/html", html);
  });
  
  // Handle login
  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
      String username = request->getParam("username", true)->value();
    String password = request->getParam("password", true)->value();

    // Check credentials
    if (userDatabase.find(username) != userDatabase.end() && userDatabase[username] == password) {
        // Redirect to the menu with the username as a query parameter
        request->redirect("/menu?user=" + username);
    } else {
        // Send back an error message for invalid credentials
        String html = "<html><body><h1>Invalid credentials</h1><a href='/'>Go back</a></body></html>";
        request->send(200, "text/html", html);
    }
 });

  // Serve the menu page
  server.on("/menu", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("user")) { // Ensure the 'user' query parameter is present
        request->redirect("/");
        return;
    }
    String currentUser = request->getParam("user")->value(); // Extract username from the query parameter

    // Generate the menu page HTML
    String html = "<html><head><style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f4f4f9; color: #333; text-align: center; margin: 0; padding: 0; }";
    html += "h1 { color: #4CAF50; }";
    html += "a { display: block; margin: 10px auto; padding: 10px 20px; background: #4CAF50; color: white; text-decoration: none; border-radius: 4px; }";
    html += "a:hover { background: #45a049; }";
    html += "</style></head><body><h1>Welcome, " + currentUser + "!</h1>";
    html += "<h2>Choose an option:</h2>";
    html += "<a href='/chat'>Enter GooseChat</a>";
    html += "<a href='/games'>Enter GooseGames</a>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  // Serve the game menu page
  server.on("/games", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("user")) { // Ensure the 'user' query parameter is present
        request->redirect("/");
        return;
    }
    String currentUser = request->getParam("user")->value(); // Extract username from the query parameter

    // Generate the game menu page HTML
    String html = "<html><head><style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f4f4f9; color: #333; text-align: center; margin: 0; padding: 0; }";
    html += "h1 { color: #4CAF50; }";
    html += "a { display: block; margin: 10px auto; padding: 10px 20px; background: #4CAF50; color: white; text-decoration: none; border-radius: 4px; }";
    html += "a:hover { background: #45a049; }";
    html += "</style></head><body><h1>Welcome to GooseGames, " + currentUser + "!</h1>";
    html += "<h2>Choose a game:</h2>";
    html += "<a href='/snake-game'>Snake Game</a>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  // Serve chat page
  server.on("/chat", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<html><head><style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f4f4f9; color: #333; text-align: center; margin: 0; padding: 0; }";
    html += "h1 { color: #4CAF50; }";
    html += "div.messages { margin-top: 20px; padding: 20px; background: #fff; border: 1px solid #ddd; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); max-width: 600px; margin: 20px auto; }";
    html += "p { text-align: left; }";
    html += "</style></head><body><h1>Welcome to GooseChat!</h1>";
    html += "<div class='messages'>";
    for (const auto& msg : messageHistory) {
      html += "<p><strong>" + msg.username + ":</strong> " + msg.message + " <em>[" + msg.timestamp + "]</em></p>";
    }
    html += "</div></body></html>";
    request->send(200, "text/html", html);
  });
  
  server.on("/snake-game", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<!DOCTYPE html><html><head>";
    html += "<title>Basic Snake HTML Game</title>";
    html += "<meta charset=\"UTF-8\">";
    html += "<style>";
    html += "html, body { height: 100%; margin: 0; }";
    html += "body { background: black; display: flex; align-items: center; justify-content: center; }";
    html += "canvas { border: 1px solid white; }";
    html += "</style></head><body>";
    html += "<canvas width=\"400\" height=\"400\" id=\"game\"></canvas>";
    html += "<script>";
    html += "var canvas = document.getElementById('game');";
    html += "var context = canvas.getContext('2d');";
    html += "var grid = 16;";
    html += "var count = 0;";
    html += "var snake = { x: 160, y: 160, dx: grid, dy: 0, cells: [], maxCells: 4 };";
    html += "var apple = { x: 320, y: 320 };";
    html += "function getRandomInt(min, max) { return Math.floor(Math.random() * (max - min)) + min; }";
    html += "function loop() {";
    html += "requestAnimationFrame(loop);";
    html += "if (++count < 4) return;";
    html += "count = 0;";
    html += "context.clearRect(0,0,canvas.width,canvas.height);";
    html += "snake.x += snake.dx; snake.y += snake.dy;";
    html += "if (snake.x < 0) snake.x = canvas.width - grid;";
    html += "else if (snake.x >= canvas.width) snake.x = 0;";
    html += "if (snake.y < 0) snake.y = canvas.height - grid;";
    html += "else if (snake.y >= canvas.height) snake.y = 0;";
    html += "snake.cells.unshift({x: snake.x, y: snake.y});";
    html += "if (snake.cells.length > snake.maxCells) snake.cells.pop();";
    html += "context.fillStyle = 'red';";
    html += "context.fillRect(apple.x, apple.y, grid-1, grid-1);";
    html += "context.fillStyle = 'green';";
    html += "snake.cells.forEach(function(cell, index) {";
    html += "context.fillRect(cell.x, cell.y, grid-1, grid-1);";
    html += "if (cell.x === apple.x && cell.y === apple.y) {";
    html += "snake.maxCells++; apple.x = getRandomInt(0, 25) * grid; apple.y = getRandomInt(0, 25) * grid; }";
    html += "for (var i = index + 1; i < snake.cells.length; i++) {";
    html += "if (cell.x === snake.cells[i].x && cell.y === snake.cells[i].y) {";
    html += "snake.x = 160; snake.y = 160; snake.cells = []; snake.maxCells = 4; snake.dx = grid; snake.dy = 0;";
    html += "apple.x = getRandomInt(0, 25) * grid; apple.y = getRandomInt(0, 25) * grid; }";
    html += "} }); }); }";
    html += "document.addEventListener('keydown', function(e) {";
    html += "if (e.which === 37 && snake.dx === 0) { snake.dx = -grid; snake.dy = 0; }";
    html += "else if (e.which === 38 && snake.dy === 0) { snake.dy = -grid; snake.dx = 0; }";
    html += "else if (e.which === 39 && snake.dx === 0) { snake.dx = grid; snake.dy = 0; }";
    html += "else if (e.which === 40 && snake.dy === 0) { snake.dy = grid; snake.dx = 0; }";
    html += "}); requestAnimationFrame(loop);";
    html += "</script></body></html>";
    request->send(200, "text/html", html);
  });

void loop() {
  dnsServer.processNextRequest();
  webSocket.loop();
}
