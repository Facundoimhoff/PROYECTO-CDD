// =====================================================================
// ClimControl · versión WiFi
// La ESP32 controla el aula sola (sensores + relés) y, si tiene WiFi,
// manda los datos a Supabase cada 10 segundos. En la respuesta recibe
// las temperaturas que se eligen en la página (Configuración).
// Los datos de la red y del dispositivo van en secretos.h (la página los
// arma al crear el aula).
// =====================================================================
#include <DHT.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include "secretos.h"

// ----- Pines -----
#define PIN_DHT       19
#define PIN_PIR       18
#define PIN_LDR       34
#define PIN_SERVO     23
#define PIN_RELE_LUZ  25
#define PIN_RELE_VENT 26   // relé del ventilador (en la maqueta, el cooler): el cable S va al pin 26
#define PIN_RELE_SERVO 32
#define PIN_IR        21   // aire acondicionado: acá irá el emisor infrarrojo (todavía no está conectado)
#define DHTTYPE DHT11

// Nuestro módulo relé se activa con señal ALTA: HIGH lo prende y LOW lo apaga
#define RELE_PRENDIDO HIGH
#define RELE_APAGADO  LOW

// Temperaturas que deciden qué equipo se prende (con gente en el aula).
// Arrancan con los valores de fábrica, pero se cambian desde la página:
// la ESP32 las recibe de Supabase y las guarda en su memoria, así que
// si se queda sin internet sigue con las últimas que recibió.
float tempVentilador = 24.0;   // desde acá y hasta menos de tempAire: ventilador
float tempAire       = 27.0;   // desde acá: aire acondicionado (y el ventilador se apaga)

// ----- Presencia (sensor PIR) -----
// El PIR detecta MOVIMIENTO, no presencia: un alumno quieto no lo activa.
// Por eso "hay personas" = hubo movimiento en los últimos esperaSinMovimiento ms.
unsigned long esperaSinMovimiento = 5UL * 60000UL;  // 5 minutos (también se cambia desde la página; 0 = al instante)
const unsigned long CALENTAMIENTO_PIR = 60000;      // al prenderlo, el PIR tarda ~1 minuto en estabilizarse
const unsigned long PULSO_MINIMO_PIR = 200;         // un movimiento real dura más que esto; los picos cortos son ruido
const unsigned long INTERVALO_LECTURA = 2000;       // cada cuánto se leen los demás sensores y se decide
const unsigned long INTERVALO_REPORTE = 10000;      // cada cuánto se mandan los datos a Supabase

unsigned long ultimoMovimiento = 0;
bool huboMovimiento = false;
unsigned long inicioPulso = 0;
unsigned long ultimaLectura = 0;
unsigned long ultimoReporte = 0;
int ultimoEstadoReportado = -1;   // para mandar los datos apenas cambia algo
String estadoNube = "sin conectar";

DHT dht(PIN_DHT, DHTTYPE);
Servo servoCortina;
Preferences memoria;              // guarda la configuración aunque se corte la luz
WiFiClientSecure clienteSeguro;   // conexión cifrada (https) con Supabase
WebServer server(80);

// ----- Variables del sistema -----
float temperatura = 0;
float humedad = 0;
bool hayPersonas = false;
int nivelLuz = 0;
bool lucesEncendidas = false;
bool ventiladorEncendido = false;
bool aireEncendido = false;
bool cortinaCerrada = false;
bool modoAutomatico = true;

// ===============================
// SETUP: se ejecuta UNA sola vez cuando se enciende la ESP32
// ===============================
void setup() {
  Serial.begin(115200);
  dht.begin();
  servoCortina.attach(PIN_SERVO);

  pinMode(PIN_PIR, INPUT_PULLDOWN);   // si el cable se suelta, lee "nadie" en vez de valores al azar
  pinMode(PIN_LDR, INPUT);

  pinMode(PIN_RELE_LUZ, OUTPUT);
  pinMode(PIN_RELE_VENT, OUTPUT);
  digitalWrite(PIN_RELE_VENT, RELE_APAGADO);   // el ventilador arranca apagado

  pinMode(PIN_RELE_SERVO, OUTPUT);
  digitalWrite(PIN_RELE_SERVO, HIGH);

  pinMode(PIN_IR, OUTPUT);
  digitalWrite(PIN_IR, LOW);

  Serial.println("Sistema iniciado");

  // Configuración guardada en la memoria (la última que mandó la página)
  memoria.begin("climcontrol", false);
  tempVentilador = memoria.getFloat("tVent", tempVentilador);
  tempAire = memoria.getFloat("tAire", tempAire);
  esperaSinMovimiento = memoria.getULong("espera", esperaSinMovimiento);
  Serial.print("Temperaturas: ventilador desde ");
  Serial.print(tempVentilador, 1);
  Serial.print(" C, aire desde ");
  Serial.print(tempAire, 1);
  Serial.println(" C");

  // WiFi: probamos 15 segundos como máximo; sin WiFi el aula igual tiene que funcionar
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);   // si se corta, la ESP32 vuelve a conectarse sola
  WiFi.begin(WIFI_NOMBRE, WIFI_CLAVE);
  Serial.print("Conectando a WiFi");
  unsigned long inicioWiFi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicioWiFi < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi conectado. IP de la ESP32: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("No se pudo conectar al WiFi: revisen el nombre y la clave en secretos.h,");
    Serial.println("y que la red sea de 2,4 GHz. Los sensores siguen funcionando igual.");
  }

  // Sin verificar el certificado: alcanza para el prototipo (los datos igual viajan cifrados)
  clienteSeguro.setInsecure();

  // Página local de la ESP32 (control manual desde la misma red)
  server.on("/", paginaPrincipal);
  server.on("/automatico", activarAutomatico);
  server.on("/manual", activarManual);
  server.on("/luces/on", lucesOn);
  server.on("/luces/off", lucesOff);
  server.on("/ventilador/on", ventiladorOn);
  server.on("/ventilador/off", ventiladorOff);
  server.on("/cortina/abrir", abrirCortina);
  server.on("/cortina/cerrar", cerrarCortina);
  server.on("/aire/on", aireOn);
  server.on("/aire/off", aireOff);
  server.begin();
  Serial.println("Servidor web local iniciado");
}

// ===============================
// LOOP: se repite continuamente
// ===============================
void loop() {
  server.handleClient();

  // El PIR se mira todo el tiempo para no perder movimientos
  revisarPIR();

  // Sin delay(): los demás sensores se leen cada 2 segundos contando el tiempo
  if (millis() - ultimaLectura >= INTERVALO_LECTURA) {
    ultimaLectura = millis();
    leerSensores();
    if (modoAutomatico) {
      controlarCortinas();
      controlarLuces();
      controlarVentilador();
      controlarAire();
    }
    mostrarDatos();
  }

  // A Supabase: cada 10 segundos, o enseguida si cambió algún equipo o la presencia
  int estadoActual = (hayPersonas ? 1 : 0) + (ventiladorEncendido ? 2 : 0) + (aireEncendido ? 4 : 0) + (lucesEncendidas ? 8 : 0);
  bool cambio = estadoActual != ultimoEstadoReportado;
  if (millis() - ultimoReporte >= INTERVALO_REPORTE || (cambio && millis() - ultimoReporte >= 1000)) {
    ultimoReporte = millis();
    if (reportar()) ultimoEstadoReportado = estadoActual;
  }
}

// ===============================
// FUNCIONES
// ===============================
void leerSensores() {
  humedad = dht.readHumidity();
  temperatura = dht.readTemperature();
  nivelLuz = analogRead(PIN_LDR);
}

void revisarPIR() {
  // Durante el primer minuto el PIR da detecciones falsas: no le hacemos caso
  if (millis() < CALENTAMIENTO_PIR) {
    hayPersonas = false;
    return;
  }
  if (digitalRead(PIN_PIR) == HIGH) {
    if (inicioPulso == 0) inicioPulso = millis();
    // Solo cuenta si la señal se mantiene: así se ignoran los picos de ruido
    if (millis() - inicioPulso >= PULSO_MINIMO_PIR) {
      ultimoMovimiento = millis();
      huboMovimiento = true;
    }
  } else {
    inicioPulso = 0;
  }
  // Hay personas si el sensor está detectando ahora, o si hubo movimiento hace menos que la espera
  bool antes = hayPersonas;
  bool detectandoAhora = inicioPulso != 0 && millis() - inicioPulso >= PULSO_MINIMO_PIR;
  hayPersonas = detectandoAhora ||
                (huboMovimiento && millis() - ultimoMovimiento < esperaSinMovimiento);
  if (hayPersonas != antes) mostrarDatos();
}

void controlarCortinas() {
  float temperaturaAlta = 24.0;
  int luzAlta = 2500;
  if (hayPersonas && temperatura > temperaturaAlta && nivelLuz > luzAlta) {
    digitalWrite(PIN_RELE_SERVO, LOW);
    cortinaCerrada = true;
    servoCortina.write(180);
  } else {
    digitalWrite(PIN_RELE_SERVO, LOW);
    cortinaCerrada = false;
    servoCortina.write(0);
  }
}

void controlarLuces() {
  int luzBaja = 1200;
  if (hayPersonas && nivelLuz < luzBaja) {
    lucesEncendidas = true;
    digitalWrite(PIN_RELE_LUZ, LOW);
  } else {
    lucesEncendidas = false;
    digitalWrite(PIN_RELE_LUZ, HIGH);
  }
}

// Ventilador (el cooler): se prende SOLO si se cumplen las dos condiciones:
//   1) hay gente en el aula
//   2) la temperatura está entre tempVentilador y menos de tempAire
// Desde tempAire se apaga, porque ahí entra el aire acondicionado.
void controlarVentilador() {
  if (hayPersonas &&
      temperatura >= tempVentilador &&
      temperatura < tempAire) {
    ventiladorEncendido = true;
    digitalWrite(PIN_RELE_VENT, RELE_PRENDIDO);
  } else {
    ventiladorEncendido = false;
    digitalWrite(PIN_RELE_VENT, RELE_APAGADO);
  }
}

// Aire acondicionado: se prende si hay gente y la temperatura es de tempAire o más
void controlarAire() {
  if (hayPersonas && temperatura >= tempAire) {
    aireEncendido = true;
    digitalWrite(PIN_IR, HIGH);
  } else {
    aireEncendido = false;
    digitalWrite(PIN_IR, LOW);
  }
}

// ----- Supabase -----
// Llama a la función dispositivo_reportar de la base: guarda la lectura y
// devuelve las temperaturas de la configuración. Devuelve true si salió bien.
bool reportar() {
  if (WiFi.status() != WL_CONNECTED) {
    estadoNube = "sin WiFi";
    return false;
  }
  if (String(SUPABASE_URL).indexOf("supabase.co") < 0 || String(DISPOSITIVO_CODIGO).length() < 4) {
    estadoNube = "faltan los datos de Supabase en secretos.h";
    return false;
  }

  String json = "{";
  json += "\"p_codigo\":\"";   json += DISPOSITIVO_CODIGO; json += "\",";
  json += "\"p_clave\":\"";    json += DISPOSITIVO_CLAVE;  json += "\",";
  json += "\"p_temperatura\":"; json += isnan(temperatura) ? String("null") : String(temperatura, 1); json += ",";
  json += "\"p_humedad\":";     json += isnan(humedad) ? String("null") : String(humedad, 1);         json += ",";
  json += "\"p_hay_personas\":"; json += hayPersonas ? "true" : "false";        json += ",";
  json += "\"p_ventilador\":";   json += ventiladorEncendido ? "true" : "false"; json += ",";
  json += "\"p_aire\":";         json += aireEncendido ? "true" : "false";       json += ",";
  json += "\"p_luces\":";        json += lucesEncendidas ? "true" : "false";
  json += "}";

  HTTPClient http;
  http.begin(clienteSeguro, String(SUPABASE_URL) + "/rest/v1/rpc/dispositivo_reportar");
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_CLAVE);
  // las claves viejas ("anon", empiezan con eyJ) también van en Authorization
  if (String(SUPABASE_CLAVE).startsWith("eyJ")) http.addHeader("Authorization", String("Bearer ") + SUPABASE_CLAVE);

  int codigo = http.POST(json);
  String respuesta = http.getString();
  http.end();

  if (codigo == 200) {
    estadoNube = "datos enviados";
    float tv, ta, minutos;
    if (leerNumero(respuesta, "temp_ventilador", tv) && leerNumero(respuesta, "temp_aire", ta) &&
        leerNumero(respuesta, "minutos_sin_movimiento", minutos)) {
      aplicarConfiguracion(tv, ta, minutos);
    }
    return true;
  }
  if (respuesta.indexOf("DISPOSITIVO_INVALIDO") >= 0) estadoNube = "el codigo o la clave del dispositivo no coinciden (revisen secretos.h)";
  else if (codigo < 0) estadoNube = "sin respuesta de Supabase (revisen internet)";
  else estadoNube = "Supabase respondio " + String(codigo) + ": " + respuesta.substring(0, 120);
  return false;
}

// Busca "clave" : numero en la respuesta (un JSON chico) y devuelve el número
bool leerNumero(const String& json, const char* clave, float& valor) {
  String buscar = String("\"") + clave + "\"";
  int i = json.indexOf(buscar);
  if (i < 0) return false;
  i = json.indexOf(':', i + buscar.length());
  if (i < 0) return false;
  String resto = json.substring(i + 1);
  resto.trim();
  if (resto.length() == 0 || !(isDigit(resto[0]) || resto[0] == '-')) return false;
  valor = resto.toFloat();
  return true;
}

// Usa las temperaturas que vienen de la página y las guarda en la memoria
void aplicarConfiguracion(float tv, float ta, float minutos) {
  if (tv < 15 || tv > 40 || ta < 15 || ta > 40 || ta <= tv || minutos < 0 || minutos > 60) return;   // valores raros: se ignoran
  unsigned long espera = (unsigned long)(minutos * 60000.0);
  if (tv == tempVentilador && ta == tempAire && espera == esperaSinMovimiento) return;
  tempVentilador = tv;
  tempAire = ta;
  esperaSinMovimiento = espera;
  memoria.putFloat("tVent", tv);
  memoria.putFloat("tAire", ta);
  memoria.putULong("espera", espera);
  Serial.print("Configuracion nueva desde la pagina: ventilador desde ");
  Serial.print(tv, 1);
  Serial.print(" C, aire desde ");
  Serial.print(ta, 1);
  Serial.print(" C, aula vacia despues de ");
  Serial.print((int)minutos);
  Serial.println(" min");
}

void mostrarDatos() {
  Serial.println("--------------------");
  Serial.print("Temperatura: ");
  Serial.println(temperatura);
  Serial.print("Humedad: ");
  Serial.println(humedad);
  Serial.print("Personas: ");
  Serial.println(hayPersonas ? "SI" : "NO");
  if (millis() < CALENTAMIENTO_PIR) {
    Serial.print("PIR: calentando, faltan ");
    Serial.print((CALENTAMIENTO_PIR - millis()) / 1000);
    Serial.println(" s");
  } else if (huboMovimiento) {
    Serial.print("Ultimo movimiento: hace ");
    Serial.print((millis() - ultimoMovimiento) / 1000);
    Serial.println(" s");
  }
  Serial.print("Nivel de luz: ");
  Serial.println(nivelLuz);
  Serial.print("Cortina: ");
  Serial.println(cortinaCerrada ? "CERRADA" : "ABIERTA");
  Serial.print("Luces: ");
  Serial.println(lucesEncendidas ? "ENCENDIDAS" : "APAGADAS");
  Serial.print("Ventilador: ");
  Serial.println(ventiladorEncendido ? "ENCENDIDO" : "APAGADO");
  Serial.print("Aire acondicionado: ");
  Serial.println(aireEncendido ? "ENCENDIDO" : "APAGADO");
  Serial.print("Modo: ");
  Serial.println(modoAutomatico ? "AUTOMATICO" : "MANUAL");
  Serial.print("WiFi: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "CONECTADO" : "SIN CONEXION");
  Serial.print("Supabase: ");
  Serial.println(estadoNube);
}

// ----- Página local de la ESP32 (control manual) -----
void paginaPrincipal() {
  String html = "<html><head><meta charset='utf-8'><title>Sistema Aula</title></head><body>";
  html += "<h1>Sistema Inteligente del Aula</h1>";
  html += "<h2>Datos</h2>";
  html += "<p>Temperatura: " + String(temperatura) + " C</p>";
  html += "<p>Humedad: " + String(humedad) + " %</p>";
  html += "<p>Personas: " + String(hayPersonas ? "SI" : "NO") + "</p>";
  html += "<p>Nivel de luz: " + String(nivelLuz) + "</p>";
  html += "<h2>Estado</h2>";
  html += "<p>Modo: " + String(modoAutomatico ? "AUTOMATICO" : "MANUAL") + "</p>";
  html += "<p>Cortina: " + String(cortinaCerrada ? "CERRADA" : "ABIERTA") + "</p>";
  html += "<p>Luces: " + String(lucesEncendidas ? "ENCENDIDAS" : "APAGADAS") + "</p>";
  html += "<p>Ventilador: " + String(ventiladorEncendido ? "ENCENDIDO" : "APAGADO") + "</p>";
  html += "<p>Aire: " + String(aireEncendido ? "ENCENDIDO" : "APAGADO") + "</p>";
  html += "<h2>Modo de funcionamiento</h2>";
  html += "<a href='/automatico'><button>AUTOMATICO</button></a><a href='/manual'><button>MANUAL</button></a>";
  html += "<h2>Cortina</h2><a href='/cortina/abrir'><button>ABRIR</button></a><a href='/cortina/cerrar'><button>CERRAR</button></a>";
  html += "<h2>Luces</h2><a href='/luces/on'><button>ENCENDER</button></a><a href='/luces/off'><button>APAGAR</button></a>";
  html += "<h2>Ventilador</h2><a href='/ventilador/on'><button>ENCENDER</button></a><a href='/ventilador/off'><button>APAGAR</button></a>";
  html += "<h2>Aire acondicionado</h2><a href='/aire/on'><button>ENCENDER</button></a><a href='/aire/off'><button>APAGAR</button></a>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void volverAlInicio() {
  server.sendHeader("Location", "/");
  server.send(303);
}
void activarAutomatico() { modoAutomatico = true; volverAlInicio(); }
void activarManual() { modoAutomatico = false; volverAlInicio(); }
void lucesOn() { if (!modoAutomatico) { lucesEncendidas = true; digitalWrite(PIN_RELE_LUZ, LOW); } volverAlInicio(); }
void lucesOff() { if (!modoAutomatico) { lucesEncendidas = false; digitalWrite(PIN_RELE_LUZ, HIGH); } volverAlInicio(); }
void ventiladorOn() { if (!modoAutomatico) { ventiladorEncendido = true; digitalWrite(PIN_RELE_VENT, RELE_PRENDIDO); } volverAlInicio(); }
void ventiladorOff() { if (!modoAutomatico) { ventiladorEncendido = false; digitalWrite(PIN_RELE_VENT, RELE_APAGADO); } volverAlInicio(); }
void abrirCortina() { if (!modoAutomatico) { digitalWrite(PIN_RELE_SERVO, LOW); cortinaCerrada = false; servoCortina.write(0); } volverAlInicio(); }
void cerrarCortina() { if (!modoAutomatico) { digitalWrite(PIN_RELE_SERVO, LOW); cortinaCerrada = true; servoCortina.write(180); } volverAlInicio(); }
void aireOn() { if (!modoAutomatico) { aireEncendido = true; digitalWrite(PIN_IR, HIGH); } volverAlInicio(); }
void aireOff() { if (!modoAutomatico) { aireEncendido = false; digitalWrite(PIN_IR, LOW); } volverAlInicio(); }
