// =====================================================================
//  Emiglio modchip - display TFT 128x160 ST7735S su CN1 (KF2510-8A)
//  Target   : ESP32-WROOM-32E (16MB)
//  Framework: Arduino / PlatformIO
//  Rev      : 1.2  - fix bordo destro + riga inferiore (offset GRAM);
//                    setColRowStart esposta via sottoclasse (e' protected
//                    nelle versioni recenti della libreria Adafruit)
//
//  ---------------------------------------------------------------------
//  MAPPATURA CN1 (KF2510-8A, pin 1 = quello con il pallino sul simbolo)
//  ---------------------------------------------------------------------
//   CN1-1  BLK  -> +3V3 tramite LCD_SWITCH   (NON pilotabile da software)
//   CN1-2  SCL  -> GPIO18   (VSPI SCK)
//   CN1-3  SDA  -> GPIO23   (VSPI MOSI)
//   CN1-4  RES  -> GPIO2
//   CN1-5  DC   -> GPIO4
//   CN1-6  CS   -> GPIO15
//   CN1-7  GND  -> GND
//   CN1-8  VCC  -> +3V3 tramite LCD_SWITCH
//
//  Nota: nello schematico CN1-1 e CN1-8 sono sullo stesso net (+3V3
//  commutato), quindi VCC e BLK sono interscambiabili elettricamente.
//  Se lo schermo resta bianco/nero fisso, scambia PIN_TFT_DC e
//  PIN_TFT_RST: sono gli unici due ruoli non deducibili dallo schematico.
// =====================================================================

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// ---------------------------------------------------------------------
//  Configurazione
// ---------------------------------------------------------------------

// Pin (vedi mappatura CN1 sopra)
#define PIN_TFT_SCLK   18
#define PIN_TFT_MOSI   23
#define PIN_TFT_RST     2
#define PIN_TFT_DC      4
#define PIN_TFT_CS     15

// Metti a 1 per mostrare la cornice di taratura al boot invece dello splash
#define LCD_BORDER_TEST 0

// Geometria del pannello visibile
static constexpr int16_t PANEL_W = 128;
static constexpr int16_t PANEL_H = 160;

// Geometria della GRAM interna dell'ST7735S: e' piu' grande del vetro.
// La finestra visibile parte da (COLSTART, ROWSTART) dentro questa memoria.
static constexpr uint16_t GRAM_W = 132;
static constexpr uint16_t GRAM_H = 162;

// Offset del pannello. INITR_BLACKTAB li mette a 0/0, ma questo modulo
// vuole 2/1: senza correzione l'ultima colonna e l'ultima riga visibili
// non vengono mai scritte e mostrano il rumore della GRAM all'accensione.
//
//  TARATURA (in rotation 1: x <- ROWSTART, y <- COLSTART)
//   - striscia sporca a DESTRA      -> ROWSTART +1
//   - striscia sporca in BASSO      -> COLSTART +1
//   - striscia comparsa a SINISTRA
//     o in ALTO                     -> hai esagerato, -1
static constexpr int8_t TFT_COLSTART = 2;
static constexpr int8_t TFT_ROWSTART = 1;

// Rotazione: 0/2 = portrait 128x160, 1/3 = landscape 160x128
static constexpr uint8_t TFT_ROTATION = 1;

static constexpr uint32_t TFT_SPI_HZ = 40000000UL;   // 40 MHz

// ---------------------------------------------------------------------
//  Oggetto display (SPI hardware VSPI)
//
//  setColRowStart() e' protected nelle versioni recenti della libreria
//  Adafruit_ST7735/ST7789 (era public nelle vecchie). Una sottoclasse
//  minimale la espone senza toccare la libreria: da dentro la gerarchia
//  di classi il metodo protetto e' perfettamente accessibile.
// ---------------------------------------------------------------------
class Panel_ST7735 : public Adafruit_ST7735 {
public:
    Panel_ST7735(int8_t cs, int8_t dc, int8_t rst)
        : Adafruit_ST7735(cs, dc, rst) {}

    // wrapper pubblico sul metodo protetto
    void applyOffsets(int8_t col, int8_t row) { setColRowStart(col, row); }
};

static Panel_ST7735 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

// ---------------------------------------------------------------------
//  Azzera l'INTERA GRAM 132x162, bordi non visibili compresi.
//  Va chiamata subito dopo initR(), quando gli offset interni della
//  libreria sono ancora 0: solo in quel momento si puo' indirizzare il
//  pixel (0,0) reale della memoria del controller. Cosi' nessun bordo
//  potra' mostrare pixel casuali, nemmeno se l'offset fosse fuori di uno.
// ---------------------------------------------------------------------
static void lcdWipeGram()
{
    tft.startWrite();
    tft.setAddrWindow(0, 0, GRAM_W, GRAM_H);
    tft.writeColor(ST77XX_BLACK, (uint32_t)GRAM_W * (uint32_t)GRAM_H);
    tft.endWrite();
}

// ---------------------------------------------------------------------
//  Init completa
// ---------------------------------------------------------------------
static void lcdBegin()
{
    // VSPI con pin espliciti; MISO e SS non usati dal pannello
    SPI.begin(PIN_TFT_SCLK, -1, PIN_TFT_MOSI, -1);

    // BLACKTAB: e' la variante con i colori corretti per questo modulo.
    // Non passare a GREENTAB per sistemare l'offset: cambierebbe anche il
    // bit RGB/BGR del MADCTL e ti ritroveresti rosso e blu invertiti.
    tft.initR(INITR_BLACKTAB);
    tft.setSPISpeed(TFT_SPI_HZ);

    // 1) pulizia totale della memoria del controller
    lcdWipeGram();

    // 2) offset reali del pannello - OBBLIGATORIO prima di setRotation(),
    //    perche' e' setRotation() a copiare colstart/rowstart negli
    //    _xstart/_ystart effettivamente usati dalle primitive di disegno.
    tft.applyOffsets(TFT_COLSTART, TFT_ROWSTART);
    tft.setRotation(TFT_ROTATION);

    tft.fillScreen(ST77XX_BLACK);
}

// ---------------------------------------------------------------------
//  Cornice di taratura: deve risultare chiusa su tutti e quattro i lati,
//  con i due pixel d'angolo colorati ben visibili.
// ---------------------------------------------------------------------
static void lcdBorderTest()
{
    tft.fillScreen(ST77XX_BLACK);
    tft.drawRect(0, 0, tft.width(), tft.height(), ST77XX_RED);
    tft.drawPixel(0, 0, ST77XX_GREEN);                                // alto-sx
    tft.drawPixel(tft.width() - 1, 0, ST77XX_CYAN);                   // alto-dx
    tft.drawPixel(0, tft.height() - 1, ST77XX_MAGENTA);               // basso-sx
    tft.drawPixel(tft.width() - 1, tft.height() - 1, ST77XX_YELLOW);  // basso-dx

    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(6, 6);
    tft.printf("col=%d row=%d rot=%d", TFT_COLSTART, TFT_ROWSTART, TFT_ROTATION);
}

// ---------------------------------------------------------------------
//  Barre colore: verifica veloce di ordine RGB e assenza di sporco ai bordi
// ---------------------------------------------------------------------
static void lcdColorBars()
{
    static const uint16_t bars[5] = { ST77XX_RED,   ST77XX_GREEN, ST77XX_BLUE,
                                      ST77XX_WHITE, ST77XX_BLACK };
    const int16_t w = tft.width() / 5;

    for (uint8_t i = 0; i < 5; ++i) {
        // l'ultima barra chiude fino al bordo, cosi' non resta una colonna
        // scoperta per via dell'arrotondamento della divisione
        const int16_t bw = (i == 4) ? (tft.width() - 4 * w) : w;
        tft.fillRect(i * w, 0, bw, tft.height(), bars[i]);
    }
    delay(700);
    tft.fillScreen(ST77XX_BLACK);
}

// ---------------------------------------------------------------------
//  Schermata di avvio
// ---------------------------------------------------------------------
static void lcdSplash()
{
    tft.setTextWrap(false);
    tft.fillScreen(ST77XX_BLACK);

    tft.fillRect(0, 0, tft.width(), 16, ST77XX_BLUE);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(4, 5);
    tft.print(F("EMIGLIO MODCHIP"));

    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(2);
    tft.setCursor(4, 28);
    tft.print(F("ONLINE"));

    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(4, 54);
    tft.printf("ST7735S %dx%d", PANEL_W, PANEL_H);
    tft.setCursor(4, 66);
    tft.printf("SCL%d SDA%d", PIN_TFT_SCLK, PIN_TFT_MOSI);
    tft.setCursor(4, 78);
    tft.printf("CS%d DC%d RES%d", PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
    tft.setCursor(4, 90);
    tft.printf("off %d/%d  %lu MHz", TFT_COLSTART, TFT_ROWSTART,
               (unsigned long)(TFT_SPI_HZ / 1000000UL));

    // riga di chiusura in basso: se il bordo inferiore e' tarato bene,
    // resta completamente visibile e non taglia
    tft.drawFastHLine(0, tft.height() - 1, tft.width(), ST77XX_BLUE);
}

// =====================================================================
void setup()
{
    Serial.begin(115200);
    delay(50);
    Serial.println(F("\n[LCD] init ST7735S su CN1 ..."));

    lcdBegin();

#if LCD_BORDER_TEST
    lcdBorderTest();
#else
    lcdColorBars();
    lcdSplash();
#endif

    Serial.printf("[LCD] ok - %dx%d, offset col=%d row=%d\n",
                  tft.width(), tft.height(), TFT_COLSTART, TFT_ROWSTART);
}

void loop()
{
    // Heartbeat non bloccante. Tenuto a 6 px dai bordi per non coprire
    // la zona critica destra/inferiore appena tarata.
    static uint32_t last = 0;
    static bool on = false;

    if (millis() - last >= 500) {
        last = millis();
        on = !on;
        tft.fillCircle(tft.width() - 10, tft.height() - 10, 4,
                       on ? ST77XX_GREEN : ST77XX_BLACK);
    }
}