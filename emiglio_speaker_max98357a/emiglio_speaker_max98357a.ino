/*
 * Emiglio Speaker - A2DP Sink -> MAX98357A
 *
 * Cablaggio (identico al tuo sketch dei toni):
 *
 *   ESP32              MAX98357A
 *   -----------------  ----------------
 *   GPIO 26  --------> BCLK
 *   GPIO 25  --------> LRC   (word select)
 *   GPIO 22  --------> DIN
 *   5V       --------> VIN   (non il 3.3V: a 3.3V hai meta' potenza)
 *   GND      --------> GND
 *                      SD    -> vedi note in fondo (DEVE stare alto)
 *                      GAIN  -> vedi note in fondo
 *   Speaker 4-8 ohm su morsetti + e -
 *
 * Differenza rispetto a prima: l'I2S lo inizializziamo NOI con i pin espliciti
 * e passiamo l'oggetto al costruttore del sink. La libreria scrive lì i campioni
 * decodificati invece di configurarsi da sola con pin di default che cambiano
 * da una versione all'altra.
 *
 * Tools -> CPU Frequency: 240MHz (WiFi/BT)
 */

#include <ESP_I2S.h>
#include "BluetoothA2DPSink.h"
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_task_wdt.h>

#define I2S_BCLK 26
#define I2S_WS   25
#define I2S_DOUT 22

I2SClass i2s;
BluetoothA2DPSink a2dp_sink(i2s);   // <-- output verso il nostro oggetto I2S

unsigned long t_last_log = 0;

void on_connection_state(esp_a2d_connection_state_t state, void *ptr) {
  Serial.printf("[BT] %s\n", a2dp_sink.to_str(state));
}

void on_audio_state(esp_a2d_audio_state_t state, void *ptr) {
  Serial.printf("[BT] Audio: %s\n", a2dp_sink.to_str(state));
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nAvvio Speaker Bluetooth Emiglio...");

  WiFi.mode(WIFI_OFF);

  // ---------------------------------------------------------------- I2S first
  // Da fare PRIMA di a2dp_sink.start()
  i2s.setPins(I2S_BCLK, I2S_WS, I2S_DOUT);
  if (!i2s.begin(I2S_MODE_STD, 44100, I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_STEREO)) {
    Serial.println("ERRORE: init I2S fallito!");
    while (true) delay(1000);
  }
  Serial.println("- I2S pronto (BCLK 26, LRC 25, DIN 22)");

  // ------------------------------------------------------------------- A2DP
  a2dp_sink.set_max_write_delay_ms(2);   // il fix del watchdog, non toccare
  a2dp_sink.set_task_core(1);
  a2dp_sink.set_on_connection_state_changed(on_connection_state);
  a2dp_sink.set_on_audio_state_changed(on_audio_state);
  a2dp_sink.set_auto_reconnect(true, 1000);

  // Volume interno 0-127. Il telefono lo sovrascrive via AVRCP (nel tuo log
  // aveva impostato 48%), ma serve come punto di partenza sensato.
  a2dp_sink.set_volume(100);

  a2dp_sink.start("Emiglio_Speaker");
  esp_bt_sleep_disable();

  esp_task_wdt_config_t wdt_cfg = {
    .timeout_ms = 10000, .idle_core_mask = 0, .trigger_panic = false
  };
  esp_task_wdt_reconfigure(&wdt_cfg);

  Serial.println("- Bluetooth avviato. Cerca 'Emiglio_Speaker'.");
}

void loop() {
  if (millis() - t_last_log > 10000) {
    t_last_log = millis();
    Serial.printf("[%4lu s] connesso=%d audio=%s vol=%d heap=%u\n",
                  millis() / 1000,
                  a2dp_sink.is_connected() ? 1 : 0,
                  a2dp_sink.to_str(a2dp_sink.get_audio_state()),
                  a2dp_sink.get_volume(),
                  ESP.getFreeHeap());
  }
  delay(200);
}

/* ===========================================================================
 * BEEP LOCALI MENTRE IL BLUETOOTH E' CONNESSO
 * ===========================================================================
 * Il bus I2S e' uno solo: se scrivi i tuoi toni mentre la libreria scrive
 * l'audio BT, i campioni si mescolano e senti sporcizia. Metti in pausa
 * l'output del sink, suona, riattivalo. Il tuo playTone() funziona identico,
 * basta sostituire i2s_write(...) con i2s.write(...).
 *
 *   void beep(float freq, int durata_ms) {
 *     bool era_attivo = a2dp_sink.is_output_active();
 *     a2dp_sink.set_output_active(false);   // il sink smette di scrivere
 *
 *     const int SR = 44100;
 *     int tot = (durata_ms * SR) / 1000;
 *     int fade = (10 * SR) / 1000;          // 10 ms anti-pop
 *     int16_t buf[64 * 2];
 *
 *     for (int i = 0; i < tot; i += 64) {
 *       int n = min(64, tot - i);
 *       for (int j = 0; j < n; j++) {
 *         int k = i + j;
 *         float vol = 8000.0;
 *         if (k < fade)            vol *= (float)k / fade;
 *         else if (k > tot - fade) vol *= (float)(tot - k) / fade;
 *         int16_t s = (int16_t)(vol * sin(2.0 * PI * freq * k / SR));
 *         buf[j * 2] = s; buf[j * 2 + 1] = s;
 *       }
 *       i2s.write((uint8_t *)buf, n * 4);
 *     }
 *     a2dp_sink.set_output_active(era_attivo);
 *   }
 *
 * Non chiamare beep() da dentro una callback della libreria: girano sul task
 * Bluetooth del core 0 e lo bloccheresti per tutta la durata della nota,
 * riportandoti esattamente al task watchdog di prima. Chiamala dal loop().
 *
 * ===========================================================================
 * SE NON SENTI NIENTE
 * ===========================================================================
 * 1. PIN SD del MAX98357A. Con SD flottante l'amplificatore e' SPENTO (ha un
 *    pull-down interno da 100k). Deve stare alto. La tensione su SD seleziona
 *    anche il canale:
 *      SD verso VDD con 1M         -> (L+R)/2, mono sommato  <-- consigliato
 *      SD verso VDD con 220k       -> solo canale destro
 *      SD > 1.4V diretto           -> solo canale sinistro
 *    Molte breakout hanno gia' un pull-up: se senti solo un canale della
 *    canzone, e' questo il motivo.
 *
 * 2. PIN GAIN. Flottante = 9 dB (ok). A massa = 12 dB, a VDD = 6 dB.
 *
 * 3. ALIMENTAZIONE a 5V su VIN, non 3.3V. E un condensatore da 470 uF vicino
 *    all'amplificatore: a volume alto assorbe a impulsi.
 *
 * 4. VOLUME. Guarda il log: se vol= resta 0 il telefono ha impostato il volume
 *    a zero via AVRCP. Alza il volume multimediale sul telefono.
 *
 * 5. PIN INVERTITI. Se senti un ronzio o rumore bianco invece della musica,
 *    hai probabilmente BCLK e LRC scambiati. Verifica col tuo sketch dei toni:
 *    se quello suona e questo no, il cablaggio e' giusto e il problema e'
 *    software; se non suona nemmeno quello, e' hardware.
 *
 * 6. SPEAKER. Il MAX98357A vuole 4-8 ohm. Uno speaker da 32 ohm (tipo cuffia)
 *    suona pianissimo.
 * =========================================================================== */
