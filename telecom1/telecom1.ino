// =========================================================
//  IR Remote -> Note Player (ESP32 + VS1838B + MAX98357A)
// =========================================================
#include <IRremote.hpp>
#include <driver/i2s.h>
#include <math.h>

// ---------- IR Receiver ----------
#define IR_RECEIVE_PIN 35

// ---------- I2S Amplifier (MAX98357A) ----------
#define I2S_WS   25
#define I2S_DOUT 22
#define I2S_BCLK 26
#define I2S_PORT I2S_NUM_0

const int   sample_rate = 44100;
const float volume_max  = 8000.0;

// ---------- Note ----------
const float DO = 261.63;
const float RE = 293.66;
const float MI = 329.63;
const float FA = 349.23;

// ---------- Mappatura pulsanti telecomando (Address 0x7) ----------
// SX  -> 0x65 -> DO
// SU  -> 0x60 -> RE
// DX  -> 0x62 -> MI
// GIU -> 0x61 -> FA
#define CMD_SX  0x65
#define CMD_SU  0x60
#define CMD_DX  0x62
#define CMD_GIU 0x61

// ---------- Anti-doppione ----------
uint32_t lastCode = 0;
unsigned long lastTime = 0;
const unsigned long DEBOUNCE_MS = 50; // ignora stesso codice entro 300ms

// =========================================================
//                     I2S SETUP
// =========================================================
void i2s_install() {
  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = sample_rate,
    .bits_per_sample = i2s_bits_per_sample_t(16),
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
}

void i2s_setpin() {
  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_set_pin(I2S_PORT, &pin_config);
}
void flushSilence() {
  const int BUFFER_SIZE = 64;
  int16_t silence[BUFFER_SIZE * 2] = {0}; // tutto zero, L e R
  size_t bytes_written = 0;

  // Riempie tutti gli 8 buffer DMA di zeri (dma_buf_count = 8)
  for (int i = 0; i < 8; i++) {
    i2s_write(I2S_PORT, silence, sizeof(silence), &bytes_written, portMAX_DELAY);
  }
}
// =========================================================
//               RIPRODUZIONE NOTA (ANTI-POP)
// =========================================================
void playTone(float frequenza, int durata_ms) {
  int campioni_totali = (durata_ms * sample_rate) / 1000;
  int fade_samples = (10 * sample_rate) / 1000;

  const int BUFFER_SIZE = 64;
  int16_t buffer[BUFFER_SIZE * 2];

  for (int i = 0; i < campioni_totali; i += BUFFER_SIZE) {

    int campioni_da_elaborare = BUFFER_SIZE;
    if (i + BUFFER_SIZE > campioni_totali) {
      campioni_da_elaborare = campioni_totali - i;
    }

    for (int j = 0; j < campioni_da_elaborare; j++) {
      int16_t sample = 0;
      int indice_reale = i + j;

      if (frequenza > 0) {
        float volume_corrente = volume_max;

        if (indice_reale < fade_samples) {
          volume_corrente = volume_max * ((float)indice_reale / fade_samples);
        } else if (indice_reale > campioni_totali - fade_samples) {
          volume_corrente = volume_max * ((float)(campioni_totali - indice_reale) / fade_samples);
        }

        sample = (int16_t)(volume_corrente * sin(2.0 * PI * frequenza * indice_reale / sample_rate));
      }

      buffer[j * 2]     = sample; // L
      buffer[j * 2 + 1] = sample; // R
    }

    size_t bytes_written = 0;
    i2s_write(I2S_PORT, buffer, campioni_da_elaborare * 4, &bytes_written, portMAX_DELAY);
  }
}

// =========================================================
//                    GESTIONE PULSANTI
// =========================================================
void gestisciComando(uint8_t command) {
  float nota = 0;
  const char* nome = nullptr;

  switch (command) {
    case CMD_SX:  nota = DO; nome = "DO"; break;
    case CMD_SU:  nota = RE; nome = "RE"; break;
    case CMD_DX:  nota = MI; nome = "MI"; break;
    case CMD_GIU: nota = FA; nome = "FA"; break;
    default:
      Serial.print("Comando non mappato: 0x");
      Serial.println(command, HEX);
      return;
  }

  Serial.printf("%s -> %s\n", nome, nome);

  i2s_start(I2S_PORT);      // riaccende l'uscita I2S
  playTone(nota, 400);
  flushSilence();           // svuota il DMA con zeri veri
  i2s_stop(I2S_PORT);       // ferma completamente il clock I2S -> silenzio garantito
}

// =========================================================
//                         SETUP
// =========================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // IR
  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
  Serial.println("IR Receiver Ready on Pin 35");

  // I2S
  i2s_install();
  i2s_setpin();
  i2s_start(I2S_PORT);

  Serial.println("Sistema pronto: premi un tasto sul telecomando");
}

// =========================================================
//                          LOOP
// =========================================================
#define REMOTE_ADDRESS 0x07  // address del tuo telecomando
unsigned long lastValidTime = 0;
const unsigned long MIN_GAP_MS = 150; // tempo minimo tra due comandi validi

void loop() {
  if (IrReceiver.decode()) {

    auto &data = IrReceiver.decodedIRData;

    // Scarta rumore / protocollo non riconosciuto
    if (data.protocol == UNKNOWN) {
      IrReceiver.resume();
      return;
    }

    // Scarta repeat
    if (data.flags & IRDATA_FLAGS_IS_REPEAT) {
      IrReceiver.resume();
      return;
    }

    // Scarta segnali da altri dispositivi
    if (data.address != REMOTE_ADDRESS) {
      IrReceiver.resume();
      return;
    }

    unsigned long now = millis();

    // Scarta se troppo vicino al comando precedente (anche se command diverso)
    if (now - lastValidTime < MIN_GAP_MS) {
      IrReceiver.resume();
      return;
    }

    uint32_t code = data.decodedRawData;
    uint8_t  command = data.command;

    if (!(code == lastCode && (now - lastTime) < DEBOUNCE_MS)) {
      Serial.print("Comando ricevuto: 0x");
      Serial.println(command, HEX);

      gestisciComando(command);

      lastCode = code;
      lastTime = now;
    }

    lastValidTime = now;
    IrReceiver.resume();
  }
}