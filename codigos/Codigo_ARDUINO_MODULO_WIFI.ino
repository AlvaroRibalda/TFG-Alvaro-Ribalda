#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>

// --- credenciales (rellenar antes de subir) ---
const char* ssid     = "TU_SSID";
const char* password = "TU_PASSWORD";
const char* apiKey   = "TU_WRITE_API_KEY";
const char* webUser  = "TU_USUARIO_PANEL";
const char* webPass  = "TU_PASSWORD_PANEL";

// enlace serie por software con el Mega (pines D7/D8)
SoftwareSerial megaSerial(D7, D8);
ESP8266WebServer webServer(80);
WiFiClient client;

unsigned long ultimoEnvio = 0;
const unsigned long INTERVALO = 16000; // margen sobre el limite de 15s de ThingSpeak
String ultimoEstado = "PARADO";
String ultimoPedido = "--";
int pedidosEnColaMega = 0;
bool estadoPendienteEnvio = false;

// cola FIFO de resultados pendientes de subir a ThingSpeak
#define MAX_COLA_ENVIO 10
String colaEnvio[MAX_COLA_ENVIO];
int colaEntrada = 0;
int colaSalida  = 0;
int colaCount   = 0;

bool encolarDato(String dato) {
  if (colaCount >= MAX_COLA_ENVIO) return false;
  colaEnvio[colaEntrada] = dato;
  colaEntrada = (colaEntrada + 1) % MAX_COLA_ENVIO;
  colaCount++;
  return true;
}

bool hayDatosEnCola() { return colaCount > 0; }

String extraerSiguienteDeCola() {
  String dato = colaEnvio[colaSalida];
  colaSalida = (colaSalida + 1) % MAX_COLA_ENVIO;
  colaCount--;
  return dato;
}

// panel web servido en HTML embebido
const char htmlPanel[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SDX200 - Panel de control</title>
<style>
  :root {
    --bg:#F3F5F4;--surface:#FFFFFF;--surface-2:#F7F9F8;
    --border:#E2E7E5;--text:#1B2622;--text-dim:#6B7670;
    --text-faint:#9AA39E;--teal:#0D7268;--teal-light:#E3F1EE;
    --verde:#1FA579;--rojo:#DC4444;
    --sans:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;
  }
  *{box-sizing:border-box;margin:0;padding:0;}
  body{background:var(--bg);color:var(--text);font-family:var(--sans);padding:24px 16px;}
  .panel{width:100%;max-width:380px;margin:0 auto;}
  .header{display:flex;align-items:center;justify-content:space-between;margin-bottom:20px;}
  .brand{display:flex;align-items:center;gap:10px;}
  .logo{width:40px;height:40px;border-radius:10px;background:var(--teal);display:flex;align-items:center;justify-content:center;}
  .logo svg{width:22px;height:22px;}
  h1{font-size:17px;font-weight:700;}
  .sub{font-size:11px;color:var(--text-faint);margin-top:1px;}
  .conn{display:flex;align-items:center;gap:6px;background:var(--surface);border:1px solid var(--border);border-radius:20px;padding:5px 11px 5px 8px;}
  .dot{width:7px;height:7px;border-radius:50%;background:var(--verde);animation:pulse 2s infinite;}
  @keyframes pulse{0%{box-shadow:0 0 0 0 rgba(31,165,121,.45)}70%{box-shadow:0 0 0 5px rgba(31,165,121,0)}100%{box-shadow:0 0 0 0 rgba(31,165,121,0)}}
  .conn span{font-size:11px;font-weight:600;color:var(--text-dim);}
  .status{background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:14px 16px;margin-bottom:16px;display:flex;justify-content:space-between;align-items:center;}
  .status .label{font-size:10px;text-transform:uppercase;letter-spacing:.06em;color:var(--text-faint);font-weight:600;margin-bottom:4px;}
  .status .value{font-size:15px;font-weight:700;}
  .status .meta{font-size:13px;font-weight:600;color:var(--text-dim);background:var(--surface-2);padding:4px 9px;border-radius:7px;}
  .card{background:var(--surface);border:1px solid var(--border);border-radius:16px;padding:20px;}
  .card h2{font-size:12px;text-transform:uppercase;letter-spacing:.06em;color:var(--text-faint);font-weight:700;margin-bottom:16px;}
  .field{margin-bottom:14px;}
  .field label{display:block;font-size:12.5px;font-weight:600;color:var(--text-dim);margin-bottom:6px;}
  .row-2{display:grid;grid-template-columns:1fr 1fr;gap:12px;}
  select,input{width:100%;background:var(--surface-2);border:1.5px solid var(--border);color:var(--text);font-family:var(--sans);font-size:14px;font-weight:500;padding:10px 12px;border-radius:10px;appearance:none;}
  select:focus,input:focus{outline:none;border-color:var(--teal);}
  button{width:100%;background:var(--teal);color:#fff;border:none;font-family:var(--sans);font-size:14px;font-weight:700;padding:13px;border-radius:11px;margin-top:6px;cursor:pointer;}
  button:active{transform:scale(.98);}
  .feedback{font-size:13px;font-weight:600;text-align:center;min-height:16px;margin-top:12px;}
  .feedback.ok{color:var(--verde);}
  .feedback.error{color:var(--rojo);}
  .queue{border-top:1px solid var(--border);margin-top:16px;padding-top:14px;}
  .queue-row{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;}
  .queue-row:last-child{margin-bottom:0;}
  .queue-row span{font-size:12px;color:var(--text-dim);font-weight:500;}
  .queue-row strong{font-size:13px;color:var(--teal);background:var(--teal-light);padding:3px 9px;border-radius:7px;}
  footer{text-align:center;font-size:11px;color:var(--text-faint);margin-top:20px;}
</style>
</head>
<body>
<div class="panel">
  <div class="header">
    <div class="brand">
      <div class="logo">
        <svg viewBox="0 0 32 32" fill="none">
          <circle cx="16" cy="16" r="14" stroke="#fff" stroke-width="2" opacity=".25"/>
          <path d="M9 16h6" stroke="#fff" stroke-width="2.5" stroke-linecap="round"/>
          <path d="M15 16l6-6" stroke="#fff" stroke-width="2.5" stroke-linecap="round"/>
          <path d="M15 16l6 6" stroke="#fff" stroke-width="2.5" stroke-linecap="round"/>
          <circle cx="9" cy="16" r="2" fill="#fff"/>
        </svg>
      </div>
      <div>
        <h1>SDX200</h1>
        <p class="sub">Panel de control</p>
      </div>
    </div>
    <div class="conn"><span class="dot"></span><span>En linea</span></div>
  </div>
  <div class="status">
    <div>
      <p class="label">Estado del sistema</p>
      <p class="value" id="estadoVal">Cargando...</p>
    </div>
    <div class="meta" id="pedidoVal">--</div>
  </div>
  <div class="card">
    <h2>Nuevo pedido</h2>
    <div class="field">
      <label>Cliente</label>
      <select id="cliente">
        <option value="ATM">ATM - AutoTech Motors</option>
        <option value="VRX">VRX - Vertex Automotive</option>
        <option value="NDS">NDS - NorthDrive Systems</option>
        <option value="SLC">SLC - Solana Componentes</option>
      </select>
    </div>
    <div class="row-2">
      <div class="field">
        <label>Variante</label>
        <select id="variante">
          <option value="NA">NA</option>
          <option value="EU">EU</option>
        </select>
      </div>
      <div class="field">
        <label>Cantidad</label>
        <input type="number" id="cantidad" value="8" min="1" max="999">
      </div>
    </div>
    <button onclick="enviarPedido()">Enviar pedido</button>
    <div class="feedback" id="feedback"></div>
    <div class="queue">
      <div class="queue-row">
        <span>Pedidos en cola (linea)</span>
        <strong id="colaPedidos">0 / 5</strong>
      </div>
      <div class="queue-row">
        <span>Envios pendientes a la nube</span>
        <strong id="colaEnvio">0</strong>
      </div>
    </div>
  </div>
  <footer>TFG - Sistema de clasificacion automatizada</footer>
</div>
<script>
function enviarPedido() {
  var c = document.getElementById('cliente').value;
  var v = document.getElementById('variante').value;
  var q = document.getElementById('cantidad').value;
  var fb = document.getElementById('feedback');
  if (!q || q < 1) { fb.textContent='Error: cantidad no valida'; fb.className='feedback error'; return; }
  fetch('/orden?cliente='+c+'&variante='+v+'&cantidad='+q)
    .then(r=>r.text())
    .then(t=>{ fb.textContent=t; fb.className='feedback ok'; })
    .catch(()=>{ fb.textContent='Error de conexion'; fb.className='feedback error'; });
}
function actualizarEstado() {
  fetch('/estado')
    .then(r=>r.json())
    .then(d=>{
      document.getElementById('estadoVal').textContent=d.estado||'--';
      document.getElementById('pedidoVal').textContent=d.pedido||'--';
      document.getElementById('colaPedidos').textContent=(d.pedidos_cola!==undefined?d.pedidos_cola:'--')+' / 5';
      document.getElementById('colaEnvio').textContent=d.cola_envio!==undefined?d.cola_envio:'--';
    }).catch(()=>{});
}
setInterval(actualizarEstado, 10000);
actualizarEstado();
</script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  megaSerial.begin(4800);

  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Conectado! IP: ");
  Serial.println(WiFi.localIP());

  // ruta principal, sirve el panel HTML
  webServer.on("/", []() {
    if (!webServer.authenticate(webUser, webPass)) return webServer.requestAuthentication();
    webServer.send(200, "text/html", htmlPanel);
  });

  // recibe el pedido del panel y lo reenvia al Mega como comando ORDEN
  webServer.on("/orden", []() {
    if (!webServer.authenticate(webUser, webPass)) return webServer.requestAuthentication();
    if (webServer.hasArg("cliente") && webServer.hasArg("variante") && webServer.hasArg("cantidad")) {
      String orden = "ORDEN " + webServer.arg("cliente") + " " + webServer.arg("variante") + " " + webServer.arg("cantidad");
      megaSerial.println(orden);
      Serial.println("Enviado al Mega: " + orden);
      webServer.send(200, "text/plain", "Pedido enviado: " + webServer.arg("cliente") + " " + webServer.arg("variante") + " x" + webServer.arg("cantidad"));
    } else {
      webServer.send(400, "text/plain", "Faltan parametros");
    }
  });

  // el panel consulta esto cada 10s para refrescar estado y colas
  webServer.on("/estado", []() {
    if (!webServer.authenticate(webUser, webPass)) return webServer.requestAuthentication();
    String json = "{\"estado\":\"" + ultimoEstado + "\",\"pedido\":\"" + ultimoPedido +
                  "\",\"pedidos_cola\":" + String(pedidosEnColaMega) +
                  ",\"cola_envio\":" + String(colaCount) + "}";
    webServer.send(200, "application/json", json);
  });

  webServer.begin();
  Serial.println("Servidor web en: http://" + WiFi.localIP().toString());
}

void loop() {
  webServer.handleClient();
  recibirDatosMega();
  enviarAThingSpeak();
}

void recibirDatosMega() {
  if (megaSerial.available()) {
    String dato = megaSerial.readStringUntil('\n');
    dato.trim();
    if (dato.length() < 3) return;

    if (dato.startsWith("ESTADO,")) {
      String nuevoEstado = dato.substring(7);
      if (nuevoEstado != ultimoEstado) {
        ultimoEstado = nuevoEstado;
        estadoPendienteEnvio = true;
      }
      return;
    }

    if (dato.startsWith("COLA,")) {
      pedidosEnColaMega = dato.substring(5).toInt();
      return;
    }

    // resultado de pieza, viene como CSV sin prefijo
    if (dato.length() > 5 && dato.indexOf(',') > 0) {
      int c1 = dato.indexOf(',');
      int c2 = dato.indexOf(',', c1 + 1);
      if (c1 > 0 && c2 > 0) {
        ultimoPedido = dato.substring(0, c1) + " " + dato.substring(c1 + 1, c2);
      }
      ultimoEstado = "EN_MARCHA";
      if (!encolarDato(dato)) {
        Serial.println("Cola de envio llena, dato perdido: " + dato);
      }
    }
  }
}

void enviarAThingSpeak() {
  if (!hayDatosEnCola() && !estadoPendienteEnvio) return;
  if (millis() - ultimoEnvio < INTERVALO) return;
  if (WiFi.status() != WL_CONNECTED) return;

  if (hayDatosEnCola()) {
    enviarResultadoPieza();
  } else {
    enviarSoloEstado();
  }
  ultimoEnvio = millis();
}

void enviarResultadoPieza() {
  String dato = extraerSiguienteDeCola();

  int c1 = dato.indexOf(',');
  int c2 = dato.indexOf(',', c1 + 1);
  int c3 = dato.indexOf(',', c2 + 1);
  int c4 = dato.indexOf(',', c3 + 1);
  int c5 = dato.indexOf(',', c4 + 1);
  int c6 = dato.indexOf(',', c5 + 1);

  if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0 || c5 < 0 || c6 < 0) {
    Serial.println("Dato mal formado, descartado: " + dato);
    return;
  }

  String cliente   = dato.substring(0, c1);
  String variante  = dato.substring(c1 + 1, c2);
  String resultado = dato.substring(c2 + 1, c3);
  String motivo    = dato.substring(c3 + 1, c4);
  String serie     = dato.substring(c4 + 1, c5);
  String cPedido   = dato.substring(c5 + 1, c6);
  String cTotal    = dato.substring(c6 + 1);
  int resultadoNum = (resultado == "OK") ? 1 : 0;

  // "/" se codifica para poder ir en la URL
  String cPedidoCodificado = cPedido;
  cPedidoCodificado.replace("/", "%2F");

  String url = "http://api.thingspeak.com/update?api_key=";
  url += apiKey;
  url += "&field1=" + variante;
  url += "&field2=" + cliente;
  url += "&field3=" + String(resultadoNum);
  url += "&field4=" + motivo;
  url += "&field5=" + serie;
  url += "&field6=" + cPedidoCodificado;
  url += "&field7=" + cTotal;
  url += "&field8=" + ultimoEstado;

  HTTPClient http;
  http.begin(client, url);
  int httpCode = http.GET();
  Serial.print("ThingSpeak (pieza) HTTP " + String(httpCode) + ": ");
  Serial.println(http.getString());
  http.end();

  estadoPendienteEnvio = false;
}

void enviarSoloEstado() {
  String url = "http://api.thingspeak.com/update?api_key=";
  url += apiKey;
  url += "&field8=" + ultimoEstado;

  HTTPClient http;
  http.begin(client, url);
  int httpCode = http.GET();
  Serial.print("ThingSpeak (estado) HTTP " + String(httpCode) + ": ");
  Serial.println(http.getString());
  http.end();

  estadoPendienteEnvio = false;
}
