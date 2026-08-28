// wifi_config.h
#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

// ===========================
// CONFIGURAZIONE WIFI MANUALE
// ===========================

// Imposta USE_MANUAL_WIFI su true per utilizzare le credenziali qui sotto
// Imposta su false per utilizzare il WiFiManager (portale di configurazione)
#define USE_MANUAL_WIFI false

// Le tue credenziali WiFi (da compilare se MANUALE_WIFI_USO = true)
#define WIFI_SSID "IlTuoSSID"          // Sostituisci con il nome della tua rete WiFi
#define WIFI_PASSWORD "LaTuaPassword"  // Sostituisci con la tua password WiFi

// Timeout di connessione WiFi (in millisecondi)
#define WIFI_CONNECT_TIMEOUT 30000  // 30 secondi

// Numero di tentativi di riconnessione automatica
#define WIFI_RETRY_COUNT 3

#endif


