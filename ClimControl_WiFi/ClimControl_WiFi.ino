// =====================================================================
// ClimControl · versión WiFi
// La ESP32 controla el aula sola (sensores + relés) y, con WiFi, manda
// los datos a Supabase cada 10 segundos. En la respuesta recibe las
// temperaturas que se eligen en la página (Configuración).
//
// Para el cliente no hace falta Arduino IDE:
//  1) Si el aparato no tiene WiFi guardado, crea su propia red WiFi
//     "ClimControl-XXXX". Al conectarse con el celular se abre una página
//     donde se elige el WiFi del aula, su clave y el código de vinculación
//     que da la app al agregar el aula.
//  2) El aparato canjea ese código en Supabase por su clave propia y la
//     guarda en su memoria. Desde ahí funciona solo.
//  3) Para empezar de nuevo (otro WiFi u otra cuenta): mantener apretado
//     el botón BOOT de la placa 5 segundos.
// (Para pruebas también se pueden poner los datos a mano en secretos.h.)
// =====================================================================
#include <DHT.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>

// Servidor de ClimControl: es el mismo para todos los aparatos.
// La clave es la PÚBLICA (publishable): la misma que usa la página web.
#define SUPABASE_URL_FABRICA   "https://jkhyakdhlcbwkrlynvxh.supabase.co"
#define SUPABASE_CLAVE_FABRICA "sb_publishable_qeKalge2NrjIaetxfz_30g_J2E73UaL"

// secretos.h es opcional (solo para cargar los datos a mano con Arduino IDE)
#if __has_include("secretos.h")
#include "secretos.h"
#endif
#ifndef WIFI_NOMBRE
#define WIFI_NOMBRE ""
#endif
#ifndef WIFI_CLAVE
#define WIFI_CLAVE ""
#endif
#ifndef SUPABASE_URL
#define SUPABASE_URL ""
#endif
#ifndef SUPABASE_CLAVE
#define SUPABASE_CLAVE ""
#endif
#ifndef DISPOSITIVO_CODIGO
#define DISPOSITIVO_CODIGO ""
#endif
#ifndef DISPOSITIVO_CLAVE
#define DISPOSITIVO_CLAVE ""
#endif

// ----- Pines -----
#define PIN_DHT       19
#define PIN_PIR       18
#define PIN_LDR       34
#define PIN_SERVO     23
#define PIN_RELE_LUZ  25
#define PIN_RELE_VENT 26   // relé del ventilador (en la maqueta, el cooler): el cable S va al pin 26
#define PIN_RELE_SERVO 32
#define PIN_IR        21   // aire acondicionado: acá irá el emisor infrarrojo (todavía no está conectado)
#define PIN_BOTON     0    // botón BOOT de la placa: 5 segundos apretado = borrar WiFi y vinculación
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

// ----- Conexión -----
String wifiNombre, wifiClave;        // el WiFi del aula
String dispCodigo, dispClave;        // identidad del aparato en Supabase
String vinculacion;                  // código de vinculación pendiente de canjear
String urlSupabase, claveSupabase;
String nombreRed;                    // red propia del aparato: ClimControl-XXXX
String estadoVinculacion = "";
bool portalActivo = false;
unsigned long portalDesde = 0, ultimaVezConectado = 0, ultimoIntentoVinc = 0, listoDesde = 0, botonDesde = 0;
int rechazosSeguidos = 0;

DHT dht(PIN_DHT, DHTTYPE);
Servo servoCortina;
Preferences memoria;              // guarda WiFi, vinculación y configuración aunque se corte la luz
WiFiClientSecure clienteSeguro;   // conexión cifrada (https) con Supabase
WebServer server(80);
DNSServer dns;                    // hace que el celular abra solo la página de configuración

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
  pinMode(PIN_BOTON, INPUT_PULLUP);

  pinMode(PIN_RELE_LUZ, OUTPUT);
  pinMode(PIN_RELE_VENT, OUTPUT);
  digitalWrite(PIN_RELE_VENT, RELE_APAGADO);   // el ventilador arranca apagado

  pinMode(PIN_RELE_SERVO, OUTPUT);
  digitalWrite(PIN_RELE_SERVO, HIGH);

  pinMode(PIN_IR, OUTPUT);
  digitalWrite(PIN_IR, LOW);

  Serial.println("Sistema iniciado");

  // Lo guardado en la memoria: configuración, WiFi y vinculación
  memoria.begin("climcontrol", false);
  tempVentilador = memoria.getFloat("tVent", tempVentilador);
  tempAire = memoria.getFloat("tAire", tempAire);
  esperaSinMovimiento = memoria.getULong("espera", esperaSinMovimiento);
  wifiNombre = memoria.getString("wifi", "");
  wifiClave = memoria.getString("wifiClave", "");
  dispCodigo = memoria.getString("codigo", "");
  dispClave = memoria.getString("clave", "");
  vinculacion = memoria.getString("vinc", "");

  // Si no hay nada guardado, se usan los datos de secretos.h (si los completaron)
  if (wifiNombre == "" && valido(WIFI_NOMBRE, "tu red")) { wifiNombre = WIFI_NOMBRE; wifiClave = WIFI_CLAVE; }
  if (dispCodigo == "" && valido(DISPOSITIVO_CODIGO, "XXXX")) { dispCodigo = DISPOSITIVO_CODIGO; dispClave = DISPOSITIVO_CLAVE; }
  urlSupabase = valido(SUPABASE_URL, "TU-PROYECTO") ? String(SUPABASE_URL) : String(SUPABASE_URL_FABRICA);
  claveSupabase = valido(SUPABASE_CLAVE, "TU-CLAVE") ? String(SUPABASE_CLAVE) : String(SUPABASE_CLAVE_FABRICA);

  Serial.print("Temperaturas: ventilador desde ");
  Serial.print(tempVentilador, 1);
  Serial.print(" C, aire desde ");
  Serial.print(tempAire, 1);
  Serial.println(" C");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);   // si se corta, la ESP32 vuelve a conectarse sola
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  nombreRed = "ClimControl-" + mac.substring(8);

  if (wifiNombre != "") {
    // Probamos 15 segundos como máximo: sin WiFi el aula igual tiene que funcionar
    WiFi.begin(wifiNombre.c_str(), wifiClave.c_str());
    Serial.print("Conectando a WiFi ");
    Serial.print(wifiNombre);
    unsigned long inicioWiFi = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - inicioWiFi < 15000) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("WiFi conectado. IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("No se pudo conectar al WiFi. Los sensores siguen funcionando igual.");
    }
  }
  ultimaVezConectado = millis();

  // Sin verificar el certificado: alcanza para el prototipo (los datos igual viajan cifrados)
  clienteSeguro.setInsecure();

  // Páginas del aparato
  server.on("/", paginaInicio);
  server.on("/guardar", HTTP_POST, guardarConfiguracionWiFi);
  server.on("/estado", paginaEstado);
  server.on("/control", paginaPrincipal);
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
  server.onNotFound(paginaNoEncontrada);
  server.begin();

  if (wifiNombre == "" || (dispCodigo == "" && vinculacion == "")) abrirPortal();
}

// ===============================
// LOOP: se repite continuamente
// ===============================
void loop() {
  server.handleClient();
  atenderConexion();

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
  if (dispCodigo != "") {
    int estadoActual = (hayPersonas ? 1 : 0) + (ventiladorEncendido ? 2 : 0) + (aireEncendido ? 4 : 0) + (lucesEncendidas ? 8 : 0);
    bool cambio = estadoActual != ultimoEstadoReportado;
    if (millis() - ultimoReporte >= INTERVALO_REPORTE || (cambio && millis() - ultimoReporte >= 1000)) {
      ultimoReporte = millis();
      if (reportar()) ultimoEstadoReportado = estadoActual;
    }
  }
}

// ===============================
// CONEXIÓN: red propia, vinculación y botón BOOT
// ===============================
bool valido(const char* valor, const char* deEjemplo) {
  return strlen(valor) > 0 && strstr(valor, deEjemplo) == nullptr;
}

void atenderConexion() {
  if (portalActivo) dns.processNextRequest();
  bool conectado = WiFi.status() == WL_CONNECTED;
  if (conectado) ultimaVezConectado = millis();

  // Si hace un minuto que no se puede conectar, se abre la red propia para corregir el WiFi
  if (!portalActivo && wifiNombre != "" && !conectado && millis() - ultimaVezConectado > 60000) abrirPortal();

  // Canjear el código de vinculación (cada 10 segundos hasta que salga)
  if (conectado && vinculacion != "" && millis() - ultimoIntentoVinc > 10000) {
    ultimoIntentoVinc = millis();
    vincular();
  }

  // Todo listo: la red propia se apaga a los 90 segundos (para que el celular vea el resultado)
  if (portalActivo && conectado && dispCodigo != "" && vinculacion == "") {
    if (listoDesde == 0) listoDesde = millis();
    if (millis() - listoDesde > 90000) cerrarPortal();
  } else {
    listoDesde = 0;
  }

  // Botón BOOT apretado 5 segundos: borrar WiFi y vinculación, y empezar de nuevo
  if (digitalRead(PIN_BOTON) == LOW) {
    if (botonDesde == 0) botonDesde = millis();
    if (millis() - botonDesde > 5000) {
      Serial.println("Botón BOOT: se borran el WiFi y la vinculación. Reiniciando...");
      memoria.remove("wifi"); memoria.remove("wifiClave"); memoria.remove("codigo"); memoria.remove("clave"); memoria.remove("vinc");
      delay(300);
      ESP.restart();
    }
  } else {
    botonDesde = 0;
  }
}

void abrirPortal() {
  if (portalActivo) return;
  portalActivo = true;
  portalDesde = millis();
  WiFi.mode(WIFI_AP_STA);   // la red propia y, a la vez, el intento de conexión al WiFi del aula
  WiFi.softAP(nombreRed.c_str());
  delay(100);
  dns.start(53, "*", WiFi.softAPIP());
  if (wifiNombre != "") WiFi.begin(wifiNombre.c_str(), wifiClave.c_str());
  Serial.print("Red de configuracion abierta: ");
  Serial.print(nombreRed);
  Serial.println("  (desde el celular: conectarse y entrar a http://192.168.4.1)");
}

void cerrarPortal() {
  if (!portalActivo) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalActivo = false;
  Serial.println("Red de configuracion cerrada: el aparato ya está conectado");
}

// Prepara un pedido a una función de Supabase
void prepararPedido(HTTPClient& http, const char* funcion) {
  http.begin(clienteSeguro, urlSupabase + "/rest/v1/rpc/" + funcion);
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", claveSupabase);
  // las claves viejas ("anon", empiezan con eyJ) también van en Authorization
  if (claveSupabase.startsWith("eyJ")) http.addHeader("Authorization", "Bearer " + claveSupabase);
}

// Canjea el código de vinculación por el código y la clave propios del aparato
void vincular() {
  HTTPClient http;
  prepararPedido(http, "vincular_dispositivo");
  int codigo = http.POST("{\"p_vinculacion\":\"" + vinculacion + "\"}");
  String respuesta = http.getString();
  http.end();
  String c, k;
  if (codigo == 200 && leerTexto(respuesta, "codigo", c) && leerTexto(respuesta, "clave", k)) {
    dispCodigo = c;
    dispClave = k;
    memoria.putString("codigo", c);
    memoria.putString("clave", k);
    vinculacion = "";
    memoria.remove("vinc");
    estadoVinculacion = "listo";
    rechazosSeguidos = 0;
    ultimoEstadoReportado = -1;   // reportar enseguida
    Serial.print("Vinculado: este aparato es ");
    Serial.println(c);
  } else if (respuesta.indexOf("VINCULACION_INVALIDA") >= 0) {
    vinculacion = "";
    memoria.remove("vinc");
    estadoVinculacion = "El código no sirve o ya venció. Generá otro en la app (Aulas → Dispositivo → Vincular un aparato) y volvé a cargarlo.";
    Serial.println("El codigo de vinculacion no sirve o vencio");
    abrirPortal();
  } else {
    estadoVinculacion = "Esperando respuesta del servidor (se reintenta solo)...";
  }
}

// ===============================
// FUNCIONES DEL AULA
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
  String json = "{";
  json += "\"p_codigo\":\"";   json += dispCodigo; json += "\",";
  json += "\"p_clave\":\"";    json += dispClave;  json += "\",";
  json += "\"p_temperatura\":"; json += isnan(temperatura) ? String("null") : String(temperatura, 1); json += ",";
  json += "\"p_humedad\":";     json += isnan(humedad) ? String("null") : String(humedad, 1);         json += ",";
  json += "\"p_hay_personas\":"; json += hayPersonas ? "true" : "false";        json += ",";
  json += "\"p_ventilador\":";   json += ventiladorEncendido ? "true" : "false"; json += ",";
  json += "\"p_aire\":";         json += aireEncendido ? "true" : "false";       json += ",";
  json += "\"p_luces\":";        json += lucesEncendidas ? "true" : "false";
  json += "}";

  HTTPClient http;
  prepararPedido(http, "dispositivo_reportar");
  int codigo = http.POST(json);
  String respuesta = http.getString();
  http.end();

  if (codigo == 200) {
    estadoNube = "datos enviados";
    rechazosSeguidos = 0;
    float tv, ta, minutos;
    if (leerNumero(respuesta, "temp_ventilador", tv) && leerNumero(respuesta, "temp_aire", ta) &&
        leerNumero(respuesta, "minutos_sin_movimiento", minutos)) {
      aplicarConfiguracion(tv, ta, minutos);
    }
    return true;
  }
  if (respuesta.indexOf("DISPOSITIVO_INVALIDO") >= 0) {
    estadoNube = "el servidor no reconoce este aparato";
    // Tres rechazos seguidos: el aula se borró o se vinculó otro aparato. Se vuelve a configurar.
    if (++rechazosSeguidos >= 3) {
      Serial.println("El aparato ya no está vinculado: se abre la red de configuracion");
      dispCodigo = ""; dispClave = "";
      memoria.remove("codigo"); memoria.remove("clave");
      abrirPortal();
    }
  }
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

// Busca "clave" : "texto" en la respuesta y devuelve el texto
bool leerTexto(const String& json, const char* clave, String& valor) {
  String buscar = String("\"") + clave + "\"";
  int i = json.indexOf(buscar);
  if (i < 0) return false;
  i = json.indexOf(':', i + buscar.length());
  if (i < 0) return false;
  int ini = json.indexOf('"', i + 1);
  if (ini < 0) return false;
  int fin = json.indexOf('"', ini + 1);
  if (fin < 0) return false;
  valor = json.substring(ini + 1, fin);
  return valor.length() > 0;
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
  Serial.println(WiFi.status() == WL_CONNECTED ? "CONECTADO a " + wifiNombre : String("SIN CONEXION"));
  if (portalActivo) {
    Serial.print("Red de configuracion: ");
    Serial.println(nombreRed);
  }
  Serial.print("Aparato: ");
  Serial.println(dispCodigo != "" ? dispCodigo : (vinculacion != "" ? String("vinculando...") : String("sin vincular")));
  Serial.print("Supabase: ");
  Serial.println(estadoNube);
}

// ===============================
// PÁGINAS DEL APARATO
// ===============================
String escaparHtml(String s) {
  s.replace("&", "&amp;"); s.replace("<", "&lt;"); s.replace(">", "&gt;"); s.replace("\"", "&quot;");
  return s;
}

const char* ESTILO = "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<style>body{font-family:system-ui,sans-serif;background:#eef1f6;color:#1c1c1e;margin:0;padding:24px 16px}"
  ".c{max-width:420px;margin:0 auto;background:#fff;border-radius:18px;padding:22px;box-shadow:0 8px 24px rgba(0,0,0,.08)}"
  "h1{font-size:22px;margin:0 0 6px}p{color:#555;line-height:1.45}label{display:block;font-weight:600;margin:16px 0 6px}"
  "input{width:100%;box-sizing:border-box;height:46px;border:1px solid #ccd;border-radius:12px;padding:0 12px;font-size:17px}"
  "button{width:100%;height:48px;margin-top:20px;border:0;border-radius:12px;background:#0071e3;color:#fff;font-size:17px;font-weight:600}"
  ".n{font-size:14px;color:#666}.ok{color:#1a8a47;font-weight:600}.mal{color:#d70015;font-weight:600}</style>";

void paginaInicio() {
  if (!portalActivo) { paginaPrincipal(); return; }
  // Redes WiFi cercanas para elegir sin escribir el nombre
  int n = WiFi.scanNetworks();
  String opciones = "";
  for (int i = 0; i < n && i < 20; i++) {
    String r = WiFi.SSID(i);
    if (r.length() > 0 && opciones.indexOf("value=\"" + escaparHtml(r) + "\"") < 0) opciones += "<option value=\"" + escaparHtml(r) + "\">";
  }
  String html = "<!doctype html><html><head><title>ClimControl</title>";
  html += ESTILO;
  html += "</head><body><div class='c'><h1>Configurar ClimControl</h1>";
  html += "<p>Elegí el WiFi del aula y escribí el código de vinculación que te da la app (Aulas → Agregar aula).</p>";
  if (estadoVinculacion != "" && estadoVinculacion != "listo") html += "<p class='mal'>" + escaparHtml(estadoVinculacion) + "</p>";
  html += "<form method='post' action='/guardar'>";
  html += "<label for='r'>Red WiFi del aula</label><input id='r' name='red' list='redes' required value=\"" + escaparHtml(wifiNombre) + "\" autocomplete='off'><datalist id='redes'>" + opciones + "</datalist>";
  html += "<p class='n'>Tiene que ser de 2,4 GHz. Las redes que piden iniciar sesión en una página no sirven.</p>";
  html += "<label for='k'>Clave del WiFi</label><input id='k' name='clave' type='password'>";
  html += "<label for='v'>Código de vinculación</label><input id='v' name='vinc' placeholder='XXXX-XXXX' autocapitalize='characters' autocomplete='off'>";
  if (dispCodigo != "") html += "<p class='n'>Este aparato ya está vinculado (" + dispCodigo + "). Si solo cambiás el WiFi, dejá el código vacío.</p>";
  html += "<button type='submit'>Conectar</button></form></div></body></html>";
  server.send(200, "text/html", html);
}

void guardarConfiguracionWiFi() {
  String red = server.arg("red");
  String clave = server.arg("clave");
  String vinc = server.arg("vinc");
  red.trim();
  vinc.trim();
  vinc.toUpperCase();
  vinc.replace("-", "");
  vinc.replace(" ", "");
  if (red == "" || (vinc == "" && dispCodigo == "") || (vinc != "" && vinc.length() != 8)) {
    estadoVinculacion = red == "" ? "Elegí la red WiFi." : "Escribí el código de vinculación de 8 caracteres que te da la app.";
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }
  wifiNombre = red;
  wifiClave = clave;
  memoria.putString("wifi", red);
  memoria.putString("wifiClave", clave);
  if (vinc != "") {
    vinculacion = vinc;
    memoria.putString("vinc", vinc);
    ultimoIntentoVinc = 0;
  }
  estadoVinculacion = "";
  WiFi.disconnect();
  WiFi.begin(wifiNombre.c_str(), wifiClave.c_str());
  ultimaVezConectado = millis();
  server.sendHeader("Location", "/estado");
  server.send(303);
}

void paginaEstado() {
  bool conectado = WiFi.status() == WL_CONNECTED;
  String html = "<!doctype html><html><head><title>ClimControl</title><meta http-equiv='refresh' content='3'>";
  html += ESTILO;
  html += "</head><body><div class='c'><h1>Conectando…</h1>";
  html += "<p>WiFi <b>" + escaparHtml(wifiNombre) + "</b>: ";
  html += conectado ? "<span class='ok'>conectado</span>" : (millis() - ultimaVezConectado > 20000 ? "<span class='mal'>no se puede conectar. Revisá la clave.</span> <a href='/'>Volver</a>" : String("conectando…"));
  html += "</p><p>Vinculación: ";
  if (dispCodigo != "" && vinculacion == "") html += "<span class='ok'>lista</span>";
  else if (vinculacion != "") html += "esperando al servidor…";
  else html += "<span class='mal'>" + escaparHtml(estadoVinculacion) + "</span> <a href='/'>Volver</a>";
  html += "</p>";
  if (conectado && dispCodigo != "" && vinculacion == "") html += "<p class='ok'>¡Listo! Ya podés volver a tu WiFi y abrir la app: el aula aparece En línea. Esta red se apaga sola en un minuto.</p>";
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

// Cualquier dirección lleva a la página de configuración (así el celular la abre solo)
void paginaNoEncontrada() {
  if (portalActivo) {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302);
  } else {
    server.send(404, "text/plain", "No encontrado");
  }
}

// ----- Página local de control manual (desde la misma red: http://IP-del-aparato/control) -----
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

void volverAlControl() {
  server.sendHeader("Location", "/control");
  server.send(303);
}
void activarAutomatico() { modoAutomatico = true; volverAlControl(); }
void activarManual() { modoAutomatico = false; volverAlControl(); }
void lucesOn() { if (!modoAutomatico) { lucesEncendidas = true; digitalWrite(PIN_RELE_LUZ, LOW); } volverAlControl(); }
void lucesOff() { if (!modoAutomatico) { lucesEncendidas = false; digitalWrite(PIN_RELE_LUZ, HIGH); } volverAlControl(); }
void ventiladorOn() { if (!modoAutomatico) { ventiladorEncendido = true; digitalWrite(PIN_RELE_VENT, RELE_PRENDIDO); } volverAlControl(); }
void ventiladorOff() { if (!modoAutomatico) { ventiladorEncendido = false; digitalWrite(PIN_RELE_VENT, RELE_APAGADO); } volverAlControl(); }
void abrirCortina() { if (!modoAutomatico) { digitalWrite(PIN_RELE_SERVO, LOW); cortinaCerrada = false; servoCortina.write(0); } volverAlControl(); }
void cerrarCortina() { if (!modoAutomatico) { digitalWrite(PIN_RELE_SERVO, LOW); cortinaCerrada = true; servoCortina.write(180); } volverAlControl(); }
void aireOn() { if (!modoAutomatico) { aireEncendido = true; digitalWrite(PIN_IR, HIGH); } volverAlControl(); }
void aireOff() { if (!modoAutomatico) { aireEncendido = false; digitalWrite(PIN_IR, LOW); } volverAlControl(); }
