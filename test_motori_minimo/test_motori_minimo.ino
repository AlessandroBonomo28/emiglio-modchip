/*
 * =========================================================================
 *  Emiglio modchip - firmware motori, alimentazione VM = 12 V
 *  main.cpp per PlatformIO
 * =========================================================================
 *
 *  PERCHE' IL CLAMP AL 50%
 *  I motori di Emiglio sono da 6 V, VM e' a 12 V. Con duty 128/255 la
 *  tensione media ai motori e' 6 V: l'induttanza degli avvolgimenti livella
 *  la corrente, quindi il motore lavora come se fosse alimentato a 6 V in
 *  continua. Alzare il duty oltre 128 significa sovralimentare i motori e
 *  superare gli 1,2 A continui che il TB6612 regge per canale.
 *
 *  DUTY_MAX e' l'unica difesa che hai: non esiste un limitatore hardware.
 *  Per questo tutte le scritture PWM passano da una sola funzione,
 *  pwmWrite(). Non chiamare mai ledcWrite() direttamente da altrove.
 *
 *  PINOUT
 *    PWMA 13   AIN1 27   AIN2 14
 *    PWMB 21   BIN1 32   BIN2 33
 *    STBY 19   IR   35
 *
 *  COMANDI IR (indirizzo 0x07)
 *    SU 0x60 avanti | GIU 0x61 indietro | DX 0x62 destra | SX 0x65 sinistra
 *    Stesso tasto della manovra in corso = HALT.
 *
 *  COMANDI SERIALI (per provare senza telecomando)
 *    w/s/d/a  come SU/GIU/DX/SX
 *    x stop | i stato | + / - duty | ? aiuto
 *
 *  platformio.ini
 *    [env:esp32dev]
 *    platform = espressif32
 *    board = esp32dev
 *    framework = arduino
 *    monitor_speed = 115200
 *    board_build.f_cpu = 240000000L
 *    lib_deps = z3t0/IRremote@^4.4.1
 */

#include <Arduino.h>
#include <IRremote.hpp>
#include <esp_system.h>

// ======================================================================
//   SICUREZZA - la parte da non toccare
// ======================================================================

// 128/255 = 50% = 6 V medi con VM a 12 V.
// Se un giorno torni a un pacco da 6 V, questo diventa 255.
static const uint8_t DUTY_MAX = 128;

// Velocita' di lavoro, sempre <= DUTY_MAX
static const uint8_t SPEED_MARCIA = 110;
static const uint8_t SPEED_GIRO   = 100;

// Rampa di spunto: evita il picco di corrente allo strappo iniziale
static const uint8_t  RAMP_STEPS = 8;
static const uint16_t RAMP_MS    = 20;

// Autospegnimento di sicurezza. 0 = disabilitato (marcia indefinita, come da
// specifica). Se lo metti a 30000 e Emiglio si impunta contro un muro senza
// che tu te ne accorga, dopo 30 s si ferma invece di restare in stallo.
static const uint32_t MAX_RUN_MS = 0;

// ======================================================================
//   PIN
// ======================================================================
static const int PIN_STBY = 19;
static const int PIN_PWMA = 13;
static const int PIN_AIN1 = 27;
static const int PIN_AIN2 = 14;
static const int PIN_PWMB = 21;
static const int PIN_BIN1 = 32;
static const int PIN_BIN2 = 33;

static const int IR_RECEIVE_PIN = 35;
static const uint8_t REMOTE_ADDRESS = 0x07;

static const uint8_t CMD_SU  = 0x60;
static const uint8_t CMD_GIU = 0x61;
static const uint8_t CMD_DX  = 0x62;
static const uint8_t CMD_SX  = 0x65;

// ======================================================================
//   CONFIG PWM
// ======================================================================
static const uint32_t PWM_FREQ = 20000;   // sopra la banda udibile
static const uint8_t  PWM_RES  = 8;

// Se un motore gira al contrario, cambia qui invece di rifare il cablaggio
static const bool INVERTI_A = false;
static const bool INVERTI_B = false;

// ======================================================================
//   STATO
// ======================================================================
enum Stato { STOP, AVANTI, INDIETRO, DESTRA, SINISTRA };

static Stato    stato      = STOP;
static uint8_t  dutyMarcia = SPEED_MARCIA;
static uint8_t  dutyGiro   = SPEED_GIRO;
static uint32_t tStato     = 0;
static uint32_t tUltimoIR  = 0;
static uint32_t tBeat      = 0;
static const uint32_t MIN_GAP_MS = 150;

// ======================================================================
//   PROTOTIPI
// ======================================================================
static inline uint8_t clampDuty(uint16_t d);
static void pwmWrite(int pin, uint16_t d);
static void dirA(int d);
static void dirB(int d);
static void coast();
static void applica(Stato s);
static void comando(Stato richiesto);
static const char *nomeStato(Stato s);
static void aiuto();
static void resetReason();
static void serialeIn(char c);

// ======================================================================
//   PWM - UNICO PUNTO DI ACCESSO
// ======================================================================
static inline uint8_t clampDuty(uint16_t d) {
  return (d > DUTY_MAX) ? DUTY_MAX : (uint8_t)d;
}

// L'unica funzione autorizzata a scrivere sul PWM. Se estendi questo
// firmware, passa da qui: e' il punto in cui il limite viene imposto.
static void pwmWrite(int pin, uint16_t d) {
  ledcWrite(pin, clampDuty(d));
}

// ======================================================================
//   DIREZIONI
// ======================================================================
static void dirA(int d) {
  if (INVERTI_A) d = -d;
  digitalWrite(PIN_AIN1, d > 0 ? HIGH : LOW);
  digitalWrite(PIN_AIN2, d < 0 ? HIGH : LOW);
}

static void dirB(int d) {
  if (INVERTI_B) d = -d;
  digitalWrite(PIN_BIN1, d > 0 ? HIGH : LOW);
  digitalWrite(PIN_BIN2, d < 0 ? HIGH : LOW);
}

// Ruota libera. Non tocca STBY: il driver resta abilitato.
static void coast() {
  pwmWrite(PIN_PWMA, 0);
  pwmWrite(PIN_PWMB, 0);
  digitalWrite(PIN_AIN1, LOW);
  digitalWrite(PIN_AIN2, LOW);
  digitalWrite(PIN_BIN1, LOW);
  digitalWrite(PIN_BIN2, LOW);
}

// ======================================================================
//   MANOVRE
// ======================================================================
static void applica(Stato s) {
  stato  = s;
  tStato = millis();

  if (s == STOP) {
    coast();
    Serial.println("  -> HALT");
    return;
  }

  int a = 0, b = 0;
  uint8_t target = dutyMarcia;

  switch (s) {
    case AVANTI:   a = +1; b = +1; target = dutyMarcia; break;
    case INDIETRO: a = -1; b = -1; target = dutyMarcia; break;
    case DESTRA:   a = +1; b = -1; target = dutyGiro;   break;
    case SINISTRA: a = -1; b = +1; target = dutyGiro;   break;
    default: return;
  }

  target = clampDuty(target);

  coast();
  dirA(a);
  dirB(b);

  // Rampa invece del gradino secco
  for (uint8_t i = 1; i <= RAMP_STEPS; i++) {
    uint16_t d = (uint16_t)target * i / RAMP_STEPS;
    pwmWrite(PIN_PWMA, d);
    pwmWrite(PIN_PWMB, d);
    delay(RAMP_MS);
  }

  Serial.printf("  -> %s (duty %u/255, max %u)\n",
                nomeStato(s), target, DUTY_MAX);
}

// Stesso tasto della manovra in corso = halt
static void comando(Stato richiesto) {
  applica(richiesto == stato ? STOP : richiesto);
}

static const char *nomeStato(Stato s) {
  switch (s) {
    case AVANTI:   return "AVANTI";
    case INDIETRO: return "INDIETRO";
    case DESTRA:   return "DESTRA";
    case SINISTRA: return "SINISTRA";
    default:       return "STOP";
  }
}

// ======================================================================
//   SERIALE
// ======================================================================
static void aiuto() {
  Serial.println();
  Serial.println("--------------------------------------------------");
  Serial.println(" w avanti   s indietro   d destra   a sinistra");
  Serial.println(" x stop     i stato      + / - duty     ? aiuto");
  Serial.printf (" duty marcia %u | giro %u | LIMITE %u\n",
                 dutyMarcia, dutyGiro, DUTY_MAX);
  Serial.println("--------------------------------------------------");
}

static void serialeIn(char c) {
  switch (c) {
    case 'w': case 'W': comando(AVANTI);   break;
    case 's': case 'S': comando(INDIETRO); break;
    case 'd': case 'D': comando(DESTRA);   break;
    case 'a': case 'A': comando(SINISTRA); break;
    case 'x': case 'X': applica(STOP);     break;

    case '+':
      dutyMarcia = clampDuty((uint16_t)dutyMarcia + 10);
      dutyGiro   = clampDuty((uint16_t)dutyGiro + 10);
      Serial.printf("  duty marcia %u, giro %u", dutyMarcia, dutyGiro);
      if (dutyMarcia == DUTY_MAX) Serial.print("   [LIMITE RAGGIUNTO]");
      Serial.println();
      if (stato != STOP) applica(stato);
      break;

    case '-':
      dutyMarcia = (dutyMarcia > 50) ? dutyMarcia - 10 : 40;
      dutyGiro   = (dutyGiro   > 50) ? dutyGiro   - 10 : 40;
      Serial.printf("  duty marcia %u, giro %u\n", dutyMarcia, dutyGiro);
      if (stato != STOP) applica(stato);
      break;

    case 'i': case 'I':
      Serial.printf("  stato %s | duty %u/%u | da %lu ms | heap %u\n",
                    nomeStato(stato), dutyMarcia, DUTY_MAX,
                    (unsigned long)(millis() - tStato), ESP.getFreeHeap());
      break;

    case '?': aiuto(); break;
    default: break;
  }
}

// ======================================================================
//   RESET REASON
// ======================================================================
static void resetReason() {
  esp_reset_reason_t r = esp_reset_reason();
  Serial.print("Ultimo reset: ");
  switch (r) {
    case ESP_RST_POWERON: Serial.println("power-on"); break;
    case ESP_RST_SW:      Serial.println("software (upload)"); break;
    case ESP_RST_EXT:     Serial.println("pulsante reset"); break;
    case ESP_RST_BROWNOUT:
      Serial.println(">>> BROWNOUT <<<");
      Serial.println("  Tensione crollata. Se accade allo spunto dei motori:");
      Serial.println("  manca capacita' di bulk, o C4 non e' a posto.");
      break;
    case ESP_RST_PANIC:    Serial.println(">>> PANIC <<<"); break;
    case ESP_RST_TASK_WDT: Serial.println(">>> TASK WATCHDOG <<<"); break;
    default: Serial.printf("codice %d\n", (int)r); break;
  }
}

// ======================================================================
//   SETUP
// ======================================================================
void setup() {
  Serial.begin(115200);
  delay(600);

  Serial.println("\n\n=== Emiglio modchip - motori (VM 12 V, duty max 50%) ===");
  resetReason();

  // 1. Direzioni a livello noto
  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
  pinMode(PIN_BIN1, OUTPUT);
  pinMode(PIN_BIN2, OUTPUT);
  digitalWrite(PIN_AIN1, LOW);
  digitalWrite(PIN_AIN2, LOW);
  digitalWrite(PIN_BIN1, LOW);
  digitalWrite(PIN_BIN2, LOW);

  // 2. STBY basso: driver muto durante l'inizializzazione. R10 (10k a GND)
  //    lo tiene basso anche prima che il codice giri.
  pinMode(PIN_STBY, OUTPUT);
  digitalWrite(PIN_STBY, LOW);

  // 3. PWM a zero
  ledcAttach(PIN_PWMA, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_PWMB, PWM_FREQ, PWM_RES);
  pwmWrite(PIN_PWMA, 0);
  pwmWrite(PIN_PWMB, 0);

  // 4. Solo adesso abilito il driver
  delay(20);
  digitalWrite(PIN_STBY, HIGH);
  Serial.println("- TB6612 abilitato, motori fermi");

  // 5. IR. DISABLE_LED_FEEDBACK obbligatorio: il feedback userebbe GPIO2,
  //    che su questa scheda e' il DC dell'LCD ST7735.
  IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);
  Serial.printf("- IR su GPIO%d, indirizzo 0x%02X\n",
                IR_RECEIVE_PIN, REMOTE_ADDRESS);

  Serial.printf("- LIMITE DUTY %u/255 (%u%%) = 6 V medi con VM a 12 V\n",
                DUTY_MAX, (unsigned)((DUTY_MAX * 100) / 255));
  aiuto();

  tStato = millis();
}

// ======================================================================
//   LOOP
// ======================================================================
void loop() {
  // --- seriale
  while (Serial.available()) serialeIn((char)Serial.read());

  // --- IR
  if (IrReceiver.decode()) {
    auto &d = IrReceiver.decodedIRData;
    bool ok = true;

    if (d.protocol == UNKNOWN)             ok = false;  // rumore
    if (d.flags & IRDATA_FLAGS_IS_REPEAT)  ok = false;  // tasto tenuto premuto
    if (d.address != REMOTE_ADDRESS)       ok = false;  // altro telecomando
    if (millis() - tUltimoIR < MIN_GAP_MS) ok = false;  // troppo ravvicinato

    if (ok) {
      tUltimoIR = millis();
      Serial.printf("IR 0x%02X", d.command);

      switch (d.command) {
        case CMD_SU:  comando(AVANTI);   break;
        case CMD_GIU: comando(INDIETRO); break;
        case CMD_DX:  comando(DESTRA);   break;
        case CMD_SX:  comando(SINISTRA); break;
        default:      Serial.println("  (non mappato)"); break;
      }
    }
    IrReceiver.resume();
  }

  // --- autospegnimento di sicurezza
  if (MAX_RUN_MS > 0 && stato != STOP && millis() - tStato > MAX_RUN_MS) {
    Serial.println("  timeout di sicurezza");
    applica(STOP);
  }

  // --- battito: se riparte da 0, l'ESP32 si e' riavviato. E' alimentazione,
  //     non firmware.
  if (millis() - tBeat > 5000) {
    tBeat = millis();
    static uint32_t n = 0;
    Serial.printf("[vivo %lu] %s duty=%u/%u\n",
                  (unsigned long)n++, nomeStato(stato), dutyMarcia, DUTY_MAX);
  }

  delay(5);
}