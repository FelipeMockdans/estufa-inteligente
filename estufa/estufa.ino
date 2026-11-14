#include <WiFi.h>
#include "secrets.h"
#include <WebServer.h>
#include <DHT.h>
#include <Preferences.h>
#include <Wire.h>
#include <BH1750.h>
#include <HTTPClient.h>

const char* SERVIDOR = "http://192.168.137.1:5000/api/leituras";  // IP do notebook

#define DHTPIN 4
#define DHTTYPE DHT11   
#define SENSOR 32
#define RELE 26
#define RELE_LUZ 27
#define SDA_BH1750 18
#define SCL_BH1750 19

DHT dht(DHTPIN, DHTTYPE);
BH1750 lightMeter;
WebServer server(80);
Preferences prefs;

int PCT_LIGAR = 15;          // abaixo disso (%): liga a bomba
int PCT_DESLIGAR = 60;       // a partir disso (%): desliga a bomba
int ADC_SECO = 3236;         // leitura com o sensor no ar (0%)
int ADC_MOLHADO = 1164;      // leitura com o sensor na agua (100%)

float LUX_MAX = 1000.0;            // lux que corresponde a 100%
float LIMITE_LUZ_LIGAR = 10.0;     // abaixo disso (%): liga a luminaria
float LIMITE_LUZ_DESLIGAR = 15.0;  // acima disso (%): desliga a luminaria

bool modoAutoBomba = true, modoAutoLuz = true;
bool bombaLigada = false, luzLigada = false;
bool bh1750Disponivel = false;
int solo = 0, soloPct = 0;
float umidadeAr = 0, temperatura = 0;
float luminosidadeLux = 0, luminosidadePct = 0;

String wifiSsid, wifiSenha;
unsigned long tSolo = 0, tDHT = 0, tWiFi = 0, tSerial = 0, tLuz = 0, tEnvio = 0;
bool avisou = false;

// reles ativos em LOW
void ligarBomba()    { digitalWrite(RELE, LOW);  bombaLigada = true;  }
void desligarBomba() { digitalWrite(RELE, HIGH); bombaLigada = false; }
void ligarLuz()      { digitalWrite(RELE_LUZ, LOW);  luzLigada = true;  }
void desligarLuz()   { digitalWrite(RELE_LUZ, HIGH); luzLigada = false; }

int lerSolo() {
  long soma = 0;
  for (int i = 0; i < 16; i++) soma += analogRead(SENSOR);
  return soma / 16;
}

int calcularPct(int leitura) {
  int p = map(leitura, ADC_SECO, ADC_MOLHADO, 0, 100);
  return constrain(p, 0, 100);
}

float luxParaPct(float lux) {
  float p = (lux / LUX_MAX) * 100.0;
  return constrain(p, 0.0, 100.0);
}

void lerDHT() {
  float u = dht.readHumidity(), t = dht.readTemperature();
  if (!isnan(u) && !isnan(t)) { umidadeAr = u; temperatura = t; }
  else Serial.println("Erro ao ler o DHT (mantendo ultima leitura)");
}

void eventoWiFi(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    int m = info.wifi_sta_disconnected.reason;
    Serial.print("Wi-Fi desconectado. Motivo: ");
    Serial.print(m);
    if (m == 201) Serial.println(" -> Rede NAO encontrada (nome errado ou 5 GHz)");
    else if (m == 202 || m == 15 || m == 204 || m == 2) Serial.println(" -> Senha errada ou WPA3");
    else if (m == 200) Serial.println(" -> Sinal perdido");
    else Serial.println();
  }
}

void listarRedes() {
  Serial.println("Procurando redes...");
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("Nenhuma rede encontrada (antena/alimentacao?)");
    return;
  }
  bool achou = false;
  for (int i = 0; i < n; i++) {
    Serial.println(String(i + 1) + ": " + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) +
                   " dBm, canal " + String(WiFi.channel(i)) + ")");
    if (WiFi.SSID(i) == wifiSsid) achou = true;
  }
  Serial.println(achou ? ">> Sua rede FOI encontrada."
                       : ">> Sua rede NAO apareceu (nome errado ou 5 GHz).");
  WiFi.scanDelete();
}

void conectarWiFi() {
  if (wifiSsid.length() == 0) return;
  Serial.println("Conectando em: " + wifiSsid);
  WiFi.disconnect();
  WiFi.begin(wifiSsid.c_str(), wifiSenha.c_str());
  tWiFi = millis();
}

void iniciarWiFi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(eventoWiFi);

  prefs.begin("wifi", true);
  wifiSsid  = prefs.getString("ssid", WIFI_SSID);
  wifiSenha = prefs.getString("senha", WIFI_PASSWORD);
  prefs.end();

  listarRedes();
  conectarWiFi();
}

// pega o texto de um campo do JSON, ex.: "cmd_bomba":"ligar" -> ligar
String pegarValor(const String& json, const String& chave) {
  int i = json.indexOf("\"" + chave + "\"");
  if (i < 0) return "";
  i = json.indexOf(':', i);
  if (i < 0) return "";
  int a = json.indexOf('"', i);
  if (a < 0) return "";
  int b = json.indexOf('"', a + 1);
  if (b < 0) return "";
  return json.substring(a + 1, b);
}

// aplica os comandos que o servidor mandou na resposta
void aplicarComandos(const String& resposta) {
  String cb = pegarValor(resposta, "cmd_bomba");
  if (cb == "ligar")         { modoAutoBomba = false; if (!bombaLigada) ligarBomba(); }
  else if (cb == "desligar") { modoAutoBomba = false; if (bombaLigada) desligarBomba(); }
  else if (cb == "auto")     { modoAutoBomba = true; }

  String cl = pegarValor(resposta, "cmd_luz");
  if (cl == "ligar")         { modoAutoLuz = false; if (!luzLigada) ligarLuz(); }
  else if (cl == "desligar") { modoAutoLuz = false; if (luzLigada) desligarLuz(); }
  else if (cl == "auto")     { modoAutoLuz = true; }
}

// envia as leituras e recebe os comandos na resposta
void enviarParaServidor() {
  if (WiFi.status() != WL_CONNECTED) return;

  String json = "{\"solo_pct\":" + String(soloPct) +
                ",\"umidade_ar\":" + String(umidadeAr, 1) +
                ",\"temperatura\":" + String(temperatura, 1) +
                ",\"luminosidade_pct\":" + String(luminosidadePct, 0) +
                ",\"bomba\":" + (bombaLigada ? "true" : "false") +
                ",\"luminaria\":" + (luzLigada ? "true" : "false") + "}";

  HTTPClient http;
  http.setConnectTimeout(800);
  http.setTimeout(1000);
  http.begin(SERVIDOR);
  http.addHeader("Content-Type", "application/json");
  int codigo = http.POST(json);
  Serial.println("Servidor: codigo " + String(codigo));
  if (codigo == 200) aplicarComandos(http.getString());
  http.end();
}

String esc(const String& s) {
  String r;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') r += "&amp;";
    else if (c == '<') r += "&lt;";
    else if (c == '>') r += "&gt;";
    else if (c == '"') r += "&quot;";
    else if (c == '\'') r += "&#39;";
    else r += c;
  }
  return r;
}

String topo(bool refresh) {
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  h += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  if (refresh) h += "<meta http-equiv='refresh' content='5'>";
  h += "<title>Irrigacao</title><style>body{font-family:sans-serif;max-width:420px;margin:20px auto;padding:0 15px;}";
  h += ".card{background:#f2f2f2;border-radius:10px;padding:12px;margin:10px 0;}";
  h += "a.btn,button{display:block;width:100%;box-sizing:border-box;text-align:center;padding:14px;margin:8px 0;";
  h += "border-radius:8px;border:0;color:#fff;text-decoration:none;font-size:18px;}";
  h += "input{width:100%;box-sizing:border-box;padding:12px;margin:6px 0;font-size:16px;}";
  h += ".on{background:#2e8b57;}.off{background:#c0392b;}.auto{background:#2980b9;}.cz{background:#7f8c8d;}</style></head><body>";
  return h;
}

void voltar(const char* destino = "/") {
  server.sendHeader("Location", destino);
  server.send(303);
}

void paginaPrincipal() {
  bool ok = WiFi.status() == WL_CONNECTED;
  String h = topo(true) + "<h2>Irrigacao automatica</h2>";
  h += "<div class='card'>Umidade do solo: <b>" + String(soloPct) + " %</b></div>";
  h += "<div class='card'>Umidade do ar: <b>" + String(umidadeAr, 1) + " %</b></div>";
  h += "<div class='card'>Temperatura: <b>" + String(temperatura, 1) + " C</b></div>";
  h += "<div class='card'>Luminosidade: <b>" + String(luminosidadePct, 0) + " %</b></div>";
  h += "<div class='card'>Bomba: <b>" + String(bombaLigada ? "LIGADA" : "desligada") + "</b> (" +
       String(modoAutoBomba ? "automatico" : "manual") + ")</div>";
  h += "<div class='card'>Luminaria: <b>" + String(luzLigada ? "LIGADA" : "desligada") + "</b> (" +
       String(modoAutoLuz ? "automatico" : "manual") + ")</div>";
  h += "<div class='card'>Wi-Fi: <b>" + (ok ? "conectado em " + esc(wifiSsid) + " (" + WiFi.localIP().toString() + ")"
                                             : String("sem conexao com o roteador")) + "</b></div>";

  h += "<a class='btn on' href='/ligar'>Ligar bomba (manual)</a>";
  h += "<a class='btn off' href='/desligar'>Desligar bomba (manual)</a>";
  h += "<a class='btn auto' href='/auto'>Bomba: voltar ao automatico</a>";
  h += "<a class='btn on' href='/luzligar'>Ligar luminaria (manual)</a>";
  h += "<a class='btn off' href='/luzdesligar'>Desligar luminaria (manual)</a>";
  h += "<a class='btn auto' href='/luzauto'>Luminaria: voltar ao automatico</a>";
  h += "<a class='btn cz' href='/wifi'>Configurar Wi-Fi</a></body></html>";
  server.send(200, "text/html", h);
}

void paginaWiFi() {
  int n = WiFi.scanNetworks();
  String h = topo(false) + "<h2>Configurar Wi-Fi</h2>";
  h += "<form method='POST' action='/salvarwifi'>Nome da rede (so 2,4 GHz):";
  h += "<input name='ssid' list='redes' required autocomplete='off'><datalist id='redes'>";
  for (int i = 0; i < n; i++) h += "<option value='" + esc(WiFi.SSID(i)) + "'>";
  h += "</datalist>Senha:<input name='senha' type='password'>";
  h += "<button class='on' type='submit'>Salvar e conectar</button></form>";
  h += "<h3>Redes que o ESP32 enxerga</h3>";
  if (n <= 0) h += "<div class='card'>Nenhuma rede encontrada.</div>";
  for (int i = 0; i < n; i++)
    h += "<div class='card'>" + esc(WiFi.SSID(i)) + " (" + String(WiFi.RSSI(i)) + " dBm, canal " + String(WiFi.channel(i)) + ")</div>";
  WiFi.scanDelete();
  h += "<a class='btn auto' href='/wifi'>Procurar de novo</a><a class='btn cz' href='/'>Voltar</a></body></html>";
  server.send(200, "text/html", h);
}

void salvarWiFi() {
  String ssid = server.arg("ssid"), senha = server.arg("senha");
  if (ssid.length() == 0) { voltar("/wifi"); return; }

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("senha", senha);
  prefs.end();
  wifiSsid = ssid;
  wifiSenha = senha;

  server.send(200, "text/html", topo(false) + "<h2>Salvo!</h2><div class='card'>Conectando em <b>" + esc(ssid) +
              "</b>. Aguarde ~15 s. O novo IP aparece no Monitor Serial.</div>"
              "<a class='btn cz' href='/'>Pagina inicial</a></body></html>");
  delay(300);
  conectarWiFi();
}

void rotaDados() {
  String j = "{\"solo\":" + String(solo) +
             ",\"solo_pct\":" + String(soloPct) +
             ",\"umidade_ar\":" + String(umidadeAr, 1) +
             ",\"temperatura\":" + String(temperatura, 1) +
             ",\"bomba\":" + (bombaLigada ? "true" : "false") +
             ",\"luminosidade_lux\":" + String(luminosidadeLux, 1) +
             ",\"luminosidade_pct\":" + String(luminosidadePct, 0) +
             ",\"luminaria\":" + (luzLigada ? "true" : "false") +
             ",\"modo_bomba\":\"" + (modoAutoBomba ? "auto" : "manual") + "\"" +
             ",\"modo_luz\":\"" + (modoAutoLuz ? "auto" : "manual") + "\"}";
  server.send(200, "application/json", j);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(RELE, OUTPUT);
  desligarBomba();
  pinMode(RELE_LUZ, OUTPUT);
  desligarLuz();

  dht.begin();
  analogReadResolution(12);

  Wire.begin(SDA_BH1750, SCL_BH1750);
  bh1750Disponivel = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  Serial.println(bh1750Disponivel ? "BH1750 iniciado com sucesso!" : "Erro ao iniciar BH1750!");

  iniciarWiFi();

  server.on("/", paginaPrincipal);
  server.on("/ligar",       [] { modoAutoBomba = false; ligarBomba();    voltar(); });
  server.on("/desligar",    [] { modoAutoBomba = false; desligarBomba(); voltar(); });
  server.on("/auto",        [] { modoAutoBomba = true;                   voltar(); });
  server.on("/luzligar",    [] { modoAutoLuz = false;   ligarLuz();      voltar(); });
  server.on("/luzdesligar", [] { modoAutoLuz = false;   desligarLuz();   voltar(); });
  server.on("/luzauto",     [] { modoAutoLuz = true;                     voltar(); });
  server.on("/dados", rotaDados);
  server.on("/wifi", paginaWiFi);
  server.on("/salvarwifi", HTTP_POST, salvarWiFi);
  server.begin();

  solo = lerSolo();
  soloPct = calcularPct(solo);
}

void loop() {
  server.handleClient();
  unsigned long agora = millis();

  // envia as leituras e recebe comandos a cada 1 segundo
  if (agora - tEnvio >= 1000) {
    tEnvio = agora;
    enviarParaServidor();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!avisou) {
      avisou = true;
      Serial.println("CONECTADO! Acesse: http://" + WiFi.localIP().toString());
    }
  } else {
    avisou = false;
    if (wifiSsid.length() > 0 && agora - tWiFi > 20000) {
      Serial.println("Tentando reconectar...");
      conectarWiFi();
    }
  }

  if (agora - tSolo >= 100) {
    tSolo = agora;
    solo = lerSolo();
    soloPct = calcularPct(solo);

    if (modoAutoBomba) {
      if (!bombaLigada && soloPct < PCT_LIGAR) ligarBomba();
      else if (bombaLigada && soloPct >= PCT_DESLIGAR) desligarBomba();
    }
  }

  // le o DHT so com a bomba desligada (evita ruido)
  if (!bombaLigada && agora - tDHT > 5000) {
    tDHT = agora;
    lerDHT();
  }

  if (bh1750Disponivel && agora - tLuz >= 1000) {
    tLuz = agora;
    float lux = lightMeter.readLightLevel();
    if (lux >= 0) {
      luminosidadeLux = lux;
      luminosidadePct = luxParaPct(lux);
      if (modoAutoLuz) {
        if (!luzLigada && luminosidadePct < LIMITE_LUZ_LIGAR) ligarLuz();
        else if (luzLigada && luminosidadePct > LIMITE_LUZ_DESLIGAR) desligarLuz();
      }
    }
  }

  if (agora - tSerial >= 1000) {
    tSerial = agora;
    Serial.println("Solo bruto: " + String(solo) +
                   " | Solo: " + String(soloPct) + "%" +
                   " | Bomba: " + (bombaLigada ? "LIGADA" : "desligada") +
                   " | Luz: " + (luzLigada ? "LIGADA" : "desligada") +
                   " | Luminosidade: " + String(luminosidadePct, 0) + "%" +
                   " | Ar: " + String(umidadeAr, 1) + "%" +
                   " | Temp: " + String(temperatura, 1) + " C" +
                   " | Wi-Fi: " + (WiFi.status() == WL_CONNECTED ? "conectado" : "sem conexao"));
  }
}