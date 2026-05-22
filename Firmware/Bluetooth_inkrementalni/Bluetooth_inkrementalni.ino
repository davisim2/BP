#include <ESP32Encoder.h>
#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth neni povoleno! Zkontrolujte nastaveni desky v Arduino IDE.
#endif

// ==========================================
// --- NASTAVENÍ BLUETOOTH A UART ---
// ==========================================
BluetoothSerial SerialBT;
const int blasterRxPin = 16;
const int blasterTxPin = 17;
HardwareSerial BlasterSerial(1);

// ==========================================
// --- PROPOJENÍ PROMĚNNÝCH Z OSTATNÍCH ZÁLOŽEK ---
// ==========================================
extern int m1_current_freq, m2_current_freq;
extern int m1_velocity_target, m2_velocity_target;

extern volatile bool magnet1Detected;
extern volatile bool magnet2Detected;

// Propojíme si objekt enkodéru, abychom ho mohli po kalibraci vynulovat
extern ESP32Encoder encoder; 

// Prototypy funkcí
void updateM1State();
void updateM2State();
float getAS5600Angle();
void printSenzoryInfo();
void zpracujPrikazBT(char cmd);
void updateMotory();
void setupSenzory();
void setupMotory();

// ==========================================
// --- STAVOVÉ PROMĚNNÉ SYSTÉMU ---
// ==========================================
bool isCalibrating = false;
bool m2_calibrated = false;

// Proměnné pro chytré polohování kolébky
bool m1_auto_active = false;
float m1_auto_target_angle = 0.0; 

void setup() {
  SerialBT.begin("ESP32_Platforma"); 
  BlasterSerial.begin(115200, SERIAL_8N1, blasterRxPin, blasterTxPin);
  delay(500); 

  setupSenzory();
  setupMotory();

  SerialBT.println("\n=============================================");
  SerialBT.println(" SYSTEM START: Vektorove ovladani (BLUETOOTH)");
  SerialBT.println("=============================================");
  SerialBT.println("M1 (Kolebka):  S (Dolů) | W (Nahoru) | K (0°) | J (45°)");
  SerialBT.println("M2 (Platforma): D (LOW +)  |  A (HIGH +)");
  SerialBT.println("X = Nouzove zastaveni / Zruseni kalibrace");
  SerialBT.println("v = Vypsat senzory | c = Start kalibrace");
}

void loop() {
  // ---------------------------------------------------------
  // 1. ZPRACOVÁNÍ ZPRÁV Z PC (PŘES BLUETOOTH)
  // ---------------------------------------------------------
  while (SerialBT.available() > 0) {
    char cmd = SerialBT.read();

    // A) PŘEPOSÍLÁNÍ DO BLASTERU 
    switch (cmd) {
      case '1': case '2': case '3': case '4':
      case 'b': case 'n': case 'm': case 'h':
      case 'l': case 'o': case 'i': case ' ':
        BlasterSerial.print(cmd);
        break;
    }

    // B) LOGIKA PŘÍKAZŮ ESP32 PŘES SWITCH-CASE
    switch (cmd) {
      case 'h': case 'H':
        SerialBT.println("----------------------------");
        SerialBT.println("Napoveda k ovladani platformy:");
        SerialBT.println("w       : M1 (Kolebka) - Hlaven nahoru (+ uhel)");
        SerialBT.println("s       : M1 (Kolebka) - Hlaven dolu (- uhel)");
        SerialBT.println("a       : M2 (Platforma) - Otaceni proti smeru hodin");
        SerialBT.println("d       : M2 (Platforma) - Otaceni po smeru hodin");
        SerialBT.println("k       : M1 - Auto-polohovani na 0° (vodorovne)");
        SerialBT.println("j       : M1 - Auto-polohovani na 45° (nahoru)");
        SerialBT.println("c       : Start kalibrace platformy i kolebky");
        SerialBT.println("v       : Vypsat stav senzoru");
        SerialBT.println("x       : Nouzove zastaveni / Zrusit kalibraci");
        SerialBT.println("h       : Zobrazit tuto napovedu");
        SerialBT.println("----------------------------");
        break;

      case 'v': case 'V':
        printSenzoryInfo();
        break;

      case 'x': case 'X':
        if (isCalibrating) {
          isCalibrating = false;
          SerialBT.println("\n>>> KALIBRACE ZRUSENA UZIVATELEM (X) <<<");
        }
        zpracujPrikazBT(cmd); 
        break;

      case 'k': case 'K':
        m1_auto_active = true;
        m1_auto_target_angle = 0.0; 
        SerialBT.println("\n>>> AUTO-POLOHOVANI: M1 jede na 0° (Vodorovne) <<<");
        break;

      case 'j': case 'J':
        m1_auto_active = true;
        m1_auto_target_angle = 46.0; 
        SerialBT.println("\n>>> AUTO-POLOHOVANI: M1 jede na 45° (Max nahoru) <<<");
        break;

      case 'c': case 'C':
        isCalibrating = true;
        m2_calibrated = false;
        m2_velocity_target = 3000; 
        updateM2State();
        m1_auto_active = true;
        m1_auto_target_angle = 0.0; 
        SerialBT.println("\n>>> START KALIBRACE (Zastaveni klavesou 'x') <<<");
        break;

      default:
        // Ostatní znaky (WASD atd.) zpracuje funkce v motory.ino
        if (cmd != '\n' && cmd != '\r' && cmd != ' ') {
          zpracujPrikazBT(cmd);
        }
        break;
    }
  }

  // ---------------------------------------------------------
  // 2. ZPRACOVÁNÍ ZPRÁV Z BLASTERU
  // ---------------------------------------------------------
  while (BlasterSerial.available() > 0) {
    SerialBT.print((char)BlasterSerial.read()); 
  }

  // ---------------------------------------------------------
  // 3. BEZPEČNOSTNÍ LIMIT ÚHLU (M1)
  // ---------------------------------------------------------
  float current_angle = getAS5600Angle();
  
  bool trying_to_go_down = (m1_velocity_target > 0);
  bool trying_to_go_up   = (m1_velocity_target < 0);
  static unsigned long limit_timer = 0;
  bool is_hitting_limit = false;

  if (current_angle <= -37.0 && trying_to_go_down) {
    is_hitting_limit = true;
  } 
  else if (current_angle >= 47.0 && trying_to_go_up) {
    is_hitting_limit = true;
  }

  if (is_hitting_limit) {
    if (limit_timer == 0) limit_timer = millis(); 
    
    if (millis() - limit_timer >= 50) { 
      if (m1_velocity_target != 0) { 
        m1_velocity_target = 0; 
        updateM1State();
        m1_auto_active = false; 
        
        if (current_angle <= -37.0) {
          SerialBT.println("\n!!! NOUZOVY LIMIT -37° DOSAZEN - ZASTAVUJI MOTOR !!!");
          SerialBT.println("Vyjedte zpet stiskem klavesy 'w' (nahoru).");
        } else {
          SerialBT.println("\n!!! NOUZOVY LIMIT +47° DOSAZEN - ZASTAVUJI MOTOR !!!");
          SerialBT.println("Vyjedte zpet stiskem klavesy 's' (dolu).");
        }
      }
    }
  } else {
    limit_timer = 0;
  }

  // ---------------------------------------------------------
  // 4. CHYTRÉ POLOHOVÁNÍ M1 
  // ---------------------------------------------------------
  if (m1_auto_active) {
    float diff = current_angle - m1_auto_target_angle;

    if (abs(diff) <= 0.3) {
      if (m1_velocity_target != 0) {
        m1_velocity_target = 0;
        updateM1State();
        m1_auto_active = false;
        
        if (isCalibrating) {
          SerialBT.println(">>> M1 (Kolebka) v pozici 0° -> Kalibrace OK.");
        } else {
          float display_angle = (m1_auto_target_angle == 46.0) ? 45.0 : m1_auto_target_angle;
          SerialBT.printf(">>> M1 v cilove pozici (%.1f°).\n", display_angle);
        }
      }
    } else {
      int desired_vel = (current_angle > m1_auto_target_angle) ? 500 : -500;
      
      if (m1_velocity_target != desired_vel) {
        m1_velocity_target = desired_vel;
        updateM1State();
      }
    }
  }

  // ---------------------------------------------------------
  // 5. OBSLUHA HALLOVÝCH SENZORŮ (Platforma M2)
  // ---------------------------------------------------------
  if (magnet1Detected) {
    SerialBT.println("\n>>> DETEKCE: Senzor 1 (IO_32) zaznamenal magnet! <<<");
    if (isCalibrating && !m2_calibrated) {
      m2_velocity_target = 0; 
      updateM2State();
      m2_calibrated = true;
      SerialBT.println(">>> M2 (Platforma) kalibrace OK.");
    }
    magnet1Detected = false; 
  }

  if (magnet2Detected) {
    SerialBT.println("\n>>> DETEKCE: Senzor 2 (IO_34) zaznamenal magnet! <<<");
    magnet2Detected = false; 
  }

  // ---------------------------------------------------------
  // 6. VYHODNOCENÍ CELKOVÉHO KONCE KALIBRACE
  // ---------------------------------------------------------
  if (isCalibrating && !m1_auto_active && m2_calibrated && m1_current_freq == 0 && m2_current_freq == 0) {
    isCalibrating = false;
    encoder.clearCount(); // Vynulování lineárního senzoru!
    SerialBT.println("\n>>> KALIBRACE KOMPLETNE DOKONCENA <<<");
    SerialBT.println(">>> Hodnota linearniho senzoru (AS5304A) vynulovana.");
  }

  updateMotory(); 
}