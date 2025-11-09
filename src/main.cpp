#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

namespace {

constexpr char HUB_SSID[] = "MissionControlHub";
constexpr char HUB_PASSWORD[] = "LostSignal2024";
constexpr uint8_t HUB_CHANNEL = 6;
constexpr unsigned long SEQUENCE_ERROR_FLASH_MS = 2500;

constexpr uint8_t BUTTON_SEQUENCE[] = {4, 1, 5, 1, 3, 5, 4, 2, 1, 3, 2, 4, 5, 3, 1};
constexpr size_t BUTTON_SEQUENCE_LENGTH = sizeof(BUTTON_SEQUENCE) / sizeof(BUTTON_SEQUENCE[0]);

enum class GameState { Puzzle1, Puzzle2, Puzzle3, MissionComplete };
enum class ConduitConfirmResult { Accepted, AlreadyConfirmed, WrongState };

WebServer server(80);
GameState currentState = GameState::Puzzle1;
size_t nextSequenceIndex = 0;
bool latchTriggered = false;
bool conduitsVerified = false;
bool sequenceError = false;
unsigned long sequenceErrorExpiresAt = 0;

String buildSequenceStatusHtml();
bool isSequenceErrorActive();
void clearSequenceError();
void markSequenceError();

String storyTextForState() {
  switch (currentState) {
    case GameState::Puzzle1:
      return F(
          "<h2>Lost Signal</h2>"
          "<p>The Orion expedition just lost contact with Mission Control. Decode the incoming "
          "message to re-align the antenna array.</p>"
          "<div class='transmission'>"
          "<h3>Last Transmission</h3>"
          "<pre>#4 🌍  #7 🪐  #2 ☄️  #9 ⭐&#10;02: ⚡ 🔋 🔋 ☁️&#10;PWR: 🔺 🟩 🔵</pre>"
          "<p class='hint'>Each icon matches a laminated key hidden in the room.</p>"
          "<ul class='cards'>"
          "<li>Card 1 — <strong>Number Key</strong>: use the numbers after each # to pick words.</li>"
          "<li>Cards 2 &amp; 3 — <strong>Emoji Keys</strong>: earth=oxygen, planet=system, meteor=offline, "
          "star=restore, bolt=power, battery=battery, cloud=conduit, shapes=set the order.</li>"
          "<li>Card 4 — <strong>Rule Key</strong>: read the first line before the second.</li>"
          "<li>Card 5 — <strong>Operation Hint</strong>: say each emoji aloud and stitch the sentences together.</li>"
          "<li>Card 6 — <strong>Confirmation</strong>: once you reach <em>system</em> and <em>restore</em>, shout them "
          "to flag Mission Control.</li>"
          "</ul>"
          "<p><em>Awaiting GM confirmation...</em></p>");
    case GameState::Puzzle2:
      if (!conduitsVerified) {
        return F(
            "<h2>Power Conduits</h2>"
            "<p>Great work! Route power through the damaged conduits on the floor. Match the colored strings "
            "to the floor diagram to bring the system back online.</p>"
            "<p class='hint'>Await GM visual confirmation before entering the command code.</p>");
      }
      return F(
          "<h2>Power Conduits</h2>"
          "<p>Conduits verified.</p>"
          "<div class='flash-banner'>POWER STABLE - BUTTON ACCESS UNLOCKED</div>"
          "<div class='callout'>264</div>"
          "<p>Power conduits aligned. Access to Button Control Chamber granted. Proceed to repower oxygen supply.</p>");
    case GameState::Puzzle3: {
      String html = F(
          "<h2>Button Sequence</h2>"
          "<p>The lock is open, but the drive bay still needs a precise manual input. "
          "Use all five buttons to enter the correct sequence.</p>"
          "<p><small>Stay sharp. Incorrect inputs reset the buffer.</small></p>");
      html += buildSequenceStatusHtml();
      if (isSequenceErrorActive()) {
        html += F("<div class='alert flash'>Incorrect input detected. Sequence reset.</div>");
      }
      return html;
    }
    case GameState::MissionComplete:
      return F(
          "<h2>Mission Complete</h2>"
          "<p>Oxygen restored. Returning to Earth.</p>"
          "<p class='success'>Mission accomplished!</p>");
    default:
      return F("<p>Unknown state.</p>");
  }
}

String gameStateLabel() {
  switch (currentState) {
    case GameState::Puzzle1:
      return F("Puzzle 1 — Message Decoding");
    case GameState::Puzzle2:
      return F("Puzzle 2 — Power Conduits");
    case GameState::Puzzle3:
      return F("Puzzle 3 — Button Sequence");
    case GameState::MissionComplete:
      return F("Mission Complete");
    default:
      return F("Unknown");
  }
}

void triggerLatch() {
  if (latchTriggered) {
    return;
  }
  latchTriggered = true;
  Serial.println(F("[Latch] Servo/solenoid triggered to release tacklebox bottom."));
}

void resetSequenceTracking() {
  nextSequenceIndex = 0;
}

void clearSequenceError() {
  sequenceError = false;
  sequenceErrorExpiresAt = 0;
}

void markSequenceError() {
  sequenceError = true;
  sequenceErrorExpiresAt = millis() + SEQUENCE_ERROR_FLASH_MS;
}

bool isSequenceErrorActive() {
  if (!sequenceError) {
    return false;
  }
  if ((long)(millis() - sequenceErrorExpiresAt) >= 0) {
    clearSequenceError();
    return false;
  }
  return true;
}

String buildSequenceStatusHtml() {
  String html;
  html.reserve(512);
  html += F("<div class='sequence-status'>");
  if (nextSequenceIndex < BUTTON_SEQUENCE_LENGTH) {
    html += F("<div class='current-step'><span>Next Input</span><strong>");
    html += String(BUTTON_SEQUENCE[nextSequenceIndex]);
    html += F("</strong></div>");
  } else {
    html += F("<div class='current-step'><span>Next Input</span><strong>✓</strong></div>");
  }
  html += F("<div class='sequence-row'>");
  for (size_t i = 0; i < BUTTON_SEQUENCE_LENGTH; ++i) {
    const char* stateClass = "pending";
    if (i < nextSequenceIndex) {
      stateClass = "done";
    } else if (i == nextSequenceIndex) {
      stateClass = "active";
    }
    html += "<span class='seq-step ";
    html += stateClass;
    html += "'>";
    html += String(BUTTON_SEQUENCE[i]);
    html += "</span>";
  }
  html += F("</div>");
  html += F("<p class='sequence-note'>Pattern: 4 1 5 1 3 5 4 2 1 3 2 4 5 3 1</p>");
  html += F("</div>");
  return html;
}

void resetGame() {
  currentState = GameState::Puzzle1;
  latchTriggered = false;
  conduitsVerified = false;
  clearSequenceError();
  resetSequenceTracking();
  Serial.println(F("[Game] Reset to Puzzle 1."));
}

void completeMission() {
  currentState = GameState::MissionComplete;
  clearSequenceError();
  triggerLatch();
  Serial.println(F("[Game] Mission Complete triggered."));
}

void advanceToPuzzle(GameState target) {
  if (currentState == GameState::MissionComplete) {
    Serial.println(F("[Game] Already complete. Ignoring advance request."));
    return;
  }

  if (target == GameState::Puzzle2 && currentState == GameState::Puzzle1) {
    currentState = GameState::Puzzle2;
    conduitsVerified = false;
    clearSequenceError();
    Serial.println(F("[Game] Advanced to Puzzle 2."));
    return;
  }

  if (target == GameState::Puzzle3 && currentState == GameState::Puzzle2) {
    currentState = GameState::Puzzle3;
    clearSequenceError();
    resetSequenceTracking();
    Serial.println(F("[Game] Advanced to Puzzle 3. Sequence tracking reset."));
    return;
  }

  if (target == GameState::MissionComplete && currentState == GameState::Puzzle3) {
    completeMission();
    return;
  }

  Serial.println(F("[Game] Invalid state transition requested."));
}

void handleRemoteButton(char button) {
  switch (button) {
    case 'A':
    case 'a':
      Serial.println(F("[Remote] Button A pressed."));
      advanceToPuzzle(GameState::Puzzle2);
      break;
    case 'B':
    case 'b':
      Serial.println(F("[Remote] Button B pressed."));
      advanceToPuzzle(GameState::Puzzle3);
      break;
    case 'C':
    case 'c':
      Serial.println(F("[Remote] Button C pressed. Resetting game."));
      resetGame();
      break;
    case 'D':
    case 'd':
      Serial.println(F("[Remote] Button D pressed. Forcing completion."));
      completeMission();
      break;
    default:
      Serial.println(F("[Remote] Unknown button."));
      break;
  }
}

void registerButtonPress(uint8_t buttonId) {
  if (currentState != GameState::Puzzle3) {
    Serial.println(F("[Buttons] Ignored press outside Puzzle 3."));
    return;
  }

  Serial.printf("[Buttons] Received button %u\n", buttonId);

  uint8_t expected = BUTTON_SEQUENCE[nextSequenceIndex];
  if (buttonId == expected) {
    clearSequenceError();
    nextSequenceIndex++;
    Serial.printf("[Buttons] Progress %u/%u\n", static_cast<unsigned>(nextSequenceIndex),
                  static_cast<unsigned>(BUTTON_SEQUENCE_LENGTH));
    if (nextSequenceIndex >= BUTTON_SEQUENCE_LENGTH) {
      completeMission();
    }
  } else {
    Serial.printf("[Buttons] Incorrect input (expected %u). Sequence reset.\n", expected);
    markSequenceError();
    resetSequenceTracking();
  }
}

ConduitConfirmResult confirmConduitsAligned() {
  if (currentState != GameState::Puzzle2) {
    Serial.println(F("[Conduits] Confirmation ignored (not in Puzzle 2)."));
    return ConduitConfirmResult::WrongState;
  }
  if (conduitsVerified) {
    Serial.println(F("[Conduits] Already verified."));
    return ConduitConfirmResult::AlreadyConfirmed;
  }
  conduitsVerified = true;
  Serial.println(F("[Conduits] GM confirmed power conduits. Code 264 unlocked."));
  return ConduitConfirmResult::Accepted;
}

String buildDcdPage() {
  String page = F(
      "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' "
      "content='width=device-width,initial-scale=1'>"
      "<title>Mission Control DCD</title>"
      "<style>"
      "body{font-family:'Segoe UI',sans-serif;background:#030712;color:#f8fafc;margin:0;padding:2rem;"
      "min-height:100vh;overflow:hidden;position:relative;display:flex;align-items:center;justify-content:center;}"
      ".warp-field{position:fixed;top:0;left:0;width:100%;height:100%;overflow:hidden;z-index:0;"
      "background:radial-gradient(circle at top,#0f172a 0%,#01030a 65%,#000103 100%);}"
      ".warp-line{position:absolute;width:2px;height:140px;background:linear-gradient(180deg,rgba(59,130,246,0),"
      "rgba(59,130,246,.6),rgba(59,130,246,0));filter:blur(0.3px);animation:warpSlide 2.8s linear infinite;"
      "opacity:.25;}"
      ".warp-line:nth-child(3n){animation-duration:3.4s;opacity:.35;width:3px;}"
      ".warp-line:nth-child(5n){animation-duration:2.1s;opacity:.2;height:180px;}"
      "@keyframes warpSlide{0%{transform:translate3d(0,-150%,0);}100%{transform:translate3d(0,150%,0);}}"
      ".panel{position:relative;z-index:1;max-width:720px;width:100%;background:rgba(15,23,42,.9);padding:2rem;"
      "border:1px solid rgba(148,163,184,.4);border-radius:8px;box-shadow:0 15px 35px rgba(0,0,0,.4);}"
      "h1{margin-top:0;font-weight:600;letter-spacing:.08em;text-transform:uppercase;font-size:1rem;color:#94a3b8;}"
      "h2{margin-bottom:.5rem;color:#e0f2fe;}p{line-height:1.6;} .callout{font-size:2.5rem;font-weight:700;"
      "letter-spacing:.3rem;text-align:center;margin:1rem auto;padding:.5rem;border:1px solid #38bdf8;"
      "border-radius:4px;color:#38bdf8;} .success{color:#4ade80;font-weight:600;}"
      ".transmission{margin:1.5rem 0;padding:1rem;border:1px solid rgba(148,163,184,.4);border-radius:6px;"
      "background:rgba(2,6,23,.8);} .transmission h3{margin-top:0;color:#bae6fd;text-transform:uppercase;"
      "letter-spacing:.1em;font-size:.85rem;} .transmission pre{background:#020617;padding:.8rem;border-radius:4px;"
      "font-size:1.1rem;line-height:1.4;overflow:auto;} .hint{color:#94a3b8;font-style:italic;margin:.8rem 0;}"
      ".cards{margin:0;padding-left:1.2rem;} .cards li{margin:.35rem 0;}"
      ".sequence-status{margin:1.5rem 0;padding:1rem;border:1px solid rgba(148,163,184,.4);border-radius:6px;"
      "background:rgba(15,23,42,.7);} .current-step{display:flex;justify-content:space-between;align-items:center;"
      "font-size:1.2rem;margin-bottom:1rem;} .current-step span{text-transform:uppercase;font-size:.75rem;"
      "letter-spacing:.1em;color:#94a3b8;} .current-step strong{font-size:2.5rem;color:#fbbf24;"
      "font-weight:700;letter-spacing:.2em;} .sequence-row{display:flex;flex-wrap:wrap;gap:.35rem;}"
      ".seq-step{width:2.2rem;height:2.2rem;border-radius:4px;display:flex;align-items:center;justify-content:center;"
      "font-weight:600;font-size:1.1rem;border:1px solid rgba(148,163,184,.4);} .seq-step.done{background:#1d4ed8;"
      "border-color:#2563eb;color:#e0f2fe;} .seq-step.active{background:#fbbf24;border-color:#f59e0b;color:#0f172a;"
      "transform:scale(1.1);} .seq-step.pending{background:rgba(15,23,42,.8);color:#94a3b8;}"
      ".sequence-note{margin-top:.75rem;font-size:.85rem;color:#94a3b8;letter-spacing:.05em;}"
      ".alert{margin-top:1rem;padding:.75rem;border-radius:6px;border:1px solid #fecaca;color:#fee2e2;"
      "background:#7f1d1d;} .flash{animation:flashError .35s alternate 6;} @keyframes flashError{from{background:#7f1d1d;}"
      "to{background:#b91c1c;}}"
      ".flash-banner{margin:1rem 0;padding:.75rem;border-radius:6px;border:1px solid rgba(56,189,248,.8);"
      "text-align:center;font-weight:700;letter-spacing:.15em;color:#e0f2fe;background:rgba(14,165,233,.15);"
      "animation:flashPulse .65s ease-in-out infinite alternate;box-shadow:0 0 12px rgba(56,189,248,.35);}"
      "@keyframes flashPulse{from{background:rgba(14,165,233,.15);color:#bae6fd;}to{background:rgba(14,165,233,.35);"
      "color:#f0f9ff;box-shadow:0 0 22px rgba(56,189,248,.6);}}"
      ".status-bar{margin-top:1rem;font-size:.8rem;color:#94a3b8;}"
      "</style></head><body>"
      "<div class='warp-field'>"
      "<div class='warp-line' style='left:5%;animation-delay:-1s'></div>"
      "<div class='warp-line' style='left:12%;animation-delay:-2.2s'></div>"
      "<div class='warp-line' style='left:22%;animation-delay:-.4s'></div>"
      "<div class='warp-line' style='left:33%;animation-delay:-1.6s'></div>"
      "<div class='warp-line' style='left:45%;animation-delay:-2.8s'></div>"
      "<div class='warp-line' style='left:57%;animation-delay:-.9s'></div>"
      "<div class='warp-line' style='left:66%;animation-delay:-2.1s'></div>"
      "<div class='warp-line' style='left:74%;animation-delay:-.2s'></div>"
      "<div class='warp-line' style='left:83%;animation-delay:-1.3s'></div>"
      "<div class='warp-line' style='left:92%;animation-delay:-2.6s'></div>"
      "</div>"
      "<div class='panel'><h1>Mission Control</h1>"
      "<div id='dcd-content'>");
  page += storyTextForState();
  page += F("</div><div class='status-bar' id='sync-status'>Live link established.</div></div>"
           "<script>"
           "const statusEl=document.getElementById('sync-status');"
           "const contentEl=document.getElementById('dcd-content');"
           "async function refreshContent(){"
           "try{const resp=await fetch('/dcd-fragment',{cache:'no-store'});"
           "if(!resp.ok){throw new Error('HTTP '+resp.status);}"
           "const html=await resp.text();"
           "contentEl.innerHTML=html;"
           "statusEl.textContent='Link stable • '+new Date().toLocaleTimeString();"
           "}catch(err){statusEl.textContent='Link unstable: '+err;}}"
           "refreshContent();"
           "setInterval(refreshContent,700);"
           "</script>"
           "</body></html>");
  return page;
}

String buildControlPanelPage() {
  String page = F(
      "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' "
      "content='width=device-width,initial-scale=1'>"
      "<title>GM Control Panel</title>"
      "<style>"
      "body{font-family:'Segoe UI',sans-serif;background:#030712;color:#e2e8f0;margin:0;padding:2rem;"
      "min-height:100vh;position:relative;overflow:hidden;display:flex;align-items:center;justify-content:center;}"
      ".warp-field{position:fixed;top:0;left:0;width:100%;height:100%;overflow:hidden;z-index:0;"
      "background:radial-gradient(circle at top,#0f172a 0%,#01030a 65%,#000103 100%);}"
      ".warp-line{position:absolute;width:2px;height:140px;background:linear-gradient(180deg,rgba(59,130,246,0),"
      "rgba(59,130,246,.6),rgba(59,130,246,0));filter:blur(0.3px);animation:warpSlide 2.8s linear infinite;"
      "opacity:.25;}"
      ".warp-line:nth-child(3n){animation-duration:3.4s;opacity:.35;width:3px;}"
      ".warp-line:nth-child(5n){animation-duration:2.1s;opacity:.2;height:180px;}"
      "@keyframes warpSlide{0%{transform:translate3d(0,-150%,0);}100%{transform:translate3d(0,150%,0);}}"
      ".content{position:relative;z-index:1;width:100%;max-width:1100px;}"
      ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:1rem;}"
      ".card{background:#1e293b;padding:1rem;border-radius:8px;border:1px solid rgba(148,163,184,.3);}"
      "button{width:100%;padding:.8rem;border:none;border-radius:6px;font-size:1rem;font-weight:600;"
      "cursor:pointer;margin-top:.5rem;}button.remote{background:#38bdf8;color:#0f172a;}"
      "button.remote:nth-of-type(2){background:#fb7185;}button.remote:nth-of-type(3){background:#fbbf24;}"
      "button.remote:nth-of-type(4){background:#22c55e;}button.puzzle{background:#94a3b8;color:#0f172a;margin:.25rem 0;}"
      "button.action{background:#4ade80;color:#0f172a;}"
      ".status{margin-top:1rem;padding:.5rem;border-radius:6px;background:#0f172a;border:1px solid #334155;"
      "font-family:monospace;} a{color:#38bdf8;}"
      "</style></head><body>"
      "<div class='warp-field'>"
      "<div class='warp-line' style='left:8%;animation-delay:-1.4s'></div>"
      "<div class='warp-line' style='left:16%;animation-delay:-.6s'></div>"
      "<div class='warp-line' style='left:28%;animation-delay:-2.1s'></div>"
      "<div class='warp-line' style='left:37%;animation-delay:-.3s'></div>"
      "<div class='warp-line' style='left:49%;animation-delay:-1.7s'></div>"
      "<div class='warp-line' style='left:61%;animation-delay:-2.8s'></div>"
      "<div class='warp-line' style='left:72%;animation-delay:-.8s'></div>"
      "<div class='warp-line' style='left:84%;animation-delay:-2.3s'></div>"
      "<div class='warp-line' style='left:93%;animation-delay:-.2s'></div>"
      "</div>"
      "<div style='position:relative;z-index:1;'>"
      "<h1>GM Control Panel</h1>"
      "<p>Current state: <strong>");
  page += gameStateLabel();
  page += F("</strong></p><div class='grid'>"
            "<div class='card'><h2>GM Remote</h2>"
            "<button class='remote' onclick=\"sendAction('/remote?btn=A')\">Remote A (Puzzle 1 → 2)</button>"
            "<button class='remote' onclick=\"sendAction('/remote?btn=B')\">Remote B (Puzzle 2 → 3)</button>"
            "<button class='remote' onclick=\"sendAction('/remote?btn=C')\">Remote C (Reset)</button>"
            "<button class='remote' onclick=\"sendAction('/remote?btn=D')\">Remote D (Force Complete)</button>"
            "</div>"
            "<div class='card'><h2>Puzzle Buttons</h2>"
            "<p>Simulate wired + wireless button presses while in Puzzle 3.</p>");
  for (uint8_t button = 1; button <= 5; ++button) {
    page += "<button class='puzzle' onclick=\"sendAction('/puzzle-button?id=" + String(button) +
            "')\">Button " + String(button) + "</button>";
  }
  page += F("</div>"
            "<div class='card'><h2>Puzzle 2 Tools</h2>"
            "<p>Use after visually confirming players aligned every conduit correctly.</p>"
            "<button class='action' onclick=\"sendAction('/confirm-conduits')\">Confirm Conduits Aligned</button>"
            "</div></div>"
            "<div class='status' id='status'>Status log will appear here.</div>"
            "<script>"
            "async function sendAction(path){const status=document.getElementById('status');"
            "status.textContent='Sending '+path+' ...';"
        "try{const resp=await fetch(path);const text=await resp.text();"
        "status.textContent=text;}catch(err){status.textContent='Error: '+err;}}"
        "</script>"
            "<p><a href='/'>View DCD display</a></p></div></body></html>");
  return page;
}

void sendBadRequest(const String& message) {
  server.send(400, "text/plain", "Bad request: " + message);
}

void handleRoot() {
  server.send(200, "text/html", buildDcdPage());
}

void handleDcdFragment() {
  server.send(200, "text/html", storyTextForState());
}

void handleControlPanel() {
  server.send(200, "text/html", buildControlPanelPage());
}

void handleRemoteEndpoint() {
  if (!server.hasArg("btn") || server.arg("btn").isEmpty()) {
    sendBadRequest(F("missing btn parameter"));
    return;
  }
  char btn = server.arg("btn").charAt(0);
  handleRemoteButton(btn);
  server.send(200, "text/plain", String("Remote input accepted: ") + btn);
}

void handlePuzzleButtonEndpoint() {
  if (!server.hasArg("id") || server.arg("id").isEmpty()) {
    sendBadRequest(F("missing id parameter"));
    return;
  }
  int value = server.arg("id").toInt();
  if (value < 1 || value > 5) {
    sendBadRequest(F("button id must be 1-5"));
    return;
  }
  registerButtonPress(static_cast<uint8_t>(value));
  server.send(200, "text/plain", String("Button press registered: ") + value);
}

void handleConfirmConduitsEndpoint() {
  ConduitConfirmResult result = confirmConduitsAligned();
  switch (result) {
    case ConduitConfirmResult::Accepted:
      server.send(200, "text/plain", "Conduits confirmed. Code 264 unlocked.");
      break;
    case ConduitConfirmResult::AlreadyConfirmed:
      server.send(200, "text/plain", "Conduits already verified.");
      break;
    case ConduitConfirmResult::WrongState:
    default:
      server.send(200, "text/plain", "Conduit confirmation ignored. Not in Puzzle 2.");
      break;
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "Endpoint not found");
}

void configureRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/dcd-fragment", HTTP_GET, handleDcdFragment);
  server.on("/control", HTTP_GET, handleControlPanel);
  server.on("/remote", HTTP_GET, handleRemoteEndpoint);
  server.on("/puzzle-button", HTTP_GET, handlePuzzleButtonEndpoint);
  server.on("/confirm-conduits", HTTP_GET, handleConfirmConduitsEndpoint);
  server.onNotFound(handleNotFound);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("Mission Control Hub booting..."));

  WiFi.mode(WIFI_AP);
  if (WiFi.softAP(HUB_SSID, HUB_PASSWORD, HUB_CHANNEL)) {
    Serial.print(F("[WiFi] Access point ready: "));
    Serial.println(HUB_SSID);
    Serial.print(F("[WiFi] IP address: "));
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println(F("[WiFi] Failed to start access point."));
  }

  configureRoutes();
  server.begin();
  Serial.println(F("[Server] HTTP server started on port 80."));
}

void loop() {
  server.handleClient();
}
