// V2.0 - 05/2026 - Meteovision Nano by Davide Gatti - 2026 - www.survivalhacking.it
//                  Conversione del MeteoVision in formato mini.

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>            // Libreria grafica per display SSD1306 72x40
#include <Adafruit_NeoPixel.h>  // Controllo anello LED WS2812

// Gestione connessione WiFi automatica
#include <WiFiManager.h>  // Permette setup Wi-Fi via interfaccia web

// Richieste HTTP per ottenere i dati meteo
#include <HTTPClient.h>
#include <ArduinoJson.h>  // Parsing JSON della risposta OpenWeather

#include "graphics.h"  // Contiene animazioni bitmap per display

// -- LIBRERIE PER GESTIONE TEMPO --
// Tempo standard C
#include <ctime>       // Include strutture come time_t, struct tm
#include <time.h>      // Funzioni di conversione ora/data
#include <sys/time.h>  // Per strutture temporali compatibili con ESP32

// Librerie NTP (Network Time Protocol)
#include <NTPClient.h>  // Permette sincronizzazione ora via Internet
#include <WiFiUdp.h>    // Necessario per comunicazione UDP usata da NTPClient

// Librerie custom per server web
#include <WebServer.h>    // Server HTTP in esecuzione su ESP32
#include <Preferences.h>  // Per salvare configurazioni persistenti

#include "secret.h"       // File con chiavi/API sensibili
#include "wifi_config.h"  // Configurazione WiFi manuale

// -- DISPLAY SSD1306 72x40 su ESP32-C3 --
// SDA=GPIO5, SCL=GPIO6 (riservati al display - NON usare per altro)
// Buffer virtuale 128x64, area visibile 72x40 con offset OX=30 OY=25
#define DISP_W   72
#define DISP_H   40
#define DISP_OX  30
#define DISP_OY  25
#define DX(x)   ((x) + DISP_OX)
#define DY(y)   ((y) + DISP_OY)
#define LOGO_Y_ABS 25

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, /*SCL*/6, /*SDA*/5);

int getTextWidth(const String &text, int textSize) { return text.length() * 6 * textSize; }

void dispMsg(const char* r1, const char* r2="", const char* r3="", const char* r4="") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);
  if (r1[0]) u8g2.drawStr(DX(0), DY(7),  r1);
  if (r2[0]) u8g2.drawStr(DX(0), DY(16), r2);
  if (r3[0]) u8g2.drawStr(DX(0), DY(25), r3);
  if (r4[0]) u8g2.drawStr(DX(0), DY(29), r4);
  u8g2.sendBuffer();
}

// -- DEFINIZIONI LED METEO INDIVIDUALI --
// GPIO5=SDA GPIO6=SCL riservati al display
#define PIN_LED_SOLE      4
#define PIN_LED_NUVOLE    7
#define PIN_LED_PIOGGIA   8
#define PIN_LED_TEMPORALE 9
#define PIN_LED_LUNA      10

// -- DEFINIZIONI PER ANELLO NEOPIXEL --
#define PIN_NEOPIXEL 3
#define NUM_NEOPIXELS 12
Adafruit_NeoPixel pixels(NUM_NEOPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// -- DEFINIZIONE PULSANTE FISICO --
#define PIN_PULSANTE 2

// -- DEFINIZIONI PER TEMPISMO DI SCHERMATA E METEO --
#define SCREEN_TEXT_DURATION 8000              // MODIFICATO: Ridotto per display più piccolo
#define SCREEN_ANIMATION_DURATION 5000         // MODIFICATO: Ridotto per display più piccolo
#define WEATHER_FETCH_INTERVAL 10 * 60 * 1000  // Ogni 10 minuti (in ms)

// Parametri effetto "respiro" LED (NeoPixel)
#define NEOPIXEL_BREATH_MIN 25       // Luminosità minima effetto respiro
#define NEOPIXEL_BREATH_MAX 255      // Luminosità massima
#define BREATH_UPDATE_SPEED 5        // Velocità animazione
#define NEOPIXEL_BOOT_ANIM_DELAY 50  // Delay tra accensioni LED iniziali

// Variabili per la gestione del pulsante
unsigned long buttonPressStartTime = 0;
bool buttonState = HIGH;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
bool keypressed = false;

// Variabili per pressione lunga del pulsante
bool isLongPressMode = false;
const unsigned long LONG_PRESS_THRESHOLD = 1000;
const int BRIGHTNESS_CHANGE_INTERVAL = 100;
unsigned long lastBrightnessChangeTime = 0;

unsigned long lastScreenChange = 0;
bool showText = true;

// -- VARIABILI DI CONFIGURAZIONE PERSISTENTE --
Preferences preferences;

// Valori persistenti da conservare
char savedCity[40] = "Monza";
char savedCountryCode[5] = "IT";
int savedNeopixelBrightness = 200;
int savedMode = 0;
bool DisplayOneTime = false;
int Currentmode = 0;
int forecastDay = -1; // -1: Attuale (sempre all'accensione)
int forecastPeriod = 0; // 0: Mattino, 1: Pomeriggio, 2: Sera
#define MAX_MODE 11
bool nightModeEnabled = false;
int nightStartHour = 22;
int nightStartMinute = 00;
int nightEndHour = 7;
int nightEndMinute = 00;
bool windAnimationEnabled = true;
bool neopixelAnimationEnabled = true; // Nuova variabile per animazione cambio modalità

// Variabili per la visualizzazione del nome della modalità
bool showModeName = false;
unsigned long modeNameStartTime = 0;
const unsigned long MODE_NAME_DISPLAY_DURATION = 2000; // 2 secondi

// Definizione delle modalità disponibili
#define MODO_AUTO 0
#define MODO_ROSSO 1
#define MODO_VERDE 2
#define MODO_BLU 3
#define MODO_VIOLA 4
#define MODO_CIANO 5
#define MODO_GIALLO 6
#define MODO_BIANCO 7
#define MODO_RING_OFF 8
#define MODO_LED_OFF 9
#define MODO_ALL_OFF 10
#define MODO_NOTTE 11

// -- MODALITÀ FIERA --
bool fieraMode = false;  // true = modalità fiera attiva (nessun WiFi)

// Città italiane usate in modalità fiera
const char* fieraCities[] = {
  "Milano", "Roma", "Napoli", "Torino", "Firenze",
  "Bologna", "Venezia", "Palermo", "Genova", "Bari",
  "Catania", "Verona", "Padova", "Trieste", "Brescia",
  "Bergamo", "Modena", "Parma", "Reggio Emilia", "Perugia",
  "Ancona", "Cagliari", "Foggia", "Salerno", "Ferrara"
};
const int FIERA_CITY_COUNT = 25;

// Dati simulati per la modalità fiera
int fieraCityIndex = 0;
unsigned long fieraLastCityChange = 0;
const unsigned long FIERA_CITY_INTERVAL = 3600000UL; // 1 ora in ms

// Condizioni meteo simulate
const int fieraConditions[] = {800, 801, 802, 500, 501, 200, 600, 741};
const int FIERA_CONDITION_COUNT = 8;

// Epoch fittizio usato in modalità FIERA al posto di timeClient
time_t fieraEpochTime = 0;

// -- VARIABILI GLOBALI NON SALVATE IN MEMORIA --
String ProductName = "MeteoVision Nano V2.0";
String weatherDescription = "Ricerca...";
float windSpeed = 0;
int windDirection = 0;
int weatherConditionCode = 0;
float temperatureCelsius = 0.0;
int humidity = 0;
time_t sunriseTime = 0;
time_t sunsetTime = 0;
int animationFrame = 0;
const unsigned long WIND_SCAN_DURATION = 1000;
bool windScanning = false;
bool windAnimationActive = false;
int windScanFrame = 0;
unsigned long windAnimationStartTime = 0;
unsigned long animationStartTime = 0;
int windLedIndex = 0;

// Flag per forzare l'aggiornamento immediato del meteo
bool forceWeatherUpdate = false;

// Timer per l'aggiornamento periodico del meteo
unsigned long lastWeatherFetch = 0;

// -- GESTIONE RETE E SERVER WEB --
WiFiManager wm;
WebServer server(80);

// -- CLIENT NTP PER L'ORARIO DI SISTEMA --
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// -- VARIABILI PER ANIMAZIONE DELL'ANELLO NEOPIXEL --
bool neopixelAnimationActive = true;
uint32_t neopixelBaseColor = pixels.Color(0, 0, 0);
bool neopixelBootAnimationInProgress = false;
bool neopixelModeChangeAnimation = false; // Nuova variabile per animazione cambio modalità
int neopixelAnimStep = 0;
unsigned long neopixelAnimLastUpdate = 0;
const int NEOPIXEL_ANIM_STEPS = NUM_NEOPIXELS + 5; // Numero di passi dell'animazione (LED + 5 extra per completamento)
const unsigned long NEOPIXEL_ANIM_DELAY = 40; // Delay tra i passi (più veloce per effetto fluido)

// Parametro personalizzato globale per WiFiManager
WiFiManagerParameter custom_api_key("api_key", "OpenWeatherMap API Key", "", 60);

// --- Funzione per generare dati meteo simulati in modalità FIERA ---
// (deve stare DOPO le variabili globali che usa)
void generateFieraWeather() {
  int condIdx = random(0, FIERA_CONDITION_COUNT);
  weatherConditionCode = fieraConditions[condIdx];
  weatherDescription = getWeatherDescriptionItalian(weatherConditionCode);
  windAnimationEnabled = false;

  // Temperatura in base alla condizione meteo
  if (weatherConditionCode == 600) {
    temperatureCelsius = random(-5, 4);        // Neve: freddo
  } else if (weatherConditionCode >= 200 && weatherConditionCode < 300) {
    temperatureCelsius = random(14, 24);       // Temporale
  } else if (weatherConditionCode == 800 || weatherConditionCode == 801) {
    temperatureCelsius = random(18, 33);       // Sereno/poco nuvoloso: caldo
  } else {
    temperatureCelsius = random(8, 22);        // Altro
  }

  humidity      = random(40, 91);
  windSpeed = 0; windDirection = 0;

  // Orario simulato tra le 08:00 e le 21:00
  int simHour = random(8, 22);
  int simMin  = random(0, 60);

  // Calcola un epoch fittizio senza usare settimeofday (che richiede il WiFi stack)
  struct tm timeinfo = {};
  timeinfo.tm_hour = simHour;
  timeinfo.tm_min  = simMin;
  timeinfo.tm_sec  = 0;
  timeinfo.tm_year = 125; // 2025
  timeinfo.tm_mon  = 5;   // giugno (0-based)
  timeinfo.tm_mday = 15;
  time_t fakeTime = mktime(&timeinfo);
  fieraEpochTime = fakeTime;  // Salvato, usato da getCurrentEpoch()

  // Alba/tramonto fittizi
  struct tm sr = timeinfo; sr.tm_hour = 6;  sr.tm_min = 15; sunriseTime = mktime(&sr);
  struct tm ss = timeinfo; ss.tm_hour = 20; ss.tm_min = 45; sunsetTime  = mktime(&ss);

  Serial.print("[FIERA] Città: ");          Serial.println(fieraCities[fieraCityIndex]);
  Serial.print("[FIERA] Meteo: ");          Serial.print(weatherDescription);
  Serial.print(" | Temp: ");               Serial.print(temperatureCelsius); Serial.print("°C");
  Serial.print(" | Ora simulata: ");       Serial.print(simHour); Serial.print(":"); Serial.println(simMin);
}

// Wrapper per ottenere l'epoch corrente:
// - in modalità FIERA restituisce l'ora simulata (senza toccare il WiFi)
// - in modalità normale usa timeClient come sempre
time_t getCurrentEpoch() {
  if (fieraMode) {
    return fieraEpochTime;
  }
  return timeClient.getEpochTime();
}

// --- Connessione WiFi Manuale ---
bool connectWiFiManual() {
  Serial.println("\n==================================");
  Serial.println("TENTATIVO CONNESSIONE WIFI MANUALE");
  Serial.println("==================================");
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);
  Serial.print("Timeout: ");
  Serial.print(WIFI_CONNECT_TIMEOUT / 1000);
  Serial.println(" secondi");
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(DX(0),DY(8),"WIFI MANUALE...");
  u8g2.drawStr(DX(0),DY(18),"SSID:");
  { char _b[32]; snprintf(_b,32,"%s",String(WIFI_SSID).c_str()); u8g2.drawStr(DX(0),DY(26),_b); }
  u8g2.sendBuffer();
  
  WiFi.disconnect(true);  
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  unsigned long startAttempt = millis();
  int dotCount = 0;
  
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < WIFI_CONNECT_TIMEOUT) {
    delay(500);
    Serial.print(".");
    
    dotCount = (dotCount + 1) % 4;
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(DX(0),DY(8),"CONNESSIONE");
        for(int i = 0; i < dotCount; i++) {
    }
    { char _t[8]; snprintf(_t,8,"%lus",(millis()-startAttempt)/1000); u8g2.drawStr(DX(32),DY(28),_t); }
    u8g2.sendBuffer();
  }
  
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✓ CONNESSIONE RIUSCITA!");
    Serial.print("Indirizzo IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal Strength (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(DX(0),DY(8),"WIFI CONNESSO!");
    { char _ip[20]; snprintf(_ip,20,"%s",WiFi.localIP().toString().c_str()); u8g2.drawStr(DX(0),DY(18),_ip); }
    { char _rs[16]; snprintf(_rs,16,"%ddBm",WiFi.RSSI()); u8g2.drawStr(DX(0),DY(28),_rs); }
    u8g2.sendBuffer();
    delay(3000);
    
    return true;
  } else {
    Serial.println("✗ CONNESSIONE FALLITA!");
    Serial.print("Stato WiFi: ");
    
    switch(WiFi.status()) {
      case WL_NO_SSID_AVAIL:
        Serial.println("SSID non trovato");
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(DX(0),DY(16),"ERRORE WIFI:");
        u8g2.drawStr(DX(0),DY(24),"SSID NON TROVATO");
        break;
      case WL_CONNECT_FAILED:
        Serial.println("Password errata");
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(DX(0),DY(16),"ERRORE WIFI:");
        u8g2.drawStr(DX(0),DY(24),"PASSWORD ERRATA");
        break;
      default:
        Serial.println("Errore sconosciuto");
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(DX(0),DY(16),"ERRORE WIFI:");
        u8g2.drawStr(DX(0),DY(24),"TIMEOUT");
        break;
    }
    u8g2.sendBuffer();
    delay(3000);
    
    return false;
  }
}


// --- Mappatura traduzioni OpenWeatherMap (da codice numerico a descrizione ITA) ---
String getWeatherDescriptionItalian(int conditionCode) {
  if (conditionCode >= 200 && conditionCode < 300) return "Temporale";
  if (conditionCode >= 300 && conditionCode < 400) return "Pioggerella";
  if (conditionCode >= 500 && conditionCode < 600) {
    if (conditionCode == 500) return "Pioggia leggera";
    if (conditionCode == 501) return "Pioggia moderata";
    if (conditionCode == 502) return "Pioggia intensa";
    if (conditionCode == 503) return "Pioggia molto intensa";
    if (conditionCode == 504) return "Pioggia estrema";
    if (conditionCode == 511) return "Pioggia gelata";
    if (conditionCode == 520) return "Pioggerella intensa";
    if (conditionCode == 521) return "Acquazzone";
    if (conditionCode == 522) return "Forti acquazzoni";
    if (conditionCode == 531) return "Acquazzone irregolare";
    return "Pioggia";
  }
  if (conditionCode >= 600 && conditionCode < 700) return "Neve";
  if (conditionCode >= 700 && conditionCode < 800) {
    if (conditionCode == 701) return "Nebbia";
    if (conditionCode == 711) return "Fumo";
    if (conditionCode == 721) return "Foschia";
    if (conditionCode == 731) return "Vortici di sabbia/polvere";
    if (conditionCode == 741) return "Nebbia";
    if (conditionCode == 751) return "Sabbia";
    if (conditionCode == 761) return "Polvere";
    if (conditionCode == 762) return "Cenere vulcanica";
    if (conditionCode == 771) return "Colpo di vento";
    if (conditionCode == 781) return "Tornado";
    return "Atmosferico";
  }
  if (conditionCode == 800) return "Cielo sereno";
  if (conditionCode == 801) return "Poche nuvole";
  if (conditionCode == 802) return "Nuvole sparse";
  if (conditionCode == 803) return "Nuvole irregolari";
  if (conditionCode == 804) return "Cielo coperto";

  return "Sconosciuto";
}

// --- PROTOTIPI DI FUNZIONI ---
bool connectWiFiManual();
void saveConfig();
void loadConfig();
void saveConfigCallback();
void configModeCallback(WiFiManager *myWiFiManager);
void fetchWeather();
void fetchCurrentWeather();
void fetchForecastWeather();
void displayWeatherInfo();
void displayWeatherAnimation();
void updateWeatherLEDs();
void runNeopixelAnimation();
void setNeopixelColor(uint32_t color);
void animateNeopixelCircular();
void handleButtonPress();
void onGetCommand(unsigned char device_id, const char *device_name, bool state, unsigned char value);
time_t getNtpTime();
void AllLEDoff();
void SetRingColor(int mode, bool animate = false); // Modificato per includere parametro animazione
int getBeaufortScale(float windSpeed);
void getWindColorByBeaufort(int beaufort, uint8_t &r, uint8_t &g, uint8_t &b);
int getTrailLengthByBeaufort(int beaufort);
void runWindAnimation();
void runWindDirectionScan(int windLedIndex);
void checkNightMode();
String urlEncode(const String& str);
void drawBrightnessBar(uint8_t brightness);
void bootAnimation();
void runNeopixelModeChangeAnimation(); // Nuova funzione per animazione cambio modalità

// Gestori HTTP per il server web integrato
void handleRoot();
void handleSave();
// Modalità FIERA
void generateFieraWeather();
time_t getCurrentEpoch();

// Stato per l'animazione display
unsigned long lastFrameChange = 0;
int currentFrame = 0;
bool dualAnimation = false;
static int animationDirection = 1;
const int ANIM_FRAME_DELAY = 200;

// Wrapper semplificato per ottenere l'epoch time dal client NTP
time_t getNtpTime() {
  return timeClient.getEpochTime();
}

// --- Funzione per animazione di accensione ---
void bootAnimation() {
  Serial.println("Avvio animazione di accensione...");
  
  // Array con tutti i pin LED individuali
  int ledPins[] = {PIN_LED_SOLE, PIN_LED_NUVOLE, PIN_LED_PIOGGIA, PIN_LED_TEMPORALE, PIN_LED_LUNA};
  int numLeds = sizeof(ledPins) / sizeof(ledPins[0]);
  
  // Sequenza di accensione dei LED individuali
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(ledPins[i], HIGH);
    delay(300);
    digitalWrite(ledPins[i], LOW);
  }
  
  // Accensione completa di tutti i LED individuali
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(ledPins[i], HIGH);
  }
  delay(500);
  
  // Spegnimento completo dei LED individuali
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(ledPins[i], LOW);
  }
  delay(300);
  
  // Animazione anello NeoPixel - accensione sequenziale
  for (int i = 0; i < NUM_NEOPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(255, 255, 255));
    pixels.show();
    delay(50);
  }
  delay(500);
  
  // Animazione anello NeoPixel - spegnimento sequenziale
  for (int i = 0; i < NUM_NEOPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(0, 0, 0));
    pixels.show();
    delay(50);
  }
  delay(300);
  
  // Accensione completa anello NeoPixel
  for (int i = 0; i < NUM_NEOPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(255, 255, 255));
  }
  pixels.show();
  delay(500);
  
  // Accensione completa di tutti i LED individuali
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(ledPins[i], HIGH);
  }
  delay(1000);
  
  // Spegnimento completo di tutto
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(ledPins[i], LOW);
  }
  pixels.clear();
  pixels.show();
    delay(100);  // Breve attesa prima di mostrare il logo
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  Serial.println("Animazione di accensione completata");
}

// --- Funzione per animazione cambio modalità NeoPixel (ONDA FLUIDA) ---
void runNeopixelModeChangeAnimation() {
  if (neopixelModeChangeAnimation) {
    unsigned long currentMillis = millis();
    
    if (currentMillis - neopixelAnimLastUpdate >= NEOPIXEL_ANIM_DELAY) {
      neopixelAnimLastUpdate = currentMillis;
      
      // Calcola il colore di destinazione in base alla modalità
      uint32_t targetColor = neopixelBaseColor;
      
      // Esegui i diversi passi dell'animazione a onda fluida
      switch (neopixelAnimStep) {
        case 0:
          // Primo passo: spegni tutti i LED
          for (int i = 0; i < NUM_NEOPIXELS; i++) {
            pixels.setPixelColor(i, pixels.Color(0, 0, 0));
          }
          pixels.show();
          break;
          
        case 1:
          // Secondo passo: accendi il primo LED
          pixels.setPixelColor(0, targetColor);
          pixels.show();
          break;
          
        default:
          // Passi successivi: accendi progressivamente i LED successivi
          if (neopixelAnimStep - 1 < NUM_NEOPIXELS) {
            // Accendi il LED corrente
            pixels.setPixelColor(neopixelAnimStep - 1, targetColor);
            
            // Mantieni accesi anche i LED precedentes
            for (int i = 0; i < neopixelAnimStep - 1; i++) {
              pixels.setPixelColor(i, targetColor);
            }
            
            pixels.show();
          }
          break;
      }
      
      neopixelAnimStep++;
      
      // Se abbiamo completato l'animazione di tutti i LED + alcuni passi extra per stabilizzare
      if (neopixelAnimStep >= NEOPIXEL_ANIM_STEPS) {
        // Assicurati che tutti i LED siano accesi con il colore finale
        for (int i = 0; i < NUM_NEOPIXELS; i++) {
          pixels.setPixelColor(i, targetColor);
        }
        pixels.show();
        
        // Termina l'animazione
        neopixelModeChangeAnimation = false;
        Serial.println("Animazione cambio modalità completata");
      }
    }
  }
}

// --- Setup ---
void setup() {
  Serial.begin(115200);

  // Pulsante inizializzato SUBITO per rilevare pressione all'avvio
  pinMode(PIN_PULSANTE, INPUT_PULLUP);
  bool buttonHeldAtBoot = (digitalRead(PIN_PULSANTE) == LOW);

  loadConfig();

  Wire.begin(5, 6);
  u8g2.begin();
  u8g2.setContrast(255);
  u8g2.setBusClock(400000);

  u8g2.clearBuffer();
  u8g2.drawBitmap(DISP_OX - 1, LOGO_Y_ABS, (logo_width+7)/8, logo_height, logo_data);
  u8g2.sendBuffer();
  delay(2000);

  pinMode(PIN_LED_SOLE, OUTPUT);
  pinMode(PIN_LED_NUVOLE, OUTPUT);
  pinMode(PIN_LED_PIOGGIA, OUTPUT);
  pinMode(PIN_LED_TEMPORALE, OUTPUT);
  pinMode(PIN_LED_LUNA, OUTPUT);
  AllLEDoff();

  pixels.begin();
  pixels.setBrightness(savedNeopixelBrightness);
  pixels.show();

  // Esegui l'animazione di accensione
  bootAnimation();

  // ---------------------------------------------------------
  // GESTIONE PULSANTE ALL'AVVIO:
  //  - Pulsante non premuto        → avvio normale
  //  - Premuto e rilasciato < 5s   → MODALITÀ FIERA
  //  - Tenuto premuto >= 5s        → RESET WIFI (comportamento originale)
  // ---------------------------------------------------------
  if (buttonHeldAtBoot) {

    unsigned long holdStart = millis();
    bool resetTriggered = false;

    // Mostra subito "FIERA" sul display
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x13B_tr);
      u8g2.drawStr(DX(28),DY(16),"FIERA");
    u8g2.sendBuffer();
    Serial.println("Pulsante tenuto premuto all'avvio - in attesa...");

    // Aspetta finché il pulsante è premuto o scadono 5 secondi
    while (digitalRead(PIN_PULSANTE) == LOW) {
      unsigned long elapsed = millis() - holdStart;

      if (elapsed >= 5000 && !resetTriggered) {
        // Passati 5 secondi → mostra "RESET WIFI"
        resetTriggered = true;
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_5x7_tr);
              u8g2.drawStr(DX(10),DY(12),"RESET WIFI");
        u8g2.drawStr(DX(0),DY(24),"RILASCIA PER CONFERMARE");
        u8g2.sendBuffer();
        Serial.println("Soglia 5s raggiunta: modalità RESET WIFI");
      }
      delay(10);
    }

    if (resetTriggered) {
      // Il pulsante è stato tenuto >= 5 secondi → Reset WiFi
      Serial.println("Reset WiFi e configurazione.");
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_5x7_tr);
          u8g2.drawStr(DX(0),DY(20),"RESET WIFI E CONFIG...");
      u8g2.sendBuffer();
      delay(1000);
      preferences.begin("meteo-config", false);
      preferences.clear();
      preferences.end();
      wm.resetSettings();
      ESP.restart();
    } else {
      // Pulsante rilasciato prima di 5 secondi → MODALITÀ FIERA
      fieraMode = true;
      randomSeed(millis());
      fieraCityIndex = random(0, FIERA_CITY_COUNT);
      strncpy(savedCity, fieraCities[fieraCityIndex], sizeof(savedCity) - 1);
      savedCity[sizeof(savedCity) - 1] = '\0';
      generateFieraWeather();
      fieraLastCityChange = millis();

      Serial.println("=== MODALITÀ FIERA ATTIVATA ===");
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_5x7_tr);
          u8g2.drawStr(DX(22),DY(12),"MODALITA' FIERA");
      u8g2.drawStr(DX(10),DY(24),"AVVIO OFFLINE...");
      u8g2.sendBuffer();
      delay(2000);

      // Aggiorna LED con il meteo simulato e salta tutto il blocco WiFi
      SetRingColor(Currentmode, true);
      updateWeatherLEDs();
      lastScreenChange = millis();
      return; // Esce dal resto del setup() (salta la connessione WiFi)
    }
  }

  // ================
  // CONNESSIONE WIFI
  // ================

  bool wifiConnected = false;

  #if USE_MANUAL_WIFI
    Serial.println("Modalità: WIFI MANUALE");
    Serial.println("Configurazione da wifi_config.h");
    
    for(int attempt = 1; attempt <= WIFI_RETRY_COUNT && !wifiConnected; attempt++) {
      Serial.print("\nTentativo ");
      Serial.print(attempt);
      Serial.print(" di ");
      Serial.println(WIFI_RETRY_COUNT);
      
      wifiConnected = connectWiFiManual();
      
      if (!wifiConnected && attempt < WIFI_RETRY_COUNT) {
        Serial.println("Attesa 5 secondi prima del prossimo tentativo...");
        delay(5000);
      }
    }
    
    if (!wifiConnected) {
      Serial.println("\n⚠️ TUTTI I TENTATIVI MANUALI FALLITI!");
      Serial.println("Passaggio a WiFiManager...");
      
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.drawStr(DX(0),DY(8),"WIFI MANUALE");



      u8g2.sendBuffer();
      delay(3000);
      
      wm.setAPCallback(configModeCallback);
      wm.setSaveConfigCallback(saveConfigCallback);
      
      String savedApiKey = preferences.getString("api_key", "");
      if (savedApiKey.length() > 0) {
        custom_api_key.setValue(savedApiKey.c_str(), 60);
      }
      wm.addParameter(&custom_api_key);
      
      if (!wm.autoConnect("MeteoVision")) {
        Serial.println("Fallita connessione WiFiManager, riavvio...");
        delay(3000);
        ESP.restart();
      }
      wifiConnected = true;
    }
  #else
    Serial.println("Modalità: WIFIMANAGER");
    Serial.println("Portale di configurazione attivo");
    
    wm.setAPCallback(configModeCallback);
    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setConnectTimeout(30); // Timeout connessione 30 secondi

    preferences.begin("meteo-config", false);
    String savedApiKey = preferences.getString("api_key", "");
    preferences.end();
    if (savedApiKey.length() > 0) {
      Serial.println("API key trovata nelle preferences: " + savedApiKey);
      custom_api_key.setValue(savedApiKey.c_str(), 60);
    } else {
      Serial.println("Nessuna API key salvata trovata, campo vuoto.");
    }
    wm.addParameter(&custom_api_key);

    Serial.println("\nConnessione Wi-Fi...");
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(DX(0), DY(10), "     BY");
    u8g2.drawStr(DX(0), DY(18), " DAVIDE GATTI");
    u8g2.drawStr(DX(0), DY(28), "   SURVIVAL");
    u8g2.drawStr(DX(0), DY(37), "   HACKING");
    u8g2.sendBuffer();
    if (!wm.autoConnect("MeteoVision")) {
      Serial.println("Fallita connessione, riavvio...");
      delay(3000);
      ESP.restart();
    }
    wifiConnected = true;
  #endif

  if (wifiConnected) {
    Serial.println("\n===========================");
    Serial.println("WIFI CONNESSO CON SUCCESSO!");
    Serial.println("===========================");
    Serial.print("Indirizzo IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());
    Serial.println("========================================\n");


    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.drawStr(DX(0),DY(16),"WIFI CONNESSO");
    { char _ip[20]; snprintf(_ip,20,"%s",WiFi.localIP().toString().c_str()); u8g2.drawStr(DX(0),DY(24),_ip); }
    u8g2.drawStr(DX(0),DY(32),"ACCEDI AL WEB");
    u8g2.sendBuffer();
    delay(5000);
    u8g2.clearBuffer();

    Serial.print("Citta' configurata: ");
    Serial.println(savedCity);
    Serial.print("API Key usata: ");
    Serial.println(preferences.getString("api_key", "Errore"));
    Serial.print("Codice Paese (fisso IT): ");
    Serial.println(savedCountryCode);

    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    timeClient.begin();
    timeClient.update();

    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.begin();

    // Verifica se esiste una API key valida prima di fare il primo fetch
    preferences.begin("meteo-config", false);
    String testApiKey = preferences.getString("api_key", "");
    preferences.end();

    if (testApiKey.length() > 0) {
      Serial.println("API key trovata, recupero meteo...");
      fetchWeather();
      lastWeatherFetch = millis(); // Inizializza il timer dopo il primo fetch
    } else {
      Serial.println("⚠️ ATTENZIONE:Nessuna API KEY!");
      Serial.println("Configura l'API key tramite il portale web all'indirizzo:");
      Serial.println(WiFi.localIP());
      
      // Mostra messaggio sul display
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_5x7_tr);
          u8g2.drawStr(DX(0),DY(8),"ATTENZIONE!");
      u8g2.drawStr(DX(0),DY(18),"NESSUNA APIKEY");
      u8g2.drawStr(DX(0),DY(26),"VAI SU:");
      { char _ip[20]; snprintf(_ip,20,"%s",WiFi.localIP().toString().c_str()); u8g2.drawStr(DX(0),DY(34),_ip); }
      u8g2.sendBuffer();
      delay(4000);
      // Imposta valori di default per evitare errori
      weatherDescription = "API Key mancante";
      temperatureCelsius = 0.0;
      humidity = 0;
      windSpeed = 0.0;
      windDirection = 0;
      weatherConditionCode = 800;
    }

    SetRingColor(Currentmode, true); // MODIFICATO: Con animazione all'avvio
    lastScreenChange = millis();
  }
}

// --- Loop principale con gestione centralizzata dell'aggiornamento meteo ---
// Variabili per scroll testo display
int scrollCityX = 0;
int scrollDescX = 0;
int scrollDayX  = 0;
unsigned long lastScrollCity = 0;
unsigned long lastScrollDesc = 0;
unsigned long lastScrollDay  = 0;
const unsigned long SCROLL_INTERVAL = 20;
const int SCROLL_STEP = 1;

void loop() {

  // === MODALITÀ FIERA ===
  if (fieraMode) {
    // Ogni ora cambia città e genera nuovo meteo simulato
    if (millis() - fieraLastCityChange >= FIERA_CITY_INTERVAL) {
      fieraLastCityChange = millis();
      fieraCityIndex = (fieraCityIndex + 1) % FIERA_CITY_COUNT;
      strncpy(savedCity, fieraCities[fieraCityIndex], sizeof(savedCity) - 1);
      savedCity[sizeof(savedCity) - 1] = '\0';
      generateFieraWeather();

      // Aggiorna LED e display
      updateWeatherLEDs();
      if (Currentmode == MODO_AUTO) {
        neopixelModeChangeAnimation = true;
        neopixelAnimStep = 0;
        neopixelAnimLastUpdate = millis();
      }
      showText = true;
      DisplayOneTime = false;
      lastScreenChange = millis();
      Serial.print("[FIERA] Cambio ora - nuova città: ");
      Serial.println(savedCity);
    }

    // Gestione pulsante (cambio modalità e luminosità funzionano normalmente)
    handleButtonPress();

    unsigned long currentTime = millis();

    // Animazioni e display (stesso comportamento del loop normale)
    runNeopixelModeChangeAnimation();

    if (showModeName) {
      if (millis() - modeNameStartTime >= MODE_NAME_DISPLAY_DURATION) {
        showModeName = false;
        showText = true;
        DisplayOneTime = false;
      } else {
        return;
      }
    }

    if (keypressed == false) {
      if (Currentmode == MODO_ALL_OFF) {
        u8g2.clearBuffer(); u8g2.sendBuffer();
        AllLEDoff(); pixels.clear(); pixels.show();
      } else {
        if (showText) {
          if (currentTime - lastScreenChange >= SCREEN_TEXT_DURATION) {
            showText = false; lastScreenChange = currentTime;
            u8g2.clearBuffer(); currentFrame = 0;
          }
        } else {
          if (currentTime - lastScreenChange >= SCREEN_ANIMATION_DURATION) {
            showText = true; lastScreenChange = currentTime;
            u8g2.clearBuffer();
          }
        }
        if (!neopixelBootAnimationInProgress && !neopixelModeChangeAnimation) {
          if (Currentmode == MODO_AUTO) {
            runNeopixelAnimation();
          }
        }
        if (showText) {
          if (!DisplayOneTime) { displayWeatherInfo(); DisplayOneTime = true; }
        } else {
          displayWeatherAnimation(); DisplayOneTime = false;
        }
      }
    }
    return; // Non eseguire il resto del loop normale
  }
  // === FINE MODALITÀ FIERA ===

  #if !USE_MANUAL_WIFI
    wm.process();
  #endif

  server.handleClient();
  handleButtonPress();

  unsigned long currentTime = millis();

  // --- GESTIONE CENTRALIZZATA DELL'AGGIORNAMENTO METEO ---
  // SPOSTATO QUI IN CIMA: viene eseguito SEMPRE, anche durante regolazione luminosità o cambio modalità
  // Controlla se è necessario un aggiornamento del meteo
  if (!showModeName && (forceWeatherUpdate || (currentTime - lastWeatherFetch >= WEATHER_FETCH_INTERVAL))) {
    if (forceWeatherUpdate) {
      Serial.println("Aggiornamento forzato del meteo richiesto");
    } else {
      Serial.println("Aggiornamento periodico del meteo (10 minuti)");
    }

    fetchWeather();
    lastWeatherFetch = currentTime;
    forceWeatherUpdate = false;

    // Dopo l'aggiornamento, mostra le informazioni testuali
    lastScreenChange = currentTime;
    showText = true;
    DisplayOneTime = false;

    // Se siamo in modalità AUTO, aggiorna anche i LED
    if (Currentmode == MODO_AUTO) {
      updateWeatherLEDs();
      // Esegui l'animazione a onda fluida con il colore del meteo
      neopixelModeChangeAnimation = true;
      neopixelAnimStep = 0;
      neopixelAnimLastUpdate = millis();
    }
  }

  // Se siamo in modalità regolazione luminosità, non eseguire il resto del loop normale
  if (isLongPressMode) {
    return;
  }

  checkNightMode();

  // Gestione dell'animazione cambio modalità NeoPixel
  runNeopixelModeChangeAnimation();

  // Gestione della visualizzazione del nome della modalità
  if (showModeName) {
    if (millis() - modeNameStartTime >= MODE_NAME_DISPLAY_DURATION) {
      showModeName = false;
      showText = true;
      DisplayOneTime = false;
      // Se modalità AUTO, aggiorna subito anello con colore meteo
      if (Currentmode == MODO_AUTO) {
        updateWeatherLEDs();
        neopixelModeChangeAnimation = true;
        neopixelAnimStep = 0;
        neopixelAnimLastUpdate = millis();
      }
    } else {
      return;
    }
  }

  if (keypressed == false) {
    // GESTIONE MODALITÀ CON COMPORTAMENTI DIVERSI
    if (Currentmode == MODO_NOTTE) {
      // Modalità notte: solo display attivo, tutti i LED spenti
      if (showText) {
        if (currentTime - lastScreenChange >= SCREEN_TEXT_DURATION) {
          showText = false;
          lastScreenChange = currentTime;
          u8g2.clearBuffer();
          currentFrame = 0;
        }
        // Se il testo scrolla, chiama displayWeatherInfo ad ogni frame
        { String _d = getWeatherDescriptionItalian(weatherConditionCode); _d.toUpperCase();
          String _c = savedCity; _c.toUpperCase();
          bool needsScroll = (getTextWidth(_d,1) > DISP_W || getTextWidth(_c,1) > DISP_W);
          if (!DisplayOneTime || (needsScroll && millis() - lastScrollDesc >= SCROLL_INTERVAL)) {
            displayWeatherInfo();
            DisplayOneTime = true;
          }
        }
      } else {
        if (currentTime - lastScreenChange >= SCREEN_ANIMATION_DURATION) {
          showText = true;
          lastScreenChange = currentTime;
          u8g2.clearBuffer();
        }
        displayWeatherAnimation();
        DisplayOneTime = false;
      }
    }
    else if (Currentmode == MODO_ALL_OFF) {
      // Modalità tutto spento: display spento, tutti i LED spenti
      u8g2.clearBuffer();
      u8g2.sendBuffer();
      AllLEDoff();
      pixels.clear();
      pixels.show();
    }
    else if (Currentmode == MODO_LED_OFF) {
      // Modalità LED spenti: solo display attivo, LED individuali spenti, anello spento
      if (showText) {
        if (currentTime - lastScreenChange >= SCREEN_TEXT_DURATION) {
          showText = false;
          lastScreenChange = currentTime;
          u8g2.clearBuffer();
          currentFrame = 0;
        }
        // Se il testo scrolla, chiama displayWeatherInfo ad ogni frame
        { String _d = getWeatherDescriptionItalian(weatherConditionCode); _d.toUpperCase();
          String _c = savedCity; _c.toUpperCase();
          bool needsScroll = (getTextWidth(_d,1) > DISP_W || getTextWidth(_c,1) > DISP_W);
          if (!DisplayOneTime || (needsScroll && millis() - lastScrollDesc >= SCROLL_INTERVAL)) {
            displayWeatherInfo();
            DisplayOneTime = true;
          }
        }
      } else {
        if (currentTime - lastScreenChange >= SCREEN_ANIMATION_DURATION) {
          showText = true;
          lastScreenChange = currentTime;
          u8g2.clearBuffer();
        }
        displayWeatherAnimation();
        DisplayOneTime = false;
      }
      
      // Assicura che i LED siano spenti
      AllLEDoff();
      pixels.clear();
      pixels.show();
    }
    else if (Currentmode == MODO_RING_OFF) {
      // Modalità anello spento: display attivo, LED individuali attivi, anello spento
      if (showText) {
        if (currentTime - lastScreenChange >= SCREEN_TEXT_DURATION) {
          showText = false;
          lastScreenChange = currentTime;
          u8g2.clearBuffer();
          currentFrame = 0;
        }
      } else {
        if (currentTime - lastScreenChange >= SCREEN_ANIMATION_DURATION) {
          showText = true;
          lastScreenChange = currentTime;
          u8g2.clearBuffer();
        }
      }

      if (showText) {
        // Se il testo scrolla, chiama displayWeatherInfo ad ogni frame
        { String _d = getWeatherDescriptionItalian(weatherConditionCode); _d.toUpperCase();
          String _c = savedCity; _c.toUpperCase();
          bool needsScroll = (getTextWidth(_d,1) > DISP_W || getTextWidth(_c,1) > DISP_W);
          if (!DisplayOneTime || (needsScroll && millis() - lastScrollDesc >= SCROLL_INTERVAL)) {
            displayWeatherInfo();
            DisplayOneTime = true;
          }
        }
      } else {
        displayWeatherAnimation();
        DisplayOneTime = false;
      }
      
      // Anello spento ma LED individuali attivi
      pixels.clear();
      pixels.show();
    }
    else {
      // Altre modalità (AUTO, COLORI, ecc.) - comportamento normale
      if (showText) {
        if (currentTime - lastScreenChange >= SCREEN_TEXT_DURATION) {
          showText = false;
          lastScreenChange = currentTime;
          u8g2.clearBuffer();
          currentFrame = 0;
        }
      } else {
        if (currentTime - lastScreenChange >= SCREEN_ANIMATION_DURATION) {
          showText = true;
          lastScreenChange = currentTime;
          u8g2.clearBuffer();
        }
      }

      // Gestione NeoPixel per modalità AUTO
      if (!neopixelBootAnimationInProgress && !neopixelModeChangeAnimation) {
        if (Currentmode == MODO_AUTO) {
          runNeopixelAnimation();
        }
      }

      if (showText) {
        // Se il testo scrolla, chiama displayWeatherInfo ad ogni frame
        { String _d = getWeatherDescriptionItalian(weatherConditionCode); _d.toUpperCase();
          String _c = savedCity; _c.toUpperCase();
          bool needsScroll = (getTextWidth(_d,1) > DISP_W || getTextWidth(_c,1) > DISP_W);
          if (!DisplayOneTime || (needsScroll && millis() - lastScrollDesc >= SCROLL_INTERVAL)) {
            displayWeatherInfo();
            DisplayOneTime = true;
          }
        }
      } else {
        displayWeatherAnimation();
        DisplayOneTime = false;
      }
    }
  }
}

// --- FUNZIONE PULSANTE CORRETTA ---
void handleButtonPress() {
  bool reading = digitalRead(PIN_PULSANTE);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;

      if (buttonState == LOW) {
        keypressed = true;
        buttonPressStartTime = millis();
        isLongPressMode = false;
        Serial.println("Pulsante premuto (inizia timer).");
      } else {
        unsigned long pressDuration = millis() - buttonPressStartTime;
        Serial.printf("Pulsante rilasciato. Durata: %lu ms.\n", pressDuration);

        if (!isLongPressMode && pressDuration < LONG_PRESS_THRESHOLD) {
          Serial.println("Pressione breve rilevata. Cambio modalita'.");
          Currentmode++;
          if (Currentmode > MAX_MODE) Currentmode = MODO_AUTO;
          showText = true;
          DisplayOneTime = false;

          // Mostra il nome della modalità per 2 secondi
          showModeName = true;
          modeNameStartTime = millis();

          SetRingColor(Currentmode, neopixelAnimationEnabled); // Con animazione se abilitata
          saveConfig();
        }
        else if (isLongPressMode) {
          Serial.println("Uscita dalla regolazione luminosita'.");
          isLongPressMode = false;
          saveConfig();
          showText = true;
          DisplayOneTime = false;
        }
        
        keypressed = false;
      }
    }
  }

  lastButtonState = reading;

  if (buttonState == LOW && !isLongPressMode) {
    if (millis() - buttonPressStartTime >= LONG_PRESS_THRESHOLD) {
      isLongPressMode = true;
      Serial.println("Entrato in modalita' regolazione luminosita'.");
      lastBrightnessChangeTime = millis();
      drawBrightnessBar(savedNeopixelBrightness);
    }
  }

  if (isLongPressMode && buttonState == LOW) {
    if (millis() - lastBrightnessChangeTime >= BRIGHTNESS_CHANGE_INTERVAL) {
      lastBrightnessChangeTime = millis();

      savedNeopixelBrightness += 5;
      if (savedNeopixelBrightness > 255) {
        savedNeopixelBrightness = 10;
      }
      
      pixels.setBrightness(savedNeopixelBrightness);
      pixels.show();
      
      drawBrightnessBar(savedNeopixelBrightness);
    }
  }
}

// --- FUNZIONE PER DISEGNARE LA BARRA DI LUMINOSITÀ ---
void drawBrightnessBar(uint8_t brightness) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);
  
  u8g2.drawStr(DX(25),DY(10),"LUMINOSITA'");
  
  int barWidth = 100;
  int barHeight = 8;
  int barX = (DISP_W - barWidth) / 2;
  int barY = 15;
  
  u8g2.drawFrame(DX(barX),DY(barY),barWidth,barHeight);
  
  int fillWidth = map(brightness, 0, 255, 0, barWidth - 2);
  u8g2.drawBox(DX(barX + 1),DY(barY + 1),fillWidth,barHeight - 2);
  
  u8g2.drawStr(DX(40),DY(33),"VALORE: ");


  u8g2.sendBuffer();
}

void runWindDirectionScan(int windLedIndex) {
  static unsigned long lastScanUpdate = 0;
  static int lastFrameLogged = -1;
  const unsigned long scanStepDuration = WIND_SCAN_DURATION / NUM_NEOPIXELS;

  if (millis() - lastScanUpdate >= scanStepDuration) {
    lastScanUpdate = millis();

    pixels.clear();
    pixels.setPixelColor(windScanFrame, pixels.Color(0, 255, 0));
    pixels.show();

    if (windScanFrame != lastFrameLogged) {
      Serial.print("WindScanFrame: ");
      Serial.println(windScanFrame);
      lastFrameLogged = windScanFrame;
    }

    windScanFrame = (windScanFrame + 1) % NUM_NEOPIXELS;

    if (windScanFrame == windLedIndex) {
      windScanning = false;
      windAnimationActive = true;
      animationFrame = 0;
      windAnimationStartTime = millis();
      Serial.println("Scan completata, inizio animazione vento");
    }
  }
}

// --- Funzioni di gestione della configurazione ---
void saveConfig() {
  preferences.begin("meteo-config", false);
  preferences.putString("city", savedCity);
  preferences.putInt("brightness", savedNeopixelBrightness);
  preferences.putInt("Currentmode", Currentmode);
  // NON salviamo più forecastDay per forzare sempre "Attuale" all'accensione
  preferences.putInt("forecastDay", forecastDay);
  preferences.putInt("forecastPeriod", forecastPeriod);
  preferences.putBool("nightModeEnabled", nightModeEnabled);
  preferences.putInt("nightStartHour", nightStartHour);
  preferences.putInt("nightStartMinute", nightStartMinute);
  preferences.putInt("nightEndHour", nightEndHour);
  preferences.putInt("nightEndMinute", nightEndMinute);
  preferences.putBool("windAnimEnabled", windAnimationEnabled);
  preferences.putBool("neopixelAnimEnabled", neopixelAnimationEnabled); // Nuova variabile
  preferences.end();

  Serial.println("Configurazione salvata.");
}

void loadConfig() {
  Serial.println("\n--- Caricamento configurazione salvata ---");
  preferences.begin("meteo-config", false);

  preferences.getString("city", savedCity, sizeof(savedCity));
  Serial.println("Città: " + String(savedCity));

  savedNeopixelBrightness = preferences.getInt("brightness", 200);
  Serial.println("Luminosità NeoPixel: " + String(savedNeopixelBrightness));

  Currentmode = preferences.getInt("Currentmode", 0);
  Serial.println("Modalità corrente: " + String(Currentmode));

  // FORZA SEMPRE PREVISIONI ATTUALI ALL'ACCENSIONE
  forecastDay = -1; // Sempre Attuale all'accensione
  
  forecastDay = preferences.getInt("forecastDay", -1);
  forecastPeriod = preferences.getInt("forecastPeriod", 0);
  // Limita il valore a 0-2 (mattino, pomeriggio, sera)
  if (forecastPeriod < 0 || forecastPeriod > 2) forecastPeriod = 0;
  
  nightModeEnabled = preferences.getBool("nightModeEnabled", false);
  nightStartHour = preferences.getInt("nightStartHour", 22);
  nightStartMinute = preferences.getInt("nightStartMinute", 0);
  nightEndHour = preferences.getInt("nightEndHour", 7);
  nightEndMinute = preferences.getInt("nightEndMinute", 0);
  windAnimationEnabled = preferences.getBool("windAnimEnabled", true);
  neopixelAnimationEnabled = preferences.getBool("neopixelAnimEnabled", true); // Nuova variabile

  Serial.println("Modalità notte:");
  Serial.println(" - Abilitata: " + String(nightModeEnabled));
  Serial.println(" - Orario: " + String(nightStartHour) + ":" + String(nightStartMinute) + " -> " + String(nightEndHour) + ":" + String(nightEndMinute));
  Serial.println("Giorno previsione: ATTUALE (sempre forzato all'accensione)");
  Serial.println("Periodo previsione: " + String(forecastPeriod) + " (0:Mattino, 1:Pomeriggio, 2:Sera)");
  Serial.println("Animazione vento: " + String(windAnimationEnabled ? "Abilitata" : "Disabilitata"));
  Serial.println("Animazione cambio modalità: " + String(neopixelAnimationEnabled ? "Abilitata" : "Disabilitata")); // Nuova variabile

  String storedApiKey = preferences.getString("api_key", "");
  if (storedApiKey.length() > 0) {
    Serial.println("API key caricata: " + storedApiKey);
  } else {
    Serial.println("⚠️  Nessuna API key salvata trovata.");
  }
  
  preferences.end();

  Serial.println("--- Fine caricamento configurazione ---\n");
}

void saveConfigCallback() {
  Serial.println("📡 Salvataggio configurazione WiFiManager...");

  String newApiKey = custom_api_key.getValue();
  newApiKey.trim();

  if (newApiKey.length() > 0) {
    preferences.begin("meteo-config", false);
    String oldApiKey = preferences.getString("api_key", "");
    if (newApiKey != oldApiKey) {
      preferences.putString("api_key", newApiKey);
      Serial.println("✅ API key aggiornata e salvata: " + newApiKey);
    } else {
      Serial.println("ℹ️ API key invariata, nessun salvataggio necessario.");
    }
    preferences.end();
  } else {
    Serial.println("⚠️ Nessuna API key inserita — nessun salvataggio.");
  }
}

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Entrato in modalita' configurazione AP");
  Serial.println(myWiFiManager->getConfigPortalSSID());

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(DX(0),DY(16),"CONFIGURA WIFI:");
  u8g2.sendBuffer();
}

// --- FUNZIONI METEO CORRETTE ---
void fetchWeather() {
  if (forecastDay == -1) {
    // Usa API meteo corrente
    fetchCurrentWeather();
  } else {
    // Usa API previsioni
    fetchForecastWeather();
  }
  
  DisplayOneTime = false;
}

void fetchCurrentWeather() {
  Serial.println("=== Fetching current weather (OpenWeather Current API) ===");
  
  HTTPClient http;

  preferences.begin("meteo-config", false);
  String currentApiKey = preferences.getString("api_key", OPENWEATHER_API_KEY);
  preferences.end();

  String apiCityQuery = savedCity;
  if (apiCityQuery.equalsIgnoreCase("Roma")) {
    apiCityQuery = "Rome";
  }

  apiCityQuery = urlEncode(apiCityQuery);

  String weatherUrl = "http://api.openweathermap.org/data/2.5/weather?q="
                      + apiCityQuery + "," + String(savedCountryCode)
                      + "&units=metric&appid=" + currentApiKey + "&lang=it";

  Serial.print("Current Weather URL: ");
  Serial.println(weatherUrl);

  http.begin(weatherUrl);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Current Weather API Response received");

    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      weatherDescription = "Errore JSON OWM.";
      return;
    }

    // Estrai dati meteo corrente
    weatherDescription = doc["weather"][0]["description"].as<String>();
    weatherConditionCode = doc["weather"][0]["id"].as<int>();
    temperatureCelsius = doc["main"]["temp"].as<float>();
    humidity = doc["main"]["humidity"].as<int>();
    windSpeed = doc["wind"]["speed"].as<float>() * 3.6; // Converti m/s in km/h
    windDirection = doc["wind"]["deg"].as<int>();
    sunriseTime = doc["sys"]["sunrise"].as<long>();
    sunsetTime = doc["sys"]["sunset"].as<long>();

    // Aggiorna i LED solo se non siamo in modalità che li richiedono spenti
    if (Currentmode != MODO_LED_OFF && Currentmode != MODO_ALL_OFF && Currentmode != MODO_NOTTE) {
      switch (Currentmode) {
        case MODO_AUTO:
          updateWeatherLEDs();
          // Esegui l'animazione a onda fluida con il colore del meteo
          neopixelModeChangeAnimation = true;
          neopixelAnimStep = 0;
          neopixelAnimLastUpdate = millis();
          break;
        case MODO_ROSSO:
        case MODO_VERDE:
        case MODO_BLU:
        case MODO_VIOLA:
        case MODO_CIANO:
        case MODO_GIALLO:
        case MODO_BIANCO:
        case MODO_RING_OFF:
          updateWeatherLEDs();
          break;
        default:
          break;
      }
    }

    Serial.println("--- Risultato Meteo Attuale ---");
    Serial.print("Descrizione Meteo: ");
    Serial.println(weatherDescription);
    Serial.print("Codice Condizione: ");
    Serial.println(weatherConditionCode);
    Serial.print("Temperatura: ");
    Serial.print(temperatureCelsius);
    Serial.println(" °C");
    Serial.print("Umidità: ");
    Serial.print(humidity);
    Serial.println(" %");
    Serial.print("Velocità vento: ");
    Serial.println(windSpeed);
    Serial.print("Direzione vento: ");
    Serial.println(windDirection);
    Serial.println("-----------------------------");

  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    weatherDescription = "Errore HTTP OWM.";

    if (httpCode == 401) {
      Serial.println("API Key non valida o non attiva.");
      weatherDescription = "API Key Errata!";
    } else if (httpCode == 404) {
      Serial.println("Citta' o paese non trovato.");
      weatherDescription = "Citta' non trovata!";
    }
  }

  http.end();
}

void fetchForecastWeather() {
  Serial.println("=== Fetching weather (OpenWeather Forecast API) ===");
  Serial.print("Giorno previsione richiesto: ");
  switch(forecastDay) {
    case 0: Serial.println("OGGI"); break;
    case 1: Serial.println("DOMANI"); break;
    case 2: Serial.println("DOPODOMANI"); break;
    case 3: Serial.println("GIORNO 4"); break;
    case 4: Serial.println("GIORNO 5"); break;
  }
  
  Serial.print("Periodo previsione richiesto: ");
  switch(forecastPeriod) {
    case 0: Serial.println("MATTINO"); break;
    case 1: Serial.println("POMERIGGIO"); break;
    case 2: Serial.println("SERA"); break;
  }
  
  HTTPClient http;

  preferences.begin("meteo-config", false);
  String currentApiKey = preferences.getString("api_key", OPENWEATHER_API_KEY);
  preferences.end();

  String apiCityQuery = savedCity;
  if (apiCityQuery.equalsIgnoreCase("Roma")) {
    apiCityQuery = "Rome";
  }

  apiCityQuery = urlEncode(apiCityQuery);

  String weatherUrl = "http://api.openweathermap.org/data/2.5/forecast?q="
                      + apiCityQuery + "," + String(savedCountryCode)
                      + "&units=metric&appid=" + currentApiKey + "&lang=it&cnt=40";

  Serial.print("Forecast URL: ");
  Serial.println(weatherUrl);

  http.begin(weatherUrl);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Forecast API Response received");

    DynamicJsonDocument doc(40 * 1024);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      weatherDescription = "Errore JSON OWM.";
      return;
    }

    // ANALISI DI TUTTI I PERIODI DEL RANGE SELEZIONATO
    int startIndex, endIndex;
    
    // Definisce i range per ogni periodo
    switch(forecastPeriod) {
      case 0: // Mattino: periodi 2 e 3 (06-12)
        startIndex = 2 + (forecastDay * 8);
        endIndex = 3 + (forecastDay * 8);
        break;
      case 1: // Pomeriggio: periodi 4 e 5 (12-18)
        startIndex = 4 + (forecastDay * 8);
        endIndex = 5 + (forecastDay * 8);
        break;
      case 2: // Sera: periodi 6 e 7 (18-00)
        startIndex = 6 + (forecastDay * 8);
        endIndex = 7 + (forecastDay * 8);
        break;
    }
    
    // Limita gli indici ai dati disponibili
    if (startIndex >= doc["list"].size()) startIndex = doc["list"].size() - 1;
    if (endIndex >= doc["list"].size()) endIndex = doc["list"].size() - 1;
    
    // Analizza tutti i periodi del range per trovare la condizione predominante
    int conditionCounts[1000] = {0}; // Array per contare le occorrenze di ogni codice meteo
    float totalTemp = 0;
    float totalHumidity = 0;
    float totalWindSpeed = 0;
    int totalWindDirection = 0;
    int periodCount = 0;
    
    for (int i = startIndex; i <= endIndex && i < doc["list"].size(); i++) {
      JsonObject forecast = doc["list"][i];
      int conditionCode = forecast["weather"][0]["id"].as<int>();
      conditionCounts[conditionCode]++;
      
      totalTemp += forecast["main"]["temp"].as<float>();
      totalHumidity += forecast["main"]["humidity"].as<int>();
      totalWindSpeed += forecast["wind"]["speed"].as<float>();
      totalWindDirection += forecast["wind"]["deg"].as<int>();
      periodCount++;
      
      Serial.print("Periodo ");
      Serial.print(i);
      Serial.print(": Condizione ");
      Serial.print(conditionCode);
      Serial.print(" - ");
      Serial.println(forecast["weather"][0]["description"].as<String>());
    }
    
    if (periodCount == 0) {
      // Fallback: usa l'ultimo periodo disponibile
      int index = startIndex;
      if (index >= doc["list"].size()) index = doc["list"].size() - 1;
      JsonObject forecast = doc["list"][index];
      
      weatherDescription = forecast["weather"][0]["description"].as<String>();
      weatherConditionCode = forecast["weather"][0]["id"].as<int>();
      temperatureCelsius = forecast["main"]["temp"].as<float>();
      humidity = forecast["main"]["humidity"].as<int>();
      windSpeed = forecast["wind"]["speed"].as<float>() * 3.6;
      windDirection = forecast["wind"]["deg"].as<int>();
      
      Serial.println("⚠️  Usando fallback - dati insufficienti");
    } else {
      // Trova il codice di condizione più frequente
      int maxCount = 0;
      int predominantCondition = 800; // Default: cielo sereno
      
      for (int i = 0; i < 1000; i++) {
        if (conditionCounts[i] > maxCount) {
          maxCount = conditionCounts[i];
          predominantCondition = i;
        }
      }
      
      // In caso di parità, preferisci condizioni peggiori (pioggia/temporale > nuvole > sole)
      if (maxCount > 0) {
        // Se c'è almeno una condizione di pioggia/temporale, preferiscila
        for (int i = 200; i < 700; i++) {
          if (conditionCounts[i] == maxCount) {
            predominantCondition = i;
            break;
          }
        }
      }
      
      weatherConditionCode = predominantCondition;
      weatherDescription = getWeatherDescriptionItalian(predominantCondition);
      temperatureCelsius = totalTemp / periodCount;
      humidity = totalHumidity / periodCount;
      windSpeed = (totalWindSpeed / periodCount) * 3.6;
      windDirection = totalWindDirection / periodCount;
    }

    // Per alba e tramonto, usa i dati della città (primo elemento)
    sunriseTime = doc["city"]["sunrise"].as<long>();
    sunsetTime = doc["city"]["sunset"].as<long>();

    // Aggiorna i LED solo se non siamo in modalità che li richiedono spenti
    if (Currentmode != MODO_LED_OFF && Currentmode != MODO_ALL_OFF && Currentmode != MODO_NOTTE) {
      switch (Currentmode) {
        case MODO_AUTO:
          updateWeatherLEDs();
          // Esegui l'animazione a onda fluida con il colore del meteo
          neopixelModeChangeAnimation = true;
          neopixelAnimStep = 0;
          neopixelAnimLastUpdate = millis();
          break;
        case MODO_ROSSO:
        case MODO_VERDE:
        case MODO_BLU:
        case MODO_VIOLA:
        case MODO_CIANO:
        case MODO_GIALLO:
        case MODO_BIANCO:
        case MODO_RING_OFF:
          updateWeatherLEDs();
          break;
        default:
          break;
      }
    }

    Serial.println("--- Risultato Previsione ---");
    Serial.print("ForecastDay: ");
    Serial.println(forecastDay);
    Serial.print("ForecastPeriod: ");
    Serial.println(forecastPeriod);
    Serial.print("Range analizzato: ");
    Serial.print(startIndex);
    Serial.print(" - ");
    Serial.println(endIndex);
    Serial.print("Periodi considerati: ");
    Serial.println(periodCount);
    Serial.print("Condizione predominante: ");
    Serial.println(weatherConditionCode);
    Serial.print("Descrizione Meteo: ");
    Serial.println(weatherDescription);
    Serial.print("Temperatura media: ");
    Serial.print(temperatureCelsius);
    Serial.println(" °C");
    Serial.print("Umidità media: ");
    Serial.print(humidity);
    Serial.println(" %");
    Serial.print("Velocità vento media: ");
    Serial.println(windSpeed);
    Serial.print("Direzione vento media: ");
    Serial.println(windDirection);
    Serial.println("-----------------------------");

  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    weatherDescription = "Errore HTTP OWM.";

    if (httpCode == 401) {
      Serial.println("API Key non valida o non attiva.");
      weatherDescription = "API Key Errata!";
    } else if (httpCode == 404) {
      Serial.println("Citta' o paese non trovato.");
      weatherDescription = "Citta' non trovata!";
    }
  }

  http.end();
}


// --- FUNZIONE displayWeatherInfo ---
void displayWeatherInfo() {
  u8g2.clearBuffer();

  // MODIFICATO: Layout compatto per display 32px
  
  // Linea 1: Città MAIUSCOLO con scroll
  u8g2.setFont(u8g2_font_5x7_tr);
  String cityDisplay = savedCity;
  cityDisplay.toUpperCase();
  int cityWidth = getTextWidth(cityDisplay, 1);
  int cityX;
  if (cityWidth <= DISP_W) {
    cityX = (DISP_W - cityWidth) / 2;
    scrollCityX = 0;
  } else {
    // Scroll automatico
    if (millis() - lastScrollCity >= SCROLL_INTERVAL) {
      lastScrollCity = millis();
      scrollCityX -= SCROLL_STEP;
      if (scrollCityX <= -(cityWidth + 8)) scrollCityX = 0;
    }
    cityX = scrollCityX;
  }
  u8g2.setClipWindow(DX(0), DY(0), DX(DISP_W-1), DY(39));
  { char _b[64]; strncpy(_b, cityDisplay.c_str(), 63); _b[63]=0;
    u8g2.drawStr(DX(cityX), DY(8), _b);
    // Secondo testo per scroll circolare continuo
    if (cityWidth > DISP_W) u8g2.drawStr(DX(cityX + cityWidth + 8), DY(8), _b);
  }
  u8g2.setMaxClipWindow();

  // Linea 2: Descrizione meteo MAIUSCOLO con scroll
  String desc = getWeatherDescriptionItalian(weatherConditionCode);
  desc.toUpperCase();
  int descWidth = getTextWidth(desc, 1);
  int descX;
  if (descWidth <= DISP_W) {
    descX = (DISP_W - descWidth) / 2;
    scrollDescX = 0;
  } else {
    if (millis() - lastScrollDesc >= SCROLL_INTERVAL) {
      lastScrollDesc = millis();
      scrollDescX -= SCROLL_STEP;
      if (scrollDescX <= -(descWidth + 8)) scrollDescX = 0;
    }
    descX = scrollDescX;
  }
  u8g2.setClipWindow(DX(0), DY(9), DX(DISP_W-1), DY(39));
  { char _b[64]; strncpy(_b, desc.c_str(), 63); _b[63]=0;
    u8g2.drawStr(DX(descX), DY(16), _b);
    // Secondo testo per scroll circolare continuo
    if (descWidth > DISP_W) u8g2.drawStr(DX(descX + descWidth + 8), DY(16), _b);
  }
  u8g2.setMaxClipWindow();

  // Linea 3: Temperatura e Umidità (centrata)
  String tempHumStr = "T:" + String(temperatureCelsius, 1) + "C H:" + String(humidity) + "%";
  int tempHumWidth = getTextWidth(tempHumStr, 1);
  int tempHumX = (DISP_W - tempHumWidth) / 2;
  { char _b[32]; snprintf(_b,32,"%s",String(tempHumStr).c_str()); u8g2.drawStr(DX(tempHumX),DY(24),_b); }

  // Linea 4: Ora grande centrata
  time_t currentTime = getCurrentEpoch();
  struct tm *timeinfo = localtime(&currentTime);
  char timeBuffer[6];
  strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", timeinfo);
  u8g2.setFont(u8g2_font_7x13B_tr);
  int timeWidth = strlen(timeBuffer) * 7;
  int timeX = (DISP_W - timeWidth) / 2;
  u8g2.drawStr(DX(timeX), DY(37), timeBuffer);
  u8g2.setFont(u8g2_font_5x7_tr);

  u8g2.sendBuffer();

  // Avvia animazione vento solo se siamo in modalità AUTO e se abilitata
  if (Currentmode == MODO_AUTO && windAnimationEnabled) {
    Serial.println("Mostro INFO METEO - Avvio animazione vento");
    int windLedIndex = 0;
    if (NUM_NEOPIXELS > 0) {
      windLedIndex = (360 - windDirection + 90) % 360;
      windLedIndex = map(windLedIndex, 0, 360, 0, NUM_NEOPIXELS);
    }

    windScanFrame = 0;
    windScanning = true;
    windAnimationActive = false;
  }
}

// --- FUNZIONE displayWeatherAnimation CORRETTA per display 32px con scritta giorno ---
void displayWeatherAnimation() {
  u8g2.clearBuffer();

  // CALCOLO CORRETTO DI isDaytime per TUTTI i giorni di previsione
  // CALCOLO CORRETTO DI isDaytime
  bool isDaytime;
  if (forecastDay == -1) {
    // Per ATTUALE: usa ora corrente e dati alba/tramonto reali
        time_t currentTime = getCurrentEpoch();
    isDaytime = (currentTime >= sunriseTime && currentTime < sunsetTime);
  } else {
    // Per TUTTI i giorni di previsione (OGGI, DOMANI, DOPODOMANI, GIORNO 4, GIORNO 5):
    // usa il PERIODO selezionato per determinare giorno/notte
    switch(forecastPeriod) {
      case 0: // Mattino (06-12) - sempre giorno
      case 1: // Pomeriggio (12-18) - sempre giorno
        isDaytime = true;
        break;
      case 2: // Sera (18-00) - sempre notte
        isDaytime = false;
        break;
    }
  }

  // MOSTRA SCRITTA GIORNO/PERIODO IN ALTO
  u8g2.setFont(u8g2_font_5x7_tr);
  String dayString = "";
  
  if (forecastDay == -1) {
    dayString = "ATTUALE";
  } else {
    switch(forecastDay) {
      case 0: dayString = "OGGI"; break;
      case 1: dayString = "DOMANI"; break;
      case 2: dayString = "DOPODOMANI"; break;
      case 3: dayString = "GIORNO 4"; break;
      case 4: dayString = "GIORNO 5"; break;
    }
    
    switch(forecastPeriod) {
      case 0: dayString += " MATTINO"; break;
      case 1: dayString += " POMERIGGIO"; break;
      case 2: dayString += " SERA"; break;
    }
  }
  
  // Scritta giorno con scroll se non ci sta
  int textWidth = dayString.length() * 6;
  int textX;
  if (textWidth <= DISP_W) {
    textX = (DISP_W - textWidth) / 2;
    scrollDayX = 0;
  } else {
    if (millis() - lastScrollDay >= SCROLL_INTERVAL) {
      lastScrollDay = millis();
      scrollDayX -= SCROLL_STEP;
      if (scrollDayX <= -(textWidth + 8)) scrollDayX = 0;
    }
    textX = scrollDayX;
  }
  u8g2.setClipWindow(DX(0), DY(0), DX(DISP_W-1), DY(39));
  { char _b[64]; strncpy(_b, dayString.c_str(), 63); _b[63]=0;
    u8g2.drawStr(DX(textX), DY(8), _b);
    // Secondo testo per scroll circolare continuo
    if (textWidth > DISP_W) u8g2.drawStr(DX(textX + textWidth + 8), DY(8), _b);
  }
  u8g2.setMaxClipWindow();

  const unsigned char **currentAnimation = nullptr;
  const unsigned char **currentAnimation1 = nullptr;
  int currentAnimFrames = 0;
  int currentAnimWidth = 0;
  int currentAnimHeight = 0;

  if (weatherConditionCode >= 200 && weatherConditionCode < 300) {
    currentAnimFrames = STORM_ANIM_FRAMES;
    currentAnimWidth = STORM_ANIM_WIDTH;
    currentAnimHeight = STORM_ANIM_HEIGHT;
    currentAnimation = storm_animation;
    currentAnimation1 = rain_animation;
    dualAnimation = true;

  } else if ((weatherConditionCode >= 300 && weatherConditionCode < 400) || (weatherConditionCode >= 500 && weatherConditionCode < 600) || (weatherConditionCode >= 600 && weatherConditionCode < 700)) {
    currentAnimation = rain_animation;
    currentAnimFrames = RAIN_ANIM_FRAMES;
    currentAnimWidth = RAIN_ANIM_WIDTH;
    currentAnimHeight = RAIN_ANIM_HEIGHT;
    dualAnimation = false;

  } else if (weatherConditionCode == 800) {
    if (isDaytime) {
      currentAnimation = sun_animation;
      currentAnimFrames = SUN_ANIM_FRAMES;
      currentAnimWidth = SUN_ANIM_WIDTH;
      currentAnimHeight = SUN_ANIM_HEIGHT;
    } else {
      currentAnimation = moon_animation;
      currentAnimFrames = MOON_ANIM_FRAMES;
      currentAnimWidth = MOON_ANIM_WIDTH;
      currentAnimHeight = MOON_ANIM_HEIGHT;
    }
    dualAnimation = false;

  } else if ((weatherConditionCode >= 801 && weatherConditionCode <= 804) || (weatherConditionCode >= 700 && weatherConditionCode < 799)) {
    if (isDaytime) {
      currentAnimation = sun_animation;
      currentAnimFrames = SUN_ANIM_FRAMES;
      currentAnimWidth = SUN_ANIM_WIDTH;
      currentAnimHeight = SUN_ANIM_HEIGHT;
    } else {
      currentAnimation = moon_animation;
      currentAnimFrames = MOON_ANIM_FRAMES;
      currentAnimWidth = MOON_ANIM_WIDTH;
      currentAnimHeight = MOON_ANIM_HEIGHT;
    }
    currentAnimation1 = cloud_animation;
    dualAnimation = true;

  } else {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(DX(10),DY(20),"NO ANIMAZIONE");
    { char _b[32]; snprintf(_b,32,"%s",String(weatherDescription).c_str()); u8g2.drawStr(DX(20),DY(28),_b); }
  }

  if (currentAnimation != nullptr) {
    if (millis() - lastFrameChange >= ANIM_FRAME_DELAY) {
      currentFrame = (currentFrame + 1) % currentAnimFrames;
      lastFrameChange = millis();
    }

    // Posizionamento animazioni per display 32px con scritta in alto
    // Icone 24x24 pixel, posizionate a y=8 (dopo la scritta di 8px)
    int y_pos = 11; // +3px verso il basso
    
    if (!dualAnimation) {
      int x_pos=(DISP_W-currentAnimWidth)/2;
      u8g2.drawBitmap(DX(x_pos),DY(y_pos),(currentAnimWidth+7)/8,currentAnimHeight,currentAnimation[currentFrame]);
    } else {
      int x_pos1=(DISP_W/3)-(currentAnimWidth/2)-10; // -10px a sinistra
      int x_pos2=(2*DISP_W/3)-(currentAnimWidth/2);
      u8g2.drawBitmap(DX(x_pos1),DY(y_pos),(currentAnimWidth+7)/8,currentAnimHeight,currentAnimation[currentFrame]);
      u8g2.drawBitmap(DX(x_pos2),DY(y_pos),(currentAnimWidth+7)/8,currentAnimHeight,currentAnimation1[currentFrame]);
    }
  }

  u8g2.sendBuffer();
}

void AllLEDoff() {
  digitalWrite(PIN_LED_SOLE, LOW);
  digitalWrite(PIN_LED_NUVOLE, LOW);
  digitalWrite(PIN_LED_PIOGGIA, LOW);
  digitalWrite(PIN_LED_TEMPORALE, LOW);
  digitalWrite(PIN_LED_LUNA, LOW);
}

// --- FUNZIONE updateWeatherLEDs CORRETTA per TUTTI i giorni di previsione ---
void updateWeatherLEDs() {
  // Spegni i LED solo se non siamo in modalità che li richiedono spenti
  if (Currentmode != MODO_LED_OFF && Currentmode != MODO_ALL_OFF && Currentmode != MODO_NOTTE) {
    AllLEDoff();

    // CALCOLO CORRETTO DI isDaytime per TUTTI i giorni di previsione
    bool isDaytime;
    
    if (forecastDay == -1) {
      // Per ATTUALE: usa ora corrente e dati alba/tramonto reali
            time_t currentTime = getCurrentEpoch();
      isDaytime = (currentTime >= sunriseTime && currentTime < sunsetTime);
    } else {
      // Per TUTTI i giorni di previsione (OGGI, DOMANI, DOPODOMANI, GIORNO 4, GIORNO 5):
      // usa il PERIODO selezionato per determinare giorno/notte
      switch(forecastPeriod) {
        case 0: // Mattino (06-12) - sempre giorno
        case 1: // Pomeriggio (12-18) - sempre giorno
          isDaytime = true;
          break;
        case 2: // Sera (18-00) - sempre notte
          isDaytime = false;
          break;
      }
    }

    uint32_t tempNeopixelBaseColor = pixels.Color(0, 0, 0);

    // LOGICA DI ACCENSIONE LED BASATA SUL CODICE METEO (ALLINEATA ALLE ANIMAZIONI)
    // TEMPORALE (200-299)
    if (weatherConditionCode >= 200 && weatherConditionCode < 300) {
      digitalWrite(PIN_LED_PIOGGIA, HIGH);
      digitalWrite(PIN_LED_TEMPORALE, HIGH);
      tempNeopixelBaseColor = pixels.Color(197, 0, 255);

    }
    // PIOGGERELLA (300-399)
    else if (weatherConditionCode >= 300 && weatherConditionCode < 400) {
      digitalWrite(PIN_LED_PIOGGIA, HIGH);
      digitalWrite(PIN_LED_NUVOLE, HIGH);
      tempNeopixelBaseColor = pixels.Color(0, 0, 255);

    }
    // PIOGGIA (500-599)
    else if (weatherConditionCode >= 500 && weatherConditionCode < 600) {
      digitalWrite(PIN_LED_PIOGGIA, HIGH);
      digitalWrite(PIN_LED_NUVOLE, HIGH);
      tempNeopixelBaseColor = pixels.Color(0, 180, 255);

    }
    // NEVE (600-699)
    else if (weatherConditionCode >= 600 && weatherConditionCode < 700) {
      digitalWrite(PIN_LED_PIOGGIA, HIGH);  // Usa LED pioggia anche per neve
      digitalWrite(PIN_LED_NUVOLE, HIGH);
      tempNeopixelBaseColor = pixels.Color(200, 200, 255);  // Colore più chiaro/azzurro per neve

    }
    // CONDIZIONI ATMOSFERICHE (700-799): Nebbia, Fumo, Foschia, Sabbia, Polvere, ecc.
    else if (weatherConditionCode >= 700 && weatherConditionCode < 800) {
      // CORREZIONE: Non è pioggia! Mostra SOLE/LUNA + NUVOLE come nell'animazione
      if (isDaytime) {
        digitalWrite(PIN_LED_SOLE, HIGH);
        digitalWrite(PIN_LED_NUVOLE, HIGH);
        tempNeopixelBaseColor = pixels.Color(200, 180, 100);  // Giallo sporco per atmosfera
      } else {
        digitalWrite(PIN_LED_LUNA, HIGH);
        digitalWrite(PIN_LED_NUVOLE, HIGH);
        tempNeopixelBaseColor = pixels.Color(100, 100, 120);  // Grigio bluastro
      }

    }
    // CIELO SERENO (800)
    else if (weatherConditionCode == 800) {
      if (isDaytime) {
        digitalWrite(PIN_LED_SOLE, HIGH);
        tempNeopixelBaseColor = pixels.Color(255, 227, 0);
      } else {
        digitalWrite(PIN_LED_LUNA, HIGH);
        tempNeopixelBaseColor = pixels.Color(128, 128, 128);
      }

    }
    // NUVOLE (801-804)
    else if (weatherConditionCode >= 801 && weatherConditionCode <= 804) {
      if (isDaytime) {
        digitalWrite(PIN_LED_SOLE, HIGH);
        digitalWrite(PIN_LED_NUVOLE, HIGH);
        tempNeopixelBaseColor = pixels.Color(255, 227, 0);
      } else {
        digitalWrite(PIN_LED_LUNA, HIGH);
        digitalWrite(PIN_LED_NUVOLE, HIGH);
        tempNeopixelBaseColor = pixels.Color(128, 128, 128);
      }
    }

    // CORREZIONE: Aggiorna neopixelBaseColor solo in modalità AUTO
    // In modalità colore manuale, neopixelBaseColor viene gestito da SetRingColor
    if (Currentmode == MODO_AUTO) {
      neopixelBaseColor = tempNeopixelBaseColor;
    }
    
    // DEBUG: Stampa lo stato dei LED per verifica
    Serial.println("=== STATO LED INDIVIDUALI ===");
    Serial.print("Giorno previsione: "); Serial.println(forecastDay);
    Serial.print("Periodo: "); Serial.println(forecastPeriod);
    Serial.print("SOLE: "); Serial.println(digitalRead(PIN_LED_SOLE));
    Serial.print("NUVOLE: "); Serial.println(digitalRead(PIN_LED_NUVOLE));
    Serial.print("PIOGGIA: "); Serial.println(digitalRead(PIN_LED_PIOGGIA));
    Serial.print("TEMPORALE: "); Serial.println(digitalRead(PIN_LED_TEMPORALE));
    Serial.print("LUNA: "); Serial.println(digitalRead(PIN_LED_LUNA));
    Serial.print("isDaytime: "); Serial.println(isDaytime);
    Serial.print("weatherConditionCode: "); Serial.println(weatherConditionCode);
    Serial.print("Descrizione: "); Serial.println(getWeatherDescriptionItalian(weatherConditionCode));
    Serial.println("=============================");
    
  } else {
    // Se siamo in modalità che richiedono LED spenti, assicuriamoci che siano spenti
    AllLEDoff();
  }
}

void setNeopixelColor(uint32_t color) {
  neopixelBaseColor = color;

  if (Currentmode != MODO_AUTO && Currentmode != MODO_LED_OFF && Currentmode != MODO_ALL_OFF && Currentmode != MODO_NOTTE) {
    pixels.setBrightness(savedNeopixelBrightness);
    for (int i = 0; i < NUM_NEOPIXELS; i++) {
      pixels.setPixelColor(i, neopixelBaseColor);
    }
    pixels.show();
  }
}

void animateNeopixelCircular() {
  uint32_t targetColor = neopixelBaseColor;

  pixels.clear();
  pixels.show();

  for (int i = 0; i < NUM_NEOPIXELS; i++) {
    pixels.setPixelColor(i, targetColor);
    pixels.show();
    delay(NEOPIXEL_BOOT_ANIM_DELAY);
  }
}

void ActivateCircular(uint32_t targetColor) {
  pixels.clear();
  for (int i = 0; i < NUM_NEOPIXELS; i++) {
    pixels.setPixelColor(i, targetColor);
  }
  pixels.show();
}

void runNeopixelAnimation() {
  // Se non siamo in modalità AUTO, non eseguire l'animazione basata sul meteo
  if (Currentmode != MODO_AUTO) {
    return;
  }
  
  uint8_t animatedRed = 0;
  uint8_t animatedGreen = 0;
  uint8_t animatedBlue = 0;

  static unsigned long lastUpdate = 0;
  unsigned long currentMillis = millis();

  static int currentBreathBrightness = NEOPIXEL_BREATH_MIN;
  static bool breathIncreasing = true;

  const unsigned long TARGET_CYCLE_DURATION_MS = 4000;
  const int ACTUAL_BRIGHTNESS_RANGE = NEOPIXEL_BREATH_MAX - NEOPIXEL_BREATH_MIN;

  unsigned long BREATH_UPDATE_INTERVAL_MS = 0;
  if (ACTUAL_BRIGHTNESS_RANGE > 0) {
    BREATH_UPDATE_INTERVAL_MS = TARGET_CYCLE_DURATION_MS / (2 * ACTUAL_BRIGHTNESS_RANGE);
  } else {
    BREATH_UPDATE_INTERVAL_MS = BREATH_UPDATE_SPEED;
  }

  if (BREATH_UPDATE_INTERVAL_MS == 0) {
    BREATH_UPDATE_INTERVAL_MS = 1;
  }

  if (currentMillis - lastUpdate >= BREATH_UPDATE_INTERVAL_MS) {
    lastUpdate = currentMillis;

    if (breathIncreasing) {
      currentBreathBrightness += 1;
      if (currentBreathBrightness >= NEOPIXEL_BREATH_MAX) {
        currentBreathBrightness = NEOPIXEL_BREATH_MAX;
        breathIncreasing = false;
      }
    } else {
      currentBreathBrightness -= 1;
      if (currentBreathBrightness <= NEOPIXEL_BREATH_MIN) {
        currentBreathBrightness = NEOPIXEL_BREATH_MIN;
        breathIncreasing = true;
      }
    }

    uint8_t red = (neopixelBaseColor >> 16) & 0xFF;
    uint8_t green = (neopixelBaseColor >> 8) & 0xFF;
    uint8_t blue = neopixelBaseColor & 0xFF;

    if (neopixelBaseColor == pixels.Color(0, 0, 0)) {
      pixels.clear();
    } else {

      int breathRange = NEOPIXEL_BREATH_MAX - NEOPIXEL_BREATH_MIN;
      if (breathRange <= 0) breathRange = 1;

      if (red == 0) {
        animatedRed = 0;
      } else {
        int minRed = NEOPIXEL_BREATH_MIN;
        if (minRed > red) minRed = red;
        animatedRed = minRed + (currentBreathBrightness - NEOPIXEL_BREATH_MIN) * (red - minRed) / breathRange;
      }

      if (green == 0) {
        animatedGreen = 0;
      } else {
        int minGreen = NEOPIXEL_BREATH_MIN;
        if (minGreen > green) minGreen = green;
        animatedGreen = minGreen + (currentBreathBrightness - NEOPIXEL_BREATH_MIN) * (green - minGreen) / breathRange;
      }

      if (blue == 0) {
        animatedBlue = 0;
      } else {
        int minBlue = NEOPIXEL_BREATH_MIN;
        if (minBlue > blue) minBlue = blue;
        animatedBlue = minBlue + (currentBreathBrightness - NEOPIXEL_BREATH_MIN) * (blue - minBlue) / breathRange;
      }

      for (int i = 0; i < NUM_NEOPIXELS; i++) {
        pixels.setPixelColor(i, pixels.Color(animatedRed, animatedGreen, animatedBlue));
      }
    }
    pixels.show();
  }
}

void runWindAnimation() {
  static unsigned long lastTrailUpdate = 0;
  static unsigned long windAnimStartTime = 0;

  // VARIABILI STATICHE PER EVITARE CHE CAMBINO DURANTE L'ANIMAZIONE
  static int savedBeaufort = 0;
  static int savedTrailLength = 0;
  static uint8_t savedWindR = 0;
  static uint8_t savedWindG = 0;
  static uint8_t savedWindB = 0;
  static int savedWindLedIndex = 0;
  static int savedTotalFrames = 0;
  static int prevAnimationFrame = -1;  // Per tracciare quando l'animazione riparte

  const unsigned long TRAIL_TOTAL_DURATION = 4000;
  const int FADE_FRAMES_PER_LED = 8;
  const int FADE_PEAK = FADE_FRAMES_PER_LED / 2;
  const unsigned long ANIMATION_TIMEOUT = 10000; // Timeout di sicurezza: 10 secondi

  // All'inizio dell'animazione (quando animationFrame passa a 0), salva tutti i parametri e resetta i timer
  if (animationFrame == 0 && prevAnimationFrame != 0) {
    windAnimStartTime = millis();
    lastTrailUpdate = millis();  // RESET del timer solo quando l'animazione riparte
    savedBeaufort = getBeaufortScale(windSpeed);
    savedTrailLength = getTrailLengthByBeaufort(savedBeaufort);
    getWindColorByBeaufort(savedBeaufort, savedWindR, savedWindG, savedWindB);

    savedWindLedIndex = (360 - windDirection + 90) % 360;
    savedWindLedIndex = map(savedWindLedIndex, 0, 360, 0, NUM_NEOPIXELS);

    savedTotalFrames = (savedTrailLength + 1) * FADE_FRAMES_PER_LED;

    Serial.print("Inizio animazione vento - Beaufort: ");
    Serial.print(savedBeaufort);
    Serial.print(", TrailLength: ");
    Serial.print(savedTrailLength);
    Serial.print(", TotalFrames: ");
    Serial.println(savedTotalFrames);
  }

  prevAnimationFrame = animationFrame;  // Aggiorna il frame precedente

  // Timeout di sicurezza: se l'animazione dura troppo, termina forzatamente
  if (millis() - windAnimStartTime > ANIMATION_TIMEOUT) {
    Serial.println("TIMEOUT animazione vento - Forzando terminazione");
    animationFrame = 0;
    windAnimationActive = false;
    return;
  }

  // Usa i valori salvati invece di ricalcolarli
  int beaufort = savedBeaufort;
  int trailLength = savedTrailLength;
  uint8_t windR = savedWindR;
  uint8_t windG = savedWindG;
  uint8_t windB = savedWindB;
  int windLedIndex = savedWindLedIndex;
  int totalFrames = savedTotalFrames;

  unsigned long frameDuration = TRAIL_TOTAL_DURATION / max(totalFrames, 1);

  uint8_t ledR[NUM_NEOPIXELS] = { 0 };
  uint8_t ledG[NUM_NEOPIXELS] = { 0 };
  uint8_t ledB[NUM_NEOPIXELS] = { 0 };

  for (int offset = 0; offset <= trailLength; offset++) {
    int startFrame = offset * FADE_FRAMES_PER_LED;

    for (int f = 0; f < FADE_FRAMES_PER_LED; f++) {
      int activeFrame = startFrame + f;

      if (animationFrame >= activeFrame) {
        float intensity;
        if (animationFrame <= activeFrame + FADE_PEAK) {
          intensity = (float)(animationFrame - activeFrame) / FADE_PEAK;
        } else {
          intensity = 1.0;
        }

        intensity = constrain(intensity, 0.0, 1.0);

        uint8_t r = windR * intensity;
        uint8_t g = windG * intensity;
        uint8_t b = windB * intensity;

        int leftIndex = (windLedIndex - offset + NUM_NEOPIXELS) % NUM_NEOPIXELS;
        int rightIndex = (windLedIndex + offset) % NUM_NEOPIXELS;

        ledR[leftIndex] = max(ledR[leftIndex], r);
        ledG[leftIndex] = max(ledG[leftIndex], g);
        ledB[leftIndex] = max(ledB[leftIndex], b);

        ledR[rightIndex] = max(ledR[rightIndex], r);
        ledG[rightIndex] = max(ledG[rightIndex], g);
        ledB[rightIndex] = max(ledB[rightIndex], b);
      }
    }
  }

  ledR[windLedIndex] = windR;
  ledG[windLedIndex] = windG;
  ledB[windLedIndex] = windB;

  for (int i = 0; i < NUM_NEOPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(ledR[i], ledG[i], ledB[i]));
  }
  pixels.show();

  if (millis() - lastTrailUpdate >= frameDuration) {
    lastTrailUpdate = millis();
    animationFrame++;

    if (animationFrame > totalFrames + FADE_FRAMES_PER_LED) {
      animationFrame = 0;
      windAnimationActive = false;
      Serial.println("Animazione scia vento terminata - Passaggio a NEOPIXEL BREATHING");
    }
  }
}

int getBeaufortScale(float windSpeed) {
  if (windSpeed < 1) return 0;
  else if (windSpeed <= 5) return 1;
  else if (windSpeed <= 11) return 2;
  else if (windSpeed <= 19) return 3;
  else if (windSpeed <= 28) return 4;
  else if (windSpeed <= 38) return 5;
  else if (windSpeed <= 49) return 6;
  else if (windSpeed <= 61) return 7;
  else if (windSpeed <= 74) return 8;
  else if (windSpeed <= 88) return 9;
  else if (windSpeed <= 102) return 10;
  else if (windSpeed <= 117) return 11;
  else return 12;
}

void getWindColorByBeaufort(int beaufort, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (beaufort == 0) {
    r = g = b = 0;
  } else if (beaufort <= 3) {
    r = 0;
    g = 255;
    b = 0;
  } else if (beaufort <= 6) {
    r = 255;
    g = 165;
    b = 0;
  } else if (beaufort <= 9) {
    r = 255;
    g = 0;
    b = 0;
  } else {
    r = 128;
    g = 0;
    b = 128;
  }
}

int getTrailLengthByBeaufort(int beaufort) {
  if (beaufort == 0) return 0;
  if (beaufort == 1) return 1;
  if (beaufort == 2) return 2;
  if (beaufort == 3) return 3;
  if (beaufort == 4) return 2;
  if (beaufort == 5) return 3;
  if (beaufort == 6) return 4;
  if (beaufort == 7) return 3;
  if (beaufort == 8) return 4;
  if (beaufort == 9) return 5;
  return 6;
}

int calcolaColoreFinale(int iniziale, int VAR, int valMin, int valMax) {
  if (iniziale == 0) return 0;

  int delta = 50;
  int coloreMin = iniziale - delta;
  if (coloreMin < 0) coloreMin = 0;

  int range = valMax - valMin;
  if (range <= 0) return iniziale;

  int incremento = (VAR - valMin) * (iniziale - coloreMin) / range;
  return coloreMin + incremento;
}

// --- FUNZIONE SetRingColor ---
void SetRingColor(int Mode, bool animate) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x13B_tr);
  const char* mn="";
  switch(Mode){
    case 0:mn="AUTO";break; case 1:mn="ROSSO";break; case 2:mn="VERDE";break;
    case 3:mn="BLU";break;  case 4:mn="VIOLA";break; case 5:mn="AZZURRO";break;
    case 6:mn="GIALLO";break;case 7:mn="BIANCO";break;case 8:mn="AN.OFF";break;
    case 9:mn="LED OFF";break;case 10:mn="SPENTO";break;case 11:mn="NOTTE";break;
  }
  { int nw=strlen(mn)*7; int nx=(DISP_W-nw)/2; if(nx<0)nx=0;
    u8g2.drawStr(DX(nx),DY(15),mn); }
  u8g2.sendBuffer();
  // buffer mantenuto con nome modalità
  switch (Mode) {
    case MODO_AUTO:
      // Non cambiare neopixelBaseColor, perché sarà impostato dal meteo
      // Avvia animazione a onda fluida se abilitata
      if (animate && neopixelAnimationEnabled) {
        neopixelModeChangeAnimation = true;
        neopixelAnimStep = 0;
        neopixelAnimLastUpdate = millis();
      } else {
        // Senza animazione, accendi direttamente l'anello con il colore di base
        for (int i = 0; i < NUM_NEOPIXELS; i++) {
          pixels.setPixelColor(i, neopixelBaseColor);
        }
        pixels.show();
      }
      break;
    case MODO_ROSSO:
      neopixelBaseColor = pixels.Color(255, 0, 0);
      break;
    case MODO_VERDE:
      neopixelBaseColor = pixels.Color(0, 255, 0);
      break;
    case MODO_BLU:
      neopixelBaseColor = pixels.Color(0, 0, 255);
      break;
    case MODO_VIOLA:
      neopixelBaseColor = pixels.Color(197, 0, 255);
      break;
    case MODO_CIANO:
      neopixelBaseColor = pixels.Color(0, 255, 255);
      break;
    case MODO_GIALLO:
      neopixelBaseColor = pixels.Color(255, 227, 0);
      break;
    case MODO_BIANCO:
      neopixelBaseColor = pixels.Color(255, 255, 255);
      break;
    case MODO_RING_OFF:
      neopixelBaseColor = pixels.Color(0, 0, 0);
      break;
    case MODO_LED_OFF:
      neopixelBaseColor = pixels.Color(0, 0, 0);
      break;
    case MODO_ALL_OFF:
      neopixelBaseColor = pixels.Color(0, 0, 0);
      break;
    case MODO_NOTTE:
      neopixelBaseColor = pixels.Color(0, 0, 0);
      break;
  }
  
  // CORREZIONE: Gestione consistente dei LED in tutte le modalità
  if (Mode == MODO_LED_OFF || Mode == MODO_ALL_OFF || Mode == MODO_NOTTE) {
    // Modalità che richiedono LED spenti
    AllLEDoff();
    pixels.clear();
    pixels.show();
  } else if (Mode == MODO_RING_OFF) {
    // Solo anello spento, LED individuali attivi (NON chiamare AllLEDoff() qui)
    pixels.clear();
    pixels.show();
    // I LED individuali mantengono il loro stato basato sul meteo
  } else if (Mode >= MODO_ROSSO && Mode <= MODO_BIANCO) {
    // Modalità colore: LED individuali rimangono attivi con lo stato meteo, anello colorato
    // NON chiamare AllLEDoff() qui - i LED individuali mantengono il loro stato
    if (animate && neopixelAnimationEnabled) {
      // Avvia animazione cambio modalità a onda fluida
      neopixelModeChangeAnimation = true;
      neopixelAnimStep = 0;
      neopixelAnimLastUpdate = millis();
    } else {
      // Imposta il colore direttamente senza animazione
      for (int i = 0; i < NUM_NEOPIXELS; i++) {
        pixels.setPixelColor(i, neopixelBaseColor);
      }
      pixels.show();
    }
  } else if (Mode == MODO_AUTO) {
    // Modalità AUTO: LED individuali gestiti dal meteo, anello con colore di base
    // L'animazione è già gestita nello switch sopra
  }
  
  // Applica sempre la luminosità corrente
  pixels.setBrightness(savedNeopixelBrightness);
  pixels.show();
}

void checkNightMode() {
  static bool nightModeActive = false;

  if (!nightModeEnabled) {
    if (nightModeActive) {
      Currentmode = savedMode;
      nightModeActive = false;
      SetRingColor(Currentmode, false); // Ripristina la modalità precedente senza animazione
      Serial.println("❌ Modalità notturna disattivata manualmente. Ripristino modalità precedente.");
    }
    return;
  }

    time_t now = getCurrentEpoch();
  struct tm *timeinfo = localtime(&now);
  int currentMinutes = timeinfo->tm_hour * 60 + timeinfo->tm_min;
  int startMinutes = nightStartHour * 60 + nightStartMinute;
  int endMinutes = nightEndHour * 60 + nightEndMinute;

  bool isNight;
  if (startMinutes < endMinutes) {
    isNight = (currentMinutes >= startMinutes && currentMinutes < endMinutes);
  } else {
    isNight = (currentMinutes >= startMinutes || currentMinutes < endMinutes);
  }

  if (isNight && !nightModeActive) {
    if (Currentmode != MODO_NOTTE) {
      savedMode = Currentmode;
      Currentmode = MODO_NOTTE;
      nightModeActive = true;

      // Spegni tutto immediatamente
      AllLEDoff();
      windAnimationActive = false;
      windScanning = false;
      pixels.clear();
      pixels.show();

      Serial.println("🌙 Entrata in modalità notte: TUTTI i LED spenti, solo display attivo");
    }
  } else if (!isNight && nightModeActive) {
    Currentmode = savedMode;
    nightModeActive = false;
    
    SetRingColor(Currentmode, true); // MODIFICATO: Con animazione al ripristino
    Serial.println("🌅 Uscita dalla modalità notte. Ripristino modalità precedente.");
  }
}

// --- IMPLEMENTAZIONE GESTORI SERVER WEB STANDARD ---

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  // CORREZIONE: Aggiunto charset UTF-8 per le emoji
  html += "<meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>" + ProductName + " Config</title>";
  html += "<style>";
  html += "* { box-sizing: border-box; margin: 0; padding: 0; }";
  html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #000000; color: #ffffff; line-height: 1.6; padding: 20px; min-height: 100vh; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: #1a1a1a; border-radius: 20px; box-shadow: 0 15px 35px rgba(0, 0, 0, 0.8); overflow: hidden; }";
  html += ".header { background: linear-gradient(135deg, #2c3e50 0%, #3498db 100%); color: white; padding: 30px 20px; text-align: center; }";
  html += ".header h2 { font-size: 2.2em; margin-bottom: 10px; text-shadow: 2px 2px 4px rgba(0,0,0,0.2); }";
  html += ".header p { font-size: 1.1em; opacity: 0.9; }";
  html += ".content { padding: 30px; }";
  html += ".weather-card { background: linear-gradient(135deg, #34495e 0%, #2c3e50 100%); border-radius: 15px; padding: 25px; margin-bottom: 25px; box-shadow: 0 8px 25px rgba(0,0,0,0.5); }";
  html += ".weather-card h3 { color: #ffffff; margin-bottom: 15px; font-size: 1.4em; border-bottom: 2px solid rgba(255,255,255,0.3); padding-bottom: 8px; }";
  html += ".weather-info { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }";
  html += ".weather-item { background: rgba(255,255,255,0.1); padding: 12px; border-radius: 10px; text-align: center; }";
  html += ".weather-label { font-weight: bold; color: #ffffff; font-size: 0.9em; }";
  html += ".weather-value { font-size: 1.2em; color: #ffffff; font-weight: bold; margin-top: 5px; }";
  html += ".config-section { background: linear-gradient(135deg, #34495e 0%, #2c3e50 100%); border-radius: 15px; padding: 25px; margin-bottom: 25px; box-shadow: 0 8px 25px rgba(0,0,0,0.5); }";
  html += ".config-section h3 { color: #ffffff; margin-bottom: 20px; font-size: 1.4em; border-bottom: 2px solid rgba(255,255,255,0.3); padding-bottom: 8px; }";
  html += ".form-group { margin-bottom: 20px; }";
  html += "label { display: block; margin-bottom: 8px; font-weight: 600; color: #ffffff; }";
  html += "input[type=text], input[type=time], select { width: 100%; padding: 12px 15px; border: 2px solid #555; border-radius: 10px; font-size: 16px; transition: all 0.3s ease; background: #333333; color: #ffffff; }";
  html += "input[type=text]:focus, input[type=time]:focus, select:focus { border-color: #3498db; outline: none; box-shadow: 0 0 0 3px rgba(52, 152, 219, 0.3); }";
  html += "input[type=range] { width: 100%; height: 8px; border-radius: 5px; background: #555; outline: none; margin: 15px 0; }";
  html += "input[type=range]::-webkit-slider-thumb { appearance: none; width: 22px; height: 22px; border-radius: 50%; background: #3498db; cursor: pointer; box-shadow: 0 2px 6px rgba(0,0,0,0.2); }";
  html += ".range-value { text-align: center; font-weight: bold; color: #3498db; font-size: 1.2em; margin-top: -10px; }";
  html += ".toggle-switch { position: relative; display: inline-block; width: 60px; height: 34px; margin-left: 15px; }";
  html += ".toggle-switch input { opacity: 0; width: 0; height: 0; }";
  html += ".slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #555; transition: .4s; border-radius: 34px; }";
  html += ".slider:before { position: absolute; content: \"\"; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }";
  html += "input:checked + .slider { background: linear-gradient(135deg, #2c3e50 0%, #3498db 100%); }";
  html += "input:checked + .slider:before { transform: translateX(26px); }";
  html += ".toggle-label { display: flex; align-items: center; margin-bottom: 15px; }";
  html += "button { background: linear-gradient(135deg, #2c3e50 0%, #3498db 100%); color: white; border: none; padding: 15px 30px; border-radius: 50px; font-size: 18px; font-weight: bold; cursor: pointer; transition: all 0.3s ease; box-shadow: 0 5px 15px rgba(0,0,0,0.2); width: 100%; margin-top: 10px; }";
  html += "button:hover { transform: translateY(-2px); box-shadow: 0 8px 20px rgba(0,0,0,0.3); background: linear-gradient(135deg, #3498db 0%, #2c3e50 100%); }";
  html += ".form-row { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }";
  html += "@media (max-width: 768px) { .form-row { grid-template-columns: 1fr; } }";
  html += "small { display: block; margin-top: 5px; font-size: 0.85em; color: rgba(255,255,255,0.6); }";
  html += "</style>";
  html += "</head><body>";
  
  html += "<div class=\"container\">";
  html += "<div class=\"header\">";
  // CORREZIONE: Emoji sostituita con una più compatibile
  html += "<h2>⛅ " + ProductName + "</h2>";
  html += "<p>Configurazione Sistema Meteo</p>";
  html += "</div>";
  
  html += "<div class=\"content\">";
  
  // Sezione Informazioni Meteo Corrente
  html += "<div class=\"weather-card\">";
  html += "<h3>📊 Informazioni Meteo Corrente</h3>";
  html += "<div class=\"weather-info\">";
  html += "<div class=\"weather-item\"><div class=\"weather-label\">🌡️ Temperatura</div><div class=\"weather-value\">" + String(temperatureCelsius, 1) + " °C</div></div>";
  html += "<div class=\"weather-item\"><div class=\"weather-label\">💧 Umidità</div><div class=\"weather-value\">" + String(humidity) + " %</div></div>";
  html += "<div class=\"weather-item\"><div class=\"weather-label\">💨 Vento</div><div class=\"weather-value\">" + String(windSpeed, 1) + " km/h</div></div>";
  html += "<div class=\"weather-item\"><div class=\"weather-label\">🧭 Direzione</div><div class=\"weather-value\">" + String(windDirection) + "°</div></div>";
  html += "<div class=\"weather-item\"><div class=\"weather-label\">☁️ Condizioni</div><div class=\"weather-value\">" + getWeatherDescriptionItalian(weatherConditionCode) + "</div></div>";
  html += "<div class=\"weather-item\"><div class=\"weather-label\">📍 Città</div><div class=\"weather-value\">" + String(savedCity) + "</div></div>";
  html += "</div>";
  html += "</div>";
  
  html += "<form action=\"/save\" method=\"post\">";
  
  // Sezione Configurazione Base
  html += "<div class=\"config-section\">";
  html += "<h3>⚙️ Configurazione Base</h3>";
  
  html += "<div class=\"form-group\">";
  html += "<label for=\"brightness\">💡 Luminosità LED (" + String(savedNeopixelBrightness) + "):</label>";
  html += "<input type=\"range\" id=\"brightness\" name=\"brightness\" min=\"10\" max=\"255\" value=\"" + String(savedNeopixelBrightness) + "\" oninput=\"document.getElementById('brightnessValue').innerText = this.value\">";
  html += "<div class=\"range-value\" id=\"brightnessValue\">" + String(savedNeopixelBrightness) + "</div>";
  html += "</div>";
  
  html += "<div class=\"form-group\">";
  html += "<label for=\"mode_select\">🎨 Modalità Visualizzazione:</label>";
  html += "<select id=\"mode_select\" name=\"mode_select\">";
  String modes[] = {
    "Automatica", "Luce Rossa", "Luce Verde", "Luce Blu", "Luce Viola",
    "Luce Azzurra", "Luce Gialla", "Luce Bianca", "Anello Spento",
    "LED Spenti", "Tutto Spento", "Modalità Notte"
  };
  for (int i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
    html += "<option value=\"" + String(i) + "\"" + (Currentmode == i ? " selected" : "") + ">" + modes[i] + "</option>";
  }
  html += "</select>";
  html += "</div>";

  // Toggle animazione cambio modalità
  html += "<div class=\"toggle-label\">";
  html += "<label for=\"neopixel_anim\">✨ Abilita Animazione Cambio Modalità:</label>";
  html += "<label class=\"toggle-switch\">";
  html += String("<input type=\"checkbox\" id=\"neopixel_anim\" name=\"neopixel_anim\" ") + (neopixelAnimationEnabled ? "checked" : "") + ">";
  html += "<span class=\"slider\"></span>";
  html += "</label>";
  html += "</div>";

  html += "</div>";

  // Sezione Configurazione Notturna
  html += "<div class=\"config-section\">";
  html += "<h3>🌙 Configurazione Notturna</h3>";
  
  html += "<div class=\"toggle-label\">";
  html += "<label for=\"night_mode\">Attiva Modalità Notturna:</label>";
  html += "<label class=\"toggle-switch\">";
  html += String("<input type=\"checkbox\" id=\"night_mode\" name=\"night_mode\" ") + (nightModeEnabled ? "checked" : "") + ">";
  html += "<span class=\"slider\"></span>";
  html += "</label>";
  html += "</div>";
  
  html += "<div class=\"form-row\">";
  html += "<div class=\"form-group\">";
  html += "<label for=\"night_start\">Ora Inizio:</label>";
  html += String("<input type=\"time\" id=\"night_start\" name=\"night_start\" value=\"") + (nightStartHour < 10 ? "0" : "") + String(nightStartHour) + ":" + (nightStartMinute < 10 ? "0" : "") + String(nightStartMinute) + "\">";
  html += "</div>";
  
  html += "<div class=\"form-group\">";
  html += "<label for=\"night_end\">Ora Fine:</label>";
  html += String("<input type=\"time\" id=\"night_end\" name=\"night_end\" value=\"") + (nightEndHour < 10 ? "0" : "") + String(nightEndHour) + ":" + (nightEndMinute < 10 ? "0" : "") + String(nightEndMinute) + "\">";
  html += "</div>";
  html += "</div>";
  
  html += "</div>";
  
  // Sezione Località e Previsioni
  html += "<div class=\"config-section\">";
  html += "<h3>📍 Località e Previsioni</h3>";
  
  html += "<div class=\"form-group\">";
  html += "<label for=\"city_select\">Seleziona Città:</label>";
  html += "<select id=\"city_select\" name=\"city_select\">";
  html += "<option value=\"\">-- Seleziona --</option>";
  String cities[] = {
    "Agrigento", "Alessandria", "Ancona", "Aosta", "L'Aquila", "Arezzo", "Ascoli Piceno", "Asti", "Avellino", "Bari",
    "Barletta-Andria-Trani", "Belluno", "Benevento", "Bergamo", "Biella", "Bologna", "Bolzano", "Brescia", "Brindisi", "Cagliari",
    "Caltanissetta", "Campobasso", "Caserta", "Catania", "Catanzaro", "Chieti", "Como", "Cosenza", "Cremona", "Crotone",
    "Cuneo", "Enna", "Fermo", "Ferrara", "Firenze", "Foggia", "Forlì-Cesena", "Frosinone", "Genova", "Gorizia",
    "Grosseto", "Imperia", "Isernia", "La Spezia", "Latina", "Lecce", "Lecco", "Livorno", "Lodi", "Lucca",
    "Macerata", "Mantova", "Massa-Carrara", "Matera", "Messina", "Milano", "Modena", "Monza", "Napoli", "Novara",
    "Nuoro", "Oristano", "Padova", "Palermo", "Parma", "Pavia", "Perugia", "Pesaro e Urbino", "Pescara", "Piacenza",
    "Pisa", "Pistoia", "Pordenone", "Potenza", "Prato", "Ragusa", "Ravenna", "Reggio Calabria", "Reggio Emilia", "Rieti",
    "Rimini", "Roma", "Rovigo", "Sabaudia", "Salerno", "Sassari", "Savona", "Siena", "Siracusa", "Sondrio",
    "Taranto", "Taverna", "Teramo", "Terni", "Torino", "Trapani", "Trento", "Treviso", "Trieste", "Udine",
    "Varese", "Venezia", "Verbania", "Vercelli", "Verona", "Vibo Valentia", "Vicenza", "Viterbo"
  };
  for (int i = 0; i < sizeof(cities) / sizeof(cities[0]); i++) {
    html += "<option value=\"" + cities[i] + "\"" + (String(savedCity).equalsIgnoreCase(cities[i]) ? " selected" : "") + ">" + cities[i] + "</option>";
  }
  html += "</select>";
  html += "</div>";
  
  html += "<div class=\"form-group\">";
  html += "<label for=\"city_manual\">Inserisci Città Manualmente:</label>";
  html += "<input type=\"text\" id=\"city_manual\" name=\"city_manual\" value=\"" + String(savedCity) + "\" placeholder=\"Es: Milano, Torino, Napoli...\">";
  html += "</div>";
  
  html += "<div class=\"form-row\">";
  html += "<div class=\"form-group\">";
  html += "<label for=\"forecastDay\">📅 Giorno Previsione:</label>";
  html += "<select id=\"forecastDay\" name=\"forecastDay\">";
  String giorni[] = { "Attuale", "Oggi", "Domani", "Dopodomani", "Giorno 4", "Giorno 5" };
  for (int i = -1; i < 5; i++) {
    html += "<option value=\"" + String(i) + "\"" + (forecastDay == i ? " selected" : "") + ">" + giorni[i + 1] + "</option>";
  }
  html += "</select>";
  html += "</div>";
  
  html += "<div class=\"form-group\">";
  html += "<label for=\"forecastPeriod\">⏰ Periodo Giorno:</label>";
  html += "<select id=\"forecastPeriod\" name=\"forecastPeriod\">";
  String periodi[] = { "Mattino (06-12)", "Pomeriggio (12-18)", "Sera (18-00)" };
  for (int i = 0; i < 3; i++) {
    html += "<option value=\"" + String(i) + "\"" + (forecastPeriod == i ? " selected" : "") + ">" + periodi[i] + "</option>";
  }
  html += "</select>";
  html += "</div>";
  html += "</div>";
  
  html += "</div>";

  html += "<div class=\"config-section\">";
  html += "<h3>🔑 Configurazione API</h3>";
  html += "<div class=\"form-group\">";
  html += "<label for=\"api_key\">OpenWeather API Key:</label>";

  // Recupera l'API key corrente
  preferences.begin("meteo-config", false);
  String currentApiKey = preferences.getString("api_key", "");
  preferences.end();

  // Mostra l'API key mascherata se presente
  String displayApiKey = currentApiKey;
  if (currentApiKey.length() > 8) {
    displayApiKey = currentApiKey.substring(0, 4) + "****" + currentApiKey.substring(currentApiKey.length() - 4);
  }

  html += "<input type=\"text\" id=\"api_key\" name=\"api_key\" value=\"" + currentApiKey + "\" placeholder=\"Inserisci la tua API key di OpenWeatherMap\">";

  // Messaggio di stato API key
  if (currentApiKey.length() > 0) {
    html += "<small style=\"color: #4CAF50; display: block; margin-top: 5px;\">✓ API key configurata</small>";
  } else {
    html += "<small style=\"color: #ff6b6b; display: block; margin-top: 5px;\">⚠ Nessuna API key configurata - Inseriscila per ricevere i dati meteo</small>";
  }
  html += "<small style=\"color: #888; display: block; margin-top: 3px;\">Ottieni la tua API key gratuita su: <a href=\"https://openweathermap.org/api\" target=\"_blank\" style=\"color: #3498db;\">openweathermap.org/api</a></small>";
  html += "</div>";
  html += "</div>";

  html += "<button type=\"submit\">💾 SALVA IMPOSTAZIONI</button>";
  html += "</form>";
  
  html += "</div>";
  html += "</div>";
  
  html += "<script>";
  html += "var cityManual = document.getElementById('city_manual');";
  html += "var citySelect = document.getElementById('city_select');";
  html += "citySelect.addEventListener('change', function() {";
  html += "  if (this.value !== '') { cityManual.value = ''; }";
  html += "});";
  html += "cityManual.addEventListener('input', function() {";
  html += "  if (this.value.trim() !== '') { citySelect.selectedIndex = 0; }";
  html += "});";
  html += "var forecastDaySelect = document.getElementById('forecastDay');";
  html += "var forecastPeriodSelect = document.getElementById('forecastPeriod');";
  html += "function updateForecastPeriod() {";
  html += "  if (forecastDaySelect.value == '-1') {";
  html += "    forecastPeriodSelect.disabled = true;";
  html += "    forecastPeriodSelect.style.opacity = '0.5';";
  html += "  } else {";
  html += "    forecastPeriodSelect.disabled = false;";
  html += "    forecastPeriodSelect.style.opacity = '1';";
  html += "  }";
  html += "}";
  html += "forecastDaySelect.addEventListener('change', updateForecastPeriod);";
  html += "updateForecastPeriod();";
  html += "</script>";
  // ========== CRÉDITS ==========
  html += "<div style=\"margin:15px 0;padding:15px;background:linear-gradient(135deg, #667eea 0%, #764ba2 100%);text-align:center;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)\">";
  html += "<p style=\"margin:0 0 10px 0;color:white;font-size:15px;font-weight:bold;text-shadow:0 2px 4px rgba(0,0,0,0.2)\">";
  html += "⭐ Progetto originale by";
  html += "</p>";
  html += "<p style=\"margin:0 0 10px 0;color:rgba(255,255,255,0.95);font-size:14px\">";
  html += "<strong>Davide GATTI</strong> - Survival Hacking, ";
  html += "<strong>Roberto Proli, </strong>";
  html += "<strong>Marco Prunca, </strong>";
  html += "<strong>Salvatore MatixVision, </strong>";
  html += "<strong>Gigi Soft</strong>";
  html += "</p>";
  html += "<a href=\"https://github.com/SurvivalHacking/MeteovisionNano\" target=\"_blank\" style=\"display:inline-block;padding:8px 16px;background:white;color:#667eea;border-radius:20px;text-decoration:none;font-weight:bold;font-size:13px;box-shadow:0 2px 4px rgba(0,0,0,0.2);transition:transform 0.2s\" onmouseover=\"this.style.transform='scale(1.05)'\" onmouseout=\"this.style.transform='scale(1)'\">";
  html += "🔗 GitHub";
  html += "</a>";
  html += "</div>";
  // ======================================

  html += "</body></html>";
  
  // CORREZIONE: Specificato charset UTF-8 nella risposta
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSave() {
  for(uint8_t i=0; i < server.args(); i++ ){
    Serial.print("Arg ");
    Serial.print(server.argName(i));
    Serial.print("=");
    Serial.println(server.arg(i));
  }

  if (server.hasArg("brightness")) {
    savedNeopixelBrightness = server.arg("brightness").toInt();
    pixels.setBrightness(savedNeopixelBrightness);
    pixels.show();
  }

  nightModeEnabled = (server.hasArg("night_mode") && server.arg("night_mode") == "on");

  bool oldWindAnimationEnabled = windAnimationEnabled;
  windAnimationEnabled = (server.hasArg("wind_anim") && server.arg("wind_anim") == "on");

  // Se l'animazione del vento viene disabilitata, ferma le animazioni in corso
  if (oldWindAnimationEnabled && !windAnimationEnabled) {
    windScanning = false;
    windAnimationActive = false;
    Serial.println("Animazione vento disabilitata - Fermando animazioni in corso");
  }

  // Gestione nuova variabile animazione cambio modalità
  bool oldNeopixelAnimationEnabled = neopixelAnimationEnabled;
  neopixelAnimationEnabled = (server.hasArg("neopixel_anim") && server.arg("neopixel_anim") == "on");

  if (server.hasArg("night_start")) {
    String startTime = server.arg("night_start");
    int sep = startTime.indexOf(':');
    if (sep != -1) {
      nightStartHour = startTime.substring(0, sep).toInt();
      nightStartMinute = startTime.substring(sep + 1).toInt();
    }
  }

  if (server.hasArg("night_end")) {
    String endTime = server.arg("night_end");
    int sep = endTime.indexOf(':');
    if (sep != -1) {
      nightEndHour = endTime.substring(0, sep).toInt();
      nightEndMinute = endTime.substring(sep + 1).toInt();
    }
  }

  Serial.print("Inizio notte: ");
  Serial.print(nightStartHour);
  Serial.print(":");
  Serial.println(nightStartMinute);

  Serial.print("Fine notte: ");
  Serial.print(nightEndHour);
  Serial.print(":");
  Serial.println(nightEndMinute);

  String modes[] = {
    "Automatica", "Luce rossa", "Luce Verde", "Luce Blu", "Luce Viola", "Luce Azzurra", "Luce Gialla", "Luce Bianca", "Anello Spento", "LED Spenti", "Tutto Spento", "Modalità Notte"
  };

  if (server.hasArg("mode_select")) {
    int selectedModeInt = server.arg("mode_select").toInt();

    if (selectedModeInt >= 0 && selectedModeInt <= MAX_MODE) {
      if (selectedModeInt != Currentmode) {
        Currentmode = selectedModeInt;

        // MODIFICATO: Mostra il nome della modalità per 2 secondi
        showModeName = true;
        modeNameStartTime = millis();
        showText = true;
        DisplayOneTime = false;

        SetRingColor(Currentmode, neopixelAnimationEnabled);
        // Forza aggiornamento immediato quando si cambia modalità
        forceWeatherUpdate = true;
      }
    }
  }

  String cityFromSelector = server.arg("city_select");
  String cityFromManualInput = server.arg("city_manual");
  String cityToSave = "";

  if (!cityFromManualInput.isEmpty()) {
    cityToSave = cityFromManualInput;
  } else {
    cityToSave = cityFromSelector;
  }

  bool cityChanged = false;
  if (String(savedCity) != cityToSave) {
    strncpy(savedCity, cityToSave.c_str(), sizeof(savedCity) - 1);
    savedCity[sizeof(savedCity) - 1] = '\0';
    cityChanged = true;
    Serial.println("Città cambiata: " + String(savedCity));
  }

  int oldForecastDay = forecastDay;
  if (server.hasArg("forecastDay")) {
    forecastDay = server.arg("forecastDay").toInt();
    if (forecastDay < -1 || forecastDay > 4) forecastDay = -1; // Se non valido, default a Attuale
  }

  int oldForecastPeriod = forecastPeriod;
  if (server.hasArg("forecastPeriod")) {
    forecastPeriod = server.arg("forecastPeriod").toInt();
    if (forecastPeriod < 0 || forecastPeriod > 2) forecastPeriod = 0;
  }

  if (forecastDay != oldForecastDay || forecastPeriod != oldForecastPeriod || cityChanged) {
    Serial.println("Giorno/periodo previsione cambiato o città modificata - Forzo aggiornamento immediato");
    forceWeatherUpdate = true;
    showText = true;
    DisplayOneTime = false;
  }

  if (server.hasArg("api_key")) {
    String newApiKey = server.arg("api_key");
    newApiKey.trim(); // Rimuovi spazi iniziali/finali
    
    preferences.begin("meteo-config", false);
    String oldApiKey = preferences.getString("api_key", "");
    
    if (newApiKey.length() > 0) {
      // API key inserita - salva
      if (newApiKey != oldApiKey) {
        preferences.putString("api_key", newApiKey);
        Serial.println("✅ API key aggiornata dalla pagina web: " + newApiKey);
        
        // Forza aggiornamento meteo con la nuova API key
        forceWeatherUpdate = true;
        showText = true;
        DisplayOneTime = false;
      } else {
        Serial.println("ℹ️ API key invariata");
      }
    } else {
      // Campo vuoto - rimuovi API key
      if (oldApiKey.length() > 0) {
        preferences.remove("api_key");
        Serial.println("🗑️ API key rimossa dalla configurazione");
        
        // Imposta messaggio di errore sul display
        weatherDescription = "API Key mancante";
        temperatureCelsius = 0.0;
        humidity = 0;
        windSpeed = 0.0;
        windDirection = 0;
        weatherConditionCode = 800;
        
        showText = true;
        DisplayOneTime = false;
      } else {
        Serial.println("ℹ️ Nessuna API key da rimuovere (già assente)");
      }
    }
    preferences.end();
  }


  saveConfig();

  preferences.begin("meteo-config",false);
  bool test = preferences.getBool("nightModeEnabled",false);
  preferences.end();
  Serial.print("nightmodeenabled salvato: ");
  Serial.println(test ? "TRUE":"FALSE");

  Serial.println("Valori salvati:");
  Serial.println(" - Brightness: " + String(savedNeopixelBrightness));
  Serial.println(" - Modalità: " + String(Currentmode));
  Serial.println(" - Città: " + String(savedCity));
  Serial.println(" - Forecast Day: " + String(forecastDay) + " (-1:Attuale, 0:Oggi, 1:Domani, 2:Dopodomani, 3:Giorno4, 4:Giorno5)");
  Serial.println(" - Forecast Period: " + String(forecastPeriod));
  Serial.println(" - Night Mode: " + String(nightModeEnabled));
  Serial.println(" - Notte: " + String(nightStartHour) + ":" + String(nightStartMinute) + " -> " + String(nightEndHour) + ":" + String(nightEndMinute));
  Serial.println(" - Wind Animation: " + String(windAnimationEnabled));
  Serial.println(" - NeoPixel Animation: " + String(neopixelAnimationEnabled)); // Nuova variabile

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
  
  delay(100);
  
  if (forceWeatherUpdate) {
    Serial.println("Eseguendo aggiornamento meteo immediato...");
    fetchWeather();
    forceWeatherUpdate = false;
  }
}

String urlEncode(const String& str) {
  String encoded = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encoded += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) code0 = c - 10 + 'A';
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}