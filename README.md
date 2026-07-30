# Emiglio modchip

![Emiglio modchip v1.0 con la schermata sponsor PCBWay sul display ST7735S](sponsor.jpg)

Scheda embedded per riportare in vita l'Emiglio, il robot giocattolo degli anni '90.
ESP32-WROOM-32E, driver motori TB6612FNG, amplificatore I2S MAX98357A, display TFT
ST7735S e ricevitore IR: tutto su un singolo PCB.

---

## Sponsor: PCBWay

<p align="center">
  <a href="https://www.pcbway.com/">
    <img src="052_PCBWAY-1250535807.jpg" alt="PCBWay" width="380">
  </a>
</p>

**Proud to announce a new sponsorship by PCBWay!**

I PCB di questo progetto sono prodotti da [**PCBWay**](https://www.pcbway.com/):
prototipi di alta qualità, assemblaggio SMT, stencil e servizio di verifica DFM
gratuito prima della messa in produzione. Se vuoi ordinare la tua Emiglio modchip
o qualsiasi altra scheda, parti da qui → **<https://www.pcbway.com/>**

Lo sponsor ha anche un posto d'onore nel firmware: lo sketch
[`pcbway/pcbway.ino`](pcbway/pcbway.ino) mostra il logo animato sul display TFT
della scheda.

---

## Schematico

![Schematico Emiglio modchip](Schematic_emiglio-modchip_2026-07-28.png)

File: [`Schematic_emiglio-modchip_2026-07-28.png`](Schematic_emiglio-modchip_2026-07-28.png)
— rev. 1.0, disegnato in EasyEDA.

Blocchi presenti sulla scheda:

| Blocco | Componenti principali | Note |
|---|---|---|
| **POWER** | AMS1117-3.3 (U5) | 5 V → 3V3, LED di presenza tensione |
| **USB C TTL** | USB Type-C 3.1 16P + CH340C (U4) + UMH3N (Q2) | programmazione e seriale, auto-reset EN/IO0 |
| **MCU** | ESP32-WROOM-32E 16 MB (U6) | pulsanti BOOT / RESET, LED debug, connettore occhi e IMPUT3 |
| **Motor driver** | TB6612FNG (U2) | 2 canali (A/B), VM da batteria, STBY con pull-down 10k |
| **I2S Audio Amp** | MAX98357A (U1) | uscita speaker su HC-XH-2A-G, VDD 5 V, GAIN via R1 1M |
| **LCD display** | connettore CN1 KF2510-8A + LCD_SWITCH | pannello ST7735S 128×160, alimentato da 3V3 commutato |
| **IR Receiver** | TSOP4838 | ricezione telecomando su GPIO35 |

### Pinout usato dal firmware

| Funzione | GPIO |
|---|---|
| TFT SCLK / MOSI | 18 / 23 |
| TFT RES / DC / CS | 2 / 4 / 15 |
| I2S BCLK / LRC (WS) / DIN | 26 / 25 / 22 |
| Motori PWMA / AIN1 / AIN2 | 13 / 27 / 14 |
| Motori PWMB / BIN1 / BIN2 | 21 / 32 / 33 |
| TB6612 STBY | 19 |
| Ricevitore IR | 35 |

> Attenzione: GPIO2 è il DC dell'LCD, quindi la libreria IRremote va inizializzata
> con `DISABLE_LED_FEEDBACK` (il feedback LED userebbe proprio GPIO2).

---

## Layout PCB

![Layout del PCB Emiglio modchip](Immagine%202026-07-30%20160827.png)

Scheda a 2 strati (rosso = top, blu = bottom). Si riconoscono la posizione del
modulo ESP32 con la keep-out dell'antenna verso il bordo, il connettore `LCD SOCKET`
con l'interruttore `LCD_SWITCH`, i morsetti `BAT+ / BAT-` e `+ SPK -`, il
ricevitore `IR-RCV` sul bordo inferiore, i pulsanti `RESET` / `BOOT` /
`IMPUT1` / `IMPUT2` e il pettine `U8` con `IMPUT3`, `GND` ed `EYE1` / `EYE2` per
gli occhi del robot. Sul silkscreen ci sono anche il logo *goodman industries* e un
saluto a Johnny 5.

Foto della scheda prodotta e montata: vedi l'immagine di apertura
([`sponsor.jpg`](sponsor.jpg)) — a destra il retro con la serigrafia
*Emiglio modchip v1.0*, a sinistra il fronte popolato.

---

## Sketch presenti nel repo

Ogni cartella è uno sketch Arduino autonomo (`.ino` con lo stesso nome della
cartella), pensato per provare un sottosistema alla volta.

### [`pcbway/`](pcbway/pcbway.ino) — schermata sponsor PCBWay
Demo grafica sul TFT ST7735S: logo PCBWay renderizzato come due bitmap 1bpp
sovrapposte (wordmark verde + swoosh arancione, 148×42, 798 byte ciascuna, così si
usano i colori reali del marchio senza spendere 12 kB di bitmap RGB565).
Include intro animata — teaser a puntini, caduta del wordmark con rimbalzo
smorzato, swoosh "routato" colonna per colonna come una pista su PCB, flash del
titolo — poi scintille casuali sui pixel vuoti del logo e marquee scorrevole a
~35 fps disegnato su `GFXcanvas1` per evitare lo sfarfallio.
**Librerie:** Adafruit_GFX, Adafruit_ST7735, SPI.

### [`LcdModchip/`](LcdModchip/LcdModchip.ino) — bring-up e taratura del display
Rev 1.2. Inizializza il pannello ST7735S su CN1 e risolve il classico problema
degli offset: azzera l'intera GRAM 132×162 (bordi non visibili compresi) subito
dopo `initR()`, poi applica `COLSTART=2 / ROWSTART=1` tramite una sottoclasse che
espone il metodo protetto `setColRowStart()`. Mostra barre colore per verificare
l'ordine RGB e uno splash con i parametri correnti; con `LCD_BORDER_TEST 1`
disegna invece la cornice di taratura con i quattro pixel d'angolo colorati.
Heartbeat non bloccante nel `loop()`.
**Librerie:** Adafruit_GFX, Adafruit_ST7735, SPI.

### [`test_motori_minimo/`](test_motori_minimo/test_motori_minimo.ino) — firmware motori
Guida il TB6612FNG con telecomando IR (indirizzo `0x07`: SU/GIU/DX/SX; ripremere
lo stesso tasto della manovra in corso fa HALT) o da seriale (`w s d a`, `x` stop,
`i` stato, `+`/`-` duty, `?` aiuto). Tutte le scritture PWM passano da un'unica
funzione `pwmWrite()` che applica il clamp `DUTY_MAX` — è l'unica difesa contro la
sovralimentazione dei motori da 6 V con VM a 12 V, non esiste un limitatore
hardware. PWM a 20 kHz (fuori dalla banda udibile), rampa di spunto a 8 step per
smorzare il picco di corrente, `STBY` tenuto basso fino a fine inizializzazione,
autospegnimento opzionale (`MAX_RUN_MS`) e decodifica del reset reason con
diagnostica dedicata al brownout.
**Librerie:** IRremote.

### [`telecom1/`](telecom1/telecom1.ino) — IR → note musicali
Suona quattro note (DO/RE/MI/FA) sui tasti direzionali del telecomando. Genera i
campioni a runtime con `sin()` su I2S a 44.1 kHz e applica un fade di 10 ms in
ingresso e uscita per evitare il pop; dopo ogni nota riempie gli 8 buffer DMA di
silenzio e ferma il clock I2S, così il silenzio è davvero silenzio. Filtri anti
falso-trigger: scarta protocollo `UNKNOWN`, i repeat, gli indirizzi di altri
telecomandi e i comandi troppo ravvicinati.
**Librerie:** IRremote, driver I2S legacy dell'ESP-IDF (`driver/i2s.h`).

### [`emiglio_speaker_max98357a/`](emiglio_speaker_max98357a/emiglio_speaker_max98357a.ino) — speaker Bluetooth A2DP
Trasforma Emiglio in una cassa Bluetooth: sink A2DP con nome `Emiglio_Speaker`,
riconnessione automatica e volume gestibile via AVRCP dal telefono. L'I2S viene
inizializzato esplicitamente con i pin della scheda e l'oggetto `I2SClass` passato
al costruttore del sink, invece di lasciare che la libreria si configuri con pin di
default che cambiano tra le versioni. WiFi spento, task audio inchiodato al core 1,
`set_max_write_delay_ms(2)` e watchdog riconfigurato a 10 s senza panic. Log di
stato ogni 10 s (connessione, stato audio, volume, heap). In coda al file c'è una
guida ai guasti tipici (pin SD flottante, GAIN, alimentazione a 5 V, BCLK/LRC
invertiti, impedenza dello speaker) e uno snippet per suonare beep locali mettendo
in pausa l'output del sink.
**Librerie:** ESP32-A2DP, ESP_I2S, WiFi, esp_bt, esp_task_wdt.

---

## Dipendenze

Tutte installabili dal Library Manager dell'IDE Arduino o via `lib_deps` in
PlatformIO.

| Libreria | Autore | Repository | Usata da |
|---|---|---|---|
| Arduino core per ESP32 | Espressif | <https://github.com/espressif/arduino-esp32> | tutti gli sketch |
| Adafruit GFX Library | Adafruit | <https://github.com/adafruit/Adafruit-GFX-Library> | `pcbway`, `LcdModchip` |
| Adafruit ST7735 / ST7789 Library | Adafruit | <https://github.com/adafruit/Adafruit-ST7735-Library> | `pcbway`, `LcdModchip` |
| Adafruit BusIO | Adafruit | <https://github.com/adafruit/Adafruit_BusIO> | dipendenza di GFX/ST7735 |
| IRremote | Armin Joachimsmeyer (z3t0) | <https://github.com/Arduino-IRremote/Arduino-IRremote> | `test_motori_minimo`, `telecom1` |
| ESP32-A2DP | Phil Schatzmann | <https://github.com/pschatzmann/ESP32-A2DP> | `emiglio_speaker_max98357a` |

`SPI`, `WiFi`, `ESP_I2S`, `driver/i2s.h`, `esp_bt.h` e `esp_task_wdt.h` arrivano
già con il core ESP32, non serve installare nulla.

### PlatformIO

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.f_cpu = 240000000L
lib_deps =
    z3t0/IRremote@^4.4.1
    adafruit/Adafruit GFX Library
    adafruit/Adafruit ST7735 and ST7789 Library
    pschatzmann/ESP32-A2DP
```

## Compilazione con l'IDE Arduino

- **Board:** ESP32 Dev Module (ESP32-WROOM-32E)
- **Flash Size:** 16 MB (128 Mb)
- **CPU Frequency:** 240 MHz (WiFi/BT) — obbligatorio per lo sketch A2DP
- **Partition Scheme:** per `emiglio_speaker_max98357a` serve uno schema con spazio
  per lo stack Bluetooth (es. *Huge APP*)
- **Upload Speed:** 921600, seriale via CH340C sulla USB-C della scheda
- **Monitor:** 115200 baud
