#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- Pines ---
#define PIN_S1 22
#define PIN_S2 23
#define PIN_S3 24
#define PIN_S4 25
#define PIN_S5 26

#define PIN_SERVO_TRACCION 9
#define PIN_SERVO_CLASIF_A 10
#define PIN_SERVO_CLASIF_B 11

#define PIN_LED_ROJO 5
#define PIN_LED_VERDE 6
#define PIN_LED_NARANJA 7
#define PIN_BUZZER 8

#define PIN_BOTON_ESTOP 2
#define PIN_BOTON_REARME 3
#define PIN_BOTON_ARRANQUE 4
#define PIN_BOTON_PARO 14

// --- Parametros de actuacion ---
#define VELOCIDAD_PARADA 1500
#define VELOCIDAD_TRANSITO 1200
#define VELOCIDAD_LECTURA 1300
#define ANGULO_REPOSO 180
#define ANGULO_DESVIO 0

#define TIMEOUT_LECTURA_QR 9000
#define TIMEOUT_CONFIRMACION 4000
#define MAX_ORDENES 5
#define DEBOUNCE_MS 250
#define LONGITUD_MINIMA_QR 20
#define DURACION_CONFIRMACION_QR 1500

// --- Tiempos de transito hasta cada actuador ---
unsigned long TIEMPO_HASTA_ACTUADOR_A = 1000;  
unsigned long TIEMPO_HASTA_ACTUADOR_B = 4000;  

// --- Objetos de hardware ---
Servo servoTraccion, servoA, servoB;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- Maquinas de estados ---
enum EstadoSistema {
  ESPERA_ORDEN, ESPERA_PRODUCTO, TRANSITO, LECTURA_QR,
  CLASIFICANDO, PEDIDO_COMPLETADO
};
enum EstadoMarchaParo { SISTEMA_PARADO, SISTEMA_EN_MARCHA, SISTEMA_EMERGENCIA };

EstadoSistema estadoActual = ESPERA_ORDEN;
EstadoMarchaParo estadoMarchaParo = SISTEMA_PARADO;

struct Pedido { String cliente; String variante; int cantidad; };
Pedido colaPedidos[MAX_ORDENES];
int indiceEntrada = 0, indiceSalida = 0, pedidosEnCola = 0;

String clienteActual  = "";
String variantePedido = "";
String varianteLeida  = "";
String serieLeida     = "";
int cantidadPedido = 0, unidadesProcesadas = 0;
char lineaDestino = 'A';

unsigned long tiempoInicioEstado = 0;
bool solicitudParo    = false;
bool errorActivo      = false;
bool lecturaInvalida  = false;
bool actuadorActivado = false;
String motivoErrorActual = "";

bool mostrandoConfirmacionQR = false;
unsigned long tiempoInicioConfirmacionQR = 0;
bool resultadoLecturaOK = false;

bool buzzerErrorActivo = false;
unsigned long tiempoUltimoBeepError = 0;
const unsigned long INTERVALO_BEEP_ERROR = 700;

volatile bool solicitudEmergencia = false;
volatile bool solicitudRearmeFlag = false;
volatile unsigned long ultimaEmergencia = 0;
volatile unsigned long ultimoRearme = 0;

int repeticionBuzzerActual = 0;
int repeticionesBuzzerTotal = 0;
unsigned long tiempoSiguienteBuzzer = 0;
int frecuenciaBuzzerActual = 0;
int duracionToneBuzzer = 0;
int duracionPausaBuzzer = 0;

bool ledNaranjaParpadeando = false;
unsigned long tiempoSiguienteParpadeoLed = 0;
bool estadoLedNaranjaParpadeo = false;
const unsigned long INTERVALO_PARPADEO_LED = 400;

void activarEmergencia() {
  if (millis() - ultimaEmergencia > DEBOUNCE_MS) {
    solicitudEmergencia = true;
    ultimaEmergencia = millis();
  }
}

void gestionarRearme() {
  if (millis() - ultimoRearme > DEBOUNCE_MS) {
    solicitudRearmeFlag = true;
    ultimoRearme = millis();
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);
  Serial2.begin(4800);

  Serial.setTimeout(50);
  Serial1.setTimeout(50);

  inicializarPines();
  inicializarServos();
  lcd.init();
  lcd.backlight();

  attachInterrupt(digitalPinToInterrupt(PIN_BOTON_ESTOP), activarEmergencia, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BOTON_REARME), gestionarRearme, FALLING);

  mostrarLCD("Sistema apagado", "Pulse ARRANQUE");
  Serial.println("Sistema iniciado. Pulsa ARRANQUE.");
  Serial.println("Introduce pedidos con: ORDEN <cliente> <variante> <cantidad>");
}

void loop() {
  procesarEmergencia();
  procesarRearme();
  leerPulsadorArranque();
  leerPulsadorParo();
  leerComandoSerie();
  actualizarBuzzer();
  actualizarBuzzerError();
  actualizarParpadeoLed();
  actualizarConfirmacionQR();

  if (estadoMarchaParo != SISTEMA_EN_MARCHA) return;

  comprobarErrores();

  if (errorActivo) {
    servoTraccion.writeMicroseconds(VELOCIDAD_TRANSITO);
    return;
  }

  switch (estadoActual) {
    case ESPERA_ORDEN:      gestionarEsperaOrden();    break;
    case ESPERA_PRODUCTO:   gestionarEsperaProducto(); break;
    case TRANSITO:          gestionarTransito();       break;
    case LECTURA_QR:        gestionarLecturaQR();      break;
    case CLASIFICANDO:      gestionarClasificacion();  break;
    case PEDIDO_COMPLETADO: break;
  }
}

String lineaPedidoActual() {
  if (cantidadPedido == 0) {
    return pedidosEnCola > 0 ? String(pedidosEnCola) + " en cola" : "Sin pedido activo";
  }
  return clienteActual + " " + variantePedido + " " + String(unidadesProcesadas) + "/" + String(cantidadPedido);
}

void leerPulsadorArranque() {
  if (digitalRead(PIN_BOTON_ARRANQUE) == LOW && estadoMarchaParo == SISTEMA_PARADO) {
    estadoMarchaParo = SISTEMA_EN_MARCHA;
    actualizarLED('V');
    Serial.println("Sistema en marcha");
    mostrarLCD(lineaPedidoActual(), "En marcha");
    enviarEstadoAWeMos("EN_MARCHA");
  }
}

void leerPulsadorParo() {
  if (digitalRead(PIN_BOTON_PARO) == LOW && estadoMarchaParo == SISTEMA_EN_MARCHA) {
    if (cantidadPedido == 0 || estadoActual == ESPERA_ORDEN) {
      servoTraccion.writeMicroseconds(VELOCIDAD_PARADA);
      estadoMarchaParo = SISTEMA_PARADO;
      actualizarLED('N');
      mostrarLCD("Sin pedido activo", "PARADO. ARRANQUE");
      Serial.println("Sistema parado (sin pedido activo)");
      enviarEstadoAWeMos("PARADO");
    } else {
      solicitudParo = true;
      mostrarLCD(lineaPedidoActual(), "Parando...");
    }
  }
}

void procesarEmergencia() {
  if (solicitudEmergencia) {
    solicitudEmergencia = false;
    if (estadoMarchaParo == SISTEMA_EN_MARCHA) {
      estadoMarchaParo = SISTEMA_EMERGENCIA;
      servoTraccion.writeMicroseconds(VELOCIDAD_PARADA);
      digitalWrite(PIN_LED_ROJO, LOW);
      digitalWrite(PIN_LED_VERDE, LOW);
      ledNaranjaParpadeando = true;
      sonarBuzzer(6);
      mostrarLCD("EMERGENCIA", "Retire la pieza");
      Serial.println("EMERGENCIA");
      enviarEstadoAWeMos("EMERGENCIA");
    } else {
      Serial.println("E-STOP ignorado: sistema ya parado");
    }
  }
}

void procesarRearme() {
  if (solicitudRearmeFlag) {
    solicitudRearmeFlag = false;
    if (estadoMarchaParo == SISTEMA_EMERGENCIA) {

      estadoMarchaParo = SISTEMA_EN_MARCHA;
      ledNaranjaParpadeando = false;
      actualizarLED('V');
      sonarBuzzer(4);
      Serial.println("Rearme: emergencia resuelta.");
      enviarEstadoAWeMos("EN_MARCHA");

      // Caso especial: si la emergencia coincidio con un pedido ya
      // completado esperando confirmacion, esta primera pulsacion
      // solo resuelve la emergencia. Hace falta una SEGUNDA pulsacion
      // para confirmar el pedido.
      if (estadoActual == PEDIDO_COMPLETADO) {
        mostrarLCD(clienteActual + " " + variantePedido + " - Listo", "Pulse REARME");
        return;
      }

      mostrandoConfirmacionQR = false;
      actuadorActivado = false;
      lecturaInvalida = false;
      reposarServoClasificacion('A');
      reposarServoClasificacion('B');

      if (cantidadPedido > 0) {
        mostrarLCD(lineaPedidoActual(), "Esperando pieza");
        transicionarA(ESPERA_PRODUCTO);
      } else {
        mostrarLCD("Sin pedido activo", "Esperando orden");
        transicionarA(ESPERA_ORDEN);
      }
      return;
    }
    if (estadoActual == PEDIDO_COMPLETADO) {
      confirmarPedidoCompletado();
    }
  }
}

void leerComandoSerie() {
  if (Serial.available()) {
    String comando = Serial.readStringUntil('\n');
    comando.trim();
    procesarComando(comando);
  }
  if (Serial2.available()) {
    String comando = Serial2.readStringUntil('\n');
    comando.trim();
    Serial.println("Recibido del WeMos: " + comando);
    procesarComando(comando);
  }
}

void procesarComando(String comando) {
  if (comando.length() < 5) return;
  if (!comando.startsWith("ORDEN")) return;
  int p1 = comando.indexOf(' ');
  int p2 = comando.indexOf(' ', p1 + 1);
  int p3 = comando.indexOf(' ', p2 + 1);
  if (p1 < 0 || p2 < 0 || p3 < 0) return;
  String c = comando.substring(p1 + 1, p2);
  String v = comando.substring(p2 + 1, p3);
  int q = comando.substring(p3 + 1).toInt();
  if (q <= 0) return;
  if (encolarPedido(c, v, q)) {
    Serial.println("Pedido encolado: " + c + " " + v + " x" + String(q));
  } else {
    Serial.println("Cola llena, pedido rechazado");
  }
}

bool encolarPedido(String cliente, String variante, int cantidad) {
  if (pedidosEnCola >= MAX_ORDENES) return false;
  colaPedidos[indiceEntrada] = {cliente, variante, cantidad};
  indiceEntrada = (indiceEntrada + 1) % MAX_ORDENES;
  pedidosEnCola++;
  enviarColaAWeMos();
  return true;
}

bool extraerSiguientePedido() {
  if (pedidosEnCola == 0) return false;
  clienteActual  = colaPedidos[indiceSalida].cliente;
  variantePedido = colaPedidos[indiceSalida].variante;
  cantidadPedido = colaPedidos[indiceSalida].cantidad;
  indiceSalida = (indiceSalida + 1) % MAX_ORDENES;
  pedidosEnCola--;
  unidadesProcesadas = 0;
  enviarColaAWeMos();
  return true;
}

void gestionarEsperaOrden() {
  if (solicitudParo) {
    servoTraccion.writeMicroseconds(VELOCIDAD_PARADA);
    estadoMarchaParo = SISTEMA_PARADO;
    solicitudParo = false;
    actualizarLED('N');
    mostrarLCD("Sin pedido activo", "PARADO. ARRANQUE");
    Serial.println("Sistema parado (PARO en espera de orden)");
    enviarEstadoAWeMos("PARADO");
    return;
  }
  if (extraerSiguientePedido()) {
    sonarBuzzer(1);
    mostrarLCD(lineaPedidoActual(), "Nuevo pedido");
    transicionarA(ESPERA_PRODUCTO);
  }
}

void gestionarEsperaProducto() {
  if (solicitudParo) {
    servoTraccion.writeMicroseconds(VELOCIDAD_PARADA);
    estadoMarchaParo = SISTEMA_PARADO;
    solicitudParo = false;
    actualizarLED('N');
    mostrarLCD(lineaPedidoActual(), "PARADO. ARRANQUE");
    Serial.println("Sistema parado (PARO en punto seguro)");
    enviarEstadoAWeMos("PARADO");
    return;
  }
  servoTraccion.writeMicroseconds(VELOCIDAD_TRANSITO);
  if (digitalRead(PIN_S1) == LOW) {
    Serial.println(">>> S1 detectado, entrando en TRANSITO");
    mostrarLCD(lineaPedidoActual(), "Esperando pieza");
    transicionarA(TRANSITO);
  }
}

void gestionarTransito() {
  servoTraccion.writeMicroseconds(VELOCIDAD_TRANSITO);
  if (digitalRead(PIN_S2) == LOW) {
    Serial.println(">>> S2 detectado, entrando en LECTURA_QR (arranca el contador de 15s)");
    while (Serial1.available()) { Serial1.read(); }
    servoTraccion.writeMicroseconds(VELOCIDAD_LECTURA);
    mostrarLCD(lineaPedidoActual(), "Leyendo QR...");
    transicionarA(LECTURA_QR);
  }
}

void gestionarLecturaQR() {
  servoTraccion.writeMicroseconds(VELOCIDAD_LECTURA);
  if (mostrandoConfirmacionQR) return;

  if (Serial1.available()) {
    String qr = Serial1.readStringUntil('\n');

    Serial.print(">>> Byte(s) recibidos en Serial1: [");
    Serial.print(qr);
    Serial.print("] longitud: ");
    Serial.println(qr.length());

    if (qr.length() < LONGITUD_MINIMA_QR) {
      Serial.println(">>> Descartado por ser demasiado corto (probable ruido)");
      return;
    }

    varianteLeida = extraerCampo(qr, "(10)");
    String clienteLeido = extraerCampo(qr, "(91)");
    serieLeida = extraerCampo(qr, "(21)");

    bool varianteOK = (varianteLeida == variantePedido);
    bool clienteOK  = (clienteLeido == clienteActual);
    resultadoLecturaOK = varianteOK && clienteOK;

    Serial.println("========================================");
    Serial.println(">>> CODIGO QR LEIDO:");
    Serial.println("    Cliente : " + clienteLeido + "  (pedido: " + clienteActual + ")");
    Serial.println("    Variante: " + varianteLeida + "  (pedido: " + variantePedido + ")");
    Serial.println("    Serie   : " + serieLeida);
    Serial.println(resultadoLecturaOK ? ">>> RESULTADO: COINCIDE con el pedido" : ">>> RESULTADO: NO COINCIDE con el pedido");
    Serial.println("========================================");

    if (resultadoLecturaOK) {
      mostrarLCD(clienteLeido + " " + varianteLeida + " " + serieLeida, "Lectura OK");
    } else {
      mostrarLCD(clienteLeido + " " + varianteLeida + " " + serieLeida, "NO COINCIDE");
    }

    mostrandoConfirmacionQR = true;
    tiempoInicioConfirmacionQR = millis();
    if (resultadoLecturaOK) {
      decidirLineaDestino();
    }
    return;
  }
}

void actualizarConfirmacionQR() {
  if (!mostrandoConfirmacionQR) return;
  if (millis() - tiempoInicioConfirmacionQR < DURACION_CONFIRMACION_QR) return;

  mostrandoConfirmacionQR = false;

  if (resultadoLecturaOK) {
    actuadorActivado = false;
    mostrarLCD(lineaPedidoActual(), "Clasificando...");
    transicionarA(CLASIFICANDO);
  } else {
    Serial.println(">>> Lectura confirmada como NO valida -> error");
    lecturaInvalida = true;
  }
}

void decidirLineaDestino() {
  lineaDestino = (varianteLeida == "NA") ? 'A' : 'B';
}

void gestionarClasificacion() {
  servoTraccion.writeMicroseconds(VELOCIDAD_TRANSITO);

  unsigned long tiempoObjetivo = (lineaDestino == 'A') ? TIEMPO_HASTA_ACTUADOR_A : TIEMPO_HASTA_ACTUADOR_B;

  if (!actuadorActivado && (millis() - tiempoInicioEstado >= tiempoObjetivo)) {
    activarServoClasificacion(lineaDestino);
    actuadorActivado = true;
    Serial.println(">>> Actuador " + String(lineaDestino) + " activado (empuje). Angulo enviado: " + String(ANGULO_DESVIO));
  }

  if (actuadorActivado) {
    int pinSalida = (lineaDestino == 'A') ? PIN_S3 : PIN_S4;
    if (digitalRead(pinSalida) == LOW) {
      unidadesProcesadas++;
      reposarServoClasificacion(lineaDestino);
      actualizarLED('V');
      sonarBuzzer(1);
      actuadorActivado = false;
      Serial.println(">>> Confirmacion de salida recibida, pieza clasificada correctamente");
      enviarResultadoAWeMos("OK", "");

      if (unidadesProcesadas >= cantidadPedido) {
        servoTraccion.writeMicroseconds(VELOCIDAD_PARADA);
        mostrarLCD(clienteActual + " " + variantePedido + " - Listo", "Pulse REARME");
        sonarBuzzer(5);
        transicionarA(PEDIDO_COMPLETADO);
      } else {
        mostrarLCD(lineaPedidoActual(), "Esperando pieza");
        transicionarA(ESPERA_PRODUCTO);
      }
    }
  }
}

void confirmarPedidoCompletado() {
  if (solicitudParo) {
    // El operario pidio PARO mientras se procesaba la ultima pieza:
    // se atiende ahora, en vez de descartarlo silenciosamente
    solicitudParo = false;
    servoTraccion.writeMicroseconds(VELOCIDAD_PARADA);
    estadoMarchaParo = SISTEMA_PARADO;
    actualizarLED('N');
    cantidadPedido = 0;
    unidadesProcesadas = 0;
    clienteActual = "";
    variantePedido = "";
    mostrarLCD("Sin pedido activo", "PARADO. ARRANQUE");
    Serial.println("Sistema parado (PARO pendiente al completar el pedido)");
    enviarEstadoAWeMos("PARADO");
    transicionarA(ESPERA_ORDEN);
    return;
  }

  if (!extraerSiguientePedido()) {
    cantidadPedido = 0;
    unidadesProcesadas = 0;
    clienteActual = "";
    variantePedido = "";
    mostrarLCD("Sin pedido activo", "Esperando orden");
    transicionarA(ESPERA_ORDEN);
  } else {
    mostrarLCD(lineaPedidoActual(), "Nuevo pedido");
    transicionarA(ESPERA_PRODUCTO);
  }
}

void comprobarErrores() {
  if (errorActivo) {
    if (digitalRead(PIN_S5) == LOW) {
      errorActivo = false;
      buzzerErrorActivo = false;
      noTone(PIN_BUZZER);
      estadoActual = ESPERA_PRODUCTO;
      actualizarLED('V');
      mostrarLCD(lineaPedidoActual(), "Esperando pieza");
    }
    return;
  }

  bool timeoutLectura = (estadoActual == LECTURA_QR) &&
                        !Serial1.available() &&
                        !mostrandoConfirmacionQR &&
                        (millis() - tiempoInicioEstado > TIMEOUT_LECTURA_QR);

  bool lecturaNoValida = (estadoActual == LECTURA_QR) && lecturaInvalida;

  unsigned long tiempoObjetivo = (lineaDestino == 'A') ? TIEMPO_HASTA_ACTUADOR_A : TIEMPO_HASTA_ACTUADOR_B;

  bool timeoutConfirmacion = (estadoActual == CLASIFICANDO) &&
                             actuadorActivado &&
                             (millis() - tiempoInicioEstado > (tiempoObjetivo + TIMEOUT_CONFIRMACION));

  if (timeoutLectura || lecturaNoValida || timeoutConfirmacion) {
    if (timeoutLectura)       motivoErrorActual = "Timeout_lectura";
    else if (lecturaNoValida) motivoErrorActual = "Cliente_no_coincide";
    else                      motivoErrorActual = "Atasco_confirmacion";
    activarError(motivoErrorActual);
  }
}

void activarError(String motivo) {
  reposarServoClasificacion('A');
  reposarServoClasificacion('B');
  actuadorActivado = false;
  if (motivo == "Atasco_confirmacion") {
    mostrarLCD(lineaPedidoActual(), "Atasco. Revisar");
  } else if (motivo == "Cliente_no_coincide") {
    mostrarLCD(lineaPedidoActual(), "Pieza incorrecta");
  } else {
    mostrarLCD(lineaPedidoActual(), "Sin QR. Revisar");
  }
  actualizarLED('R');
  buzzerErrorActivo = true;
  tiempoUltimoBeepError = millis() - INTERVALO_BEEP_ERROR;
  errorActivo = true;
  lecturaInvalida = false;
  Serial.println("ERROR: " + motivo);
  enviarResultadoAWeMos("ERROR", motivo);
}

void inicializarPines() {
  pinMode(PIN_S1, INPUT_PULLUP); pinMode(PIN_S2, INPUT_PULLUP);
  pinMode(PIN_S3, INPUT_PULLUP); pinMode(PIN_S4, INPUT_PULLUP);
  pinMode(PIN_S5, INPUT_PULLUP);
  pinMode(PIN_LED_ROJO, OUTPUT); pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_NARANJA, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BOTON_ESTOP, INPUT_PULLUP); pinMode(PIN_BOTON_REARME, INPUT_PULLUP);
  pinMode(PIN_BOTON_ARRANQUE, INPUT_PULLUP); pinMode(PIN_BOTON_PARO, INPUT_PULLUP);
}

void inicializarServos() {
  servoTraccion.attach(PIN_SERVO_TRACCION);
  servoA.attach(PIN_SERVO_CLASIF_A);
  servoB.attach(PIN_SERVO_CLASIF_B);
  servoTraccion.writeMicroseconds(VELOCIDAD_PARADA);
  servoA.write(ANGULO_REPOSO);
  servoB.write(ANGULO_REPOSO);
  Serial.println(">>> Servos inicializados en ANGULO_REPOSO = " + String(ANGULO_REPOSO));
}

void activarServoClasificacion(char linea) {
  if (linea == 'A') servoA.write(ANGULO_DESVIO);
  else              servoB.write(ANGULO_DESVIO);
}

void reposarServoClasificacion(char linea) {
  if (linea == 'A') servoA.write(ANGULO_REPOSO);
  else              servoB.write(ANGULO_REPOSO);
}

void actualizarLED(char color) {
  ledNaranjaParpadeando = false;
  digitalWrite(PIN_LED_ROJO,    color == 'R');
  digitalWrite(PIN_LED_VERDE,   color == 'V');
  digitalWrite(PIN_LED_NARANJA, color == 'N');
}

void actualizarParpadeoLed() {
  if (!ledNaranjaParpadeando) return;
  if (millis() >= tiempoSiguienteParpadeoLed) {
    estadoLedNaranjaParpadeo = !estadoLedNaranjaParpadeo;
    digitalWrite(PIN_LED_NARANJA, estadoLedNaranjaParpadeo);
    tiempoSiguienteParpadeoLed = millis() + INTERVALO_PARPADEO_LED;
  }
}

void actualizarBuzzerError() {
  if (!buzzerErrorActivo) return;
  if (millis() - tiempoUltimoBeepError >= INTERVALO_BEEP_ERROR) {
    tone(PIN_BUZZER, 600, 200);
    tiempoUltimoBeepError = millis();
  }
}

void sonarBuzzer(int patron) {
  switch (patron) {
    case 1: tone(PIN_BUZZER, 1000, 100); break;
    case 2: tone(PIN_BUZZER, 600, 500);  break;
    case 3: iniciarPatronRepetido(800, 150, 200, 3);  break;
    case 4: iniciarPatronRepetido(1000, 100, 150, 2); break;
    case 5: iniciarPatronRepetido(1200, 100, 150, 3); break;
    case 6: iniciarPatronRepetido(400, 250, 200, 4);  break;
  }
}

void iniciarPatronRepetido(int frecuencia, int duracionTono, int duracionPausa, int repeticiones) {
  frecuenciaBuzzerActual  = frecuencia;
  duracionToneBuzzer      = duracionTono;
  duracionPausaBuzzer     = duracionPausa;
  repeticionesBuzzerTotal = repeticiones;
  repeticionBuzzerActual  = 1;
  tone(PIN_BUZZER, frecuencia, duracionTono);
  tiempoSiguienteBuzzer = millis() + duracionTono + duracionPausa;
}

void actualizarBuzzer() {
  if (repeticionBuzzerActual < repeticionesBuzzerTotal && millis() >= tiempoSiguienteBuzzer) {
    repeticionBuzzerActual++;
    tone(PIN_BUZZER, frecuenciaBuzzerActual, duracionToneBuzzer);
    tiempoSiguienteBuzzer = millis() + duracionToneBuzzer + duracionPausaBuzzer;
  }
}

void mostrarLCD(String l1, String l2) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(l1);
  lcd.setCursor(0, 1); lcd.print(l2);
}

void transicionarA(EstadoSistema nuevo) {
  estadoActual = nuevo;
  tiempoInicioEstado = millis();
}

String extraerCampo(String qr, String ai) {
  int inicio = qr.indexOf(ai) + ai.length();
  int fin = qr.indexOf("(", inicio);
  if (fin == -1) fin = qr.length();
  return qr.substring(inicio, fin);
}

void enviarResultadoAWeMos(String resultado, String motivo) {
  String mensaje = clienteActual + "," + variantePedido + "," + resultado + "," + motivo + "," +
                   serieLeida + "," + String(unidadesProcesadas) + "/" + String(cantidadPedido) + "," +
                   String(unidadesProcesadas);
  Serial2.println(mensaje);
  Serial.println("Enviado a WeMos: " + mensaje);
}

void enviarEstadoAWeMos(String estado) {
  Serial2.println("ESTADO," + estado);
  Serial.println("Estado enviado a WeMos: " + estado);
}

void enviarColaAWeMos() {
  Serial2.println("COLA," + String(pedidosEnCola));
  Serial.println("Cola enviada a WeMos: " + String(pedidosEnCola));
}
