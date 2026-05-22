#include <ESP32Encoder.h>

// ==========================================
// --- NASTAVENÍ UARTU (Platforma) ---
// ==========================================
const int rxPin = 25;
const int txPin = 26;
HardwareSerial CustomSerial(2);

// ==========================================
// --- NASTAVENÍ UARTU (Blaster) ---
// ==========================================
const int blasterRxPin = 16;
const int blasterTxPin = 17;
HardwareSerial BlasterSerial(1);

// ================================================
// --- PROPOJENÍ PROMĚNNÝCH Z OSTATNÍCH ZÁLOŽEK ---
// ================================================
extern int m1_current_freq, m2_current_freq;
extern int m1_velocity_target, m2_velocity_target;

extern volatile bool magnet1Detected;
extern volatile bool magnet2Detected;

extern ESP32Encoder encoder; 

// Prototypy funkcí
void updateM1State();
void updateM2State();
float getAS5600Angle();
void printSenzoryInfo();
void zpracujPrikazUART(char cmd);
void updateMotory();
void setupSenzory();
void setupMotory();
void printHelpMenu();

// ==========================================
// --- STAVOVÉ PROMĚNNÉ SYSTÉMU ---
// ==========================================
bool isCalibrating = false;

// KALIBRAČNÍ KONSTANTA: Přesný počet pulzů enkodéru AS5304A pro odjezd o 90°
const long M2_TICKS_PER_90_DEG = 9975; 

int m2_calib_stage = 0; 
long m2_target_encoder_ticks = 0; 

// Proměnné pro chytré polohování kolébky
bool m1_auto_active = false;
float m1_auto_target_angle = 0.0; 

void setup() {
  // Inicializace komunikace
  CustomSerial.begin(115200, SERIAL_8N1, rxPin, txPin); 
  BlasterSerial.begin(115200, SERIAL_8N1, blasterRxPin, blasterTxPin);
  delay(500); 

  setupSenzory();
  setupMotory();

  CustomSerial.println("\n=============================================");
  CustomSerial.println(" SYSTEM START: Vektorove ovladani (UART)");
  CustomSerial.println("=============================================");
  printHelpMenu(); // Zavolání nápovědy hned po startu
}

// ==========================================
// --- FUNKCE PRO VÝPIS NÁPOVĚDY ---
// ==========================================
void printHelpMenu() {
  CustomSerial.println("----------------------------");
  CustomSerial.println("Napoveda k ovladani platformy:");
  CustomSerial.println("w       : M1 (Kolebka) - Hlaven nahoru (+ uhel)");
  CustomSerial.println("s       : M1 (Kolebka) - Hlaven dolu (- uhel)");
  CustomSerial.println("a       : M2 (Platforma) - Otaceni proti smeru hodin");
  CustomSerial.println("d       : M2 (Platforma) - Otaceni po smeru hodin");
  CustomSerial.println("k       : M1 - Auto-polohovani na 0° (vodorovne)");
  CustomSerial.println("j       : M1 - Auto-polohovani na 45° (nahoru)");
  CustomSerial.println("c       : Start kalibrace platformy i kolebky");
  CustomSerial.println("v       : Vypsat stav senzoru");
  CustomSerial.println("x       : Nouzove zastaveni / Zrusit kalibraci");
  CustomSerial.println("h       : Zobrazit tuto napovedu");
  CustomSerial.println("----------------------------");
}

void loop() {
  // ---------------------------------------------------------
  // 1. ZPRACOVÁNÍ ZPRÁV Z PC (PŘES UART)
  // ---------------------------------------------------------
  while (CustomSerial.available() > 0) {
    char cmd = CustomSerial.read();

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
        printHelpMenu();
        break;

      case 'v': case 'V':
        printSenzoryInfo();
        break;

      case 'x': case 'X':
        if (isCalibrating) {
          isCalibrating = false;
          CustomSerial.println("\n>>> KALIBRACE ZRUSENA UZIVATELEM (X) <<<");
        }
        zpracujPrikazUART(cmd); 
        break;

      case 'k': case 'K':
        m1_auto_active = true;
        m1_auto_target_angle = 0.0; 
        CustomSerial.println("\n>>> AUTO-POLOHOVANI: M1 jede na 0° (Vodorovne) <<<");
        break;

      case 'j': case 'J':
        m1_auto_active = true;
        m1_auto_target_angle = 46.0; 
        CustomSerial.println("\n>>> AUTO-POLOHOVANI: M1 jede na 45° (Max nahoru) <<<");
        break;

      case 'c': case 'C':
        isCalibrating = true;
        m2_calib_stage = 0; 
        
        m2_velocity_target = 3000; 
        updateM2State();
        
        m1_auto_active = true;
        m1_auto_target_angle = 0.0; 
        CustomSerial.println("\n>>> START KALIBRACE (Zastaveni klavesou 'x') <<<");
        break;

      default:
        if (cmd != '\n' && cmd != '\r' && cmd != ' ') {
          zpracujPrikazUART(cmd);
        }
        break;
    }
  }

  // ---------------------------------------------------------
  // 2. ZPRACOVÁNÍ ZPRÁV Z BLASTERU
  // ---------------------------------------------------------
  while (BlasterSerial.available() > 0) {
    CustomSerial.print((char)BlasterSerial.read()); 
  }

  // ---------------------------------------------------------
  // 3. BEZPEČNOSTNÍ LIMIT ÚHLU NÁMĚRU (M1)
  // ---------------------------------------------------------
  float current_angle = getAS5600Angle();
  
  bool trying_to_go_down = (m1_velocity_target > 0);
  bool trying_to_go_up   = (m1_velocity_target < 0);
  
  bool is_hitting_limit = false;
  static bool limit_msg_printed = false;

  if (current_angle <= -37.0 && trying_to_go_down) {
    is_hitting_limit = true;
    if (!limit_msg_printed) {
      CustomSerial.println("\n!!! NOUZOVY LIMIT -37° DOSAZEN - ZASTAVUJI MOTOR !!!");
      limit_msg_printed = true;
    }
  } 
  else if (current_angle >= 47.0 && trying_to_go_up) {
    is_hitting_limit = true;
    if (!limit_msg_printed) {
      CustomSerial.println("\n!!! NOUZOVY LIMIT +47° DOSAZEN - ZASTAVUJI MOTOR !!!");
      limit_msg_printed = true;
    }
  }
  else {
    limit_msg_printed = false; 
  }

  if (is_hitting_limit) {
    if (m1_velocity_target != 0) { 
      m1_velocity_target = 0; 
      updateM1State();
      m1_auto_active = false; 
    }
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
          CustomSerial.println(">>> M1 (Kolebka) v pozici 0° -> Kalibrace OK.");
        } else {
          float display_angle = (m1_auto_target_angle == 46.0) ? 45.0 : m1_auto_target_angle;
          CustomSerial.printf(">>> M1 v cilove pozici (%.1f°).\n", display_angle);
        }
      }
    } else { 
      int speed_mag = (abs(diff) <= 8.0) ? 500 : 4000;
      int desired_vel = (current_angle > m1_auto_target_angle) ? speed_mag : -speed_mag;
      
      if (m1_velocity_target != desired_vel) {
        m1_velocity_target = desired_vel;
        updateM1State();
      }
    }
  }

  // ---------------------------------------------------------
  // 5. OBSLUHA HALLOVÝCH SENZORŮ A PŘESUNU PLATFORMY (M2)
  // ---------------------------------------------------------
  if (isCalibrating) {
    long current_ticks = encoder.getCount();

    // FÁZE 1: Vyhledání magnetu
    if (m2_calib_stage == 0) {
      if (magnet1Detected) {
        CustomSerial.println("\n>>> DETEKCE: Senzor 1 zachycen. Zahajuji odjezd o 90° proti smeru hodin...");
        m2_velocity_target = -8000;
        updateM2State();
        m2_target_encoder_ticks = current_ticks - M2_TICKS_PER_90_DEG;
        m2_calib_stage = 1; 
        magnet1Detected = false;
      }
      else if (magnet2Detected) {
        CustomSerial.println("\n>>> DETEKCE: Senzor 2 zachycen. Zahajuji odjezd o 90° po smeru hodin...");
        m2_velocity_target = 8000;
        updateM2State();
        m2_target_encoder_ticks = current_ticks + M2_TICKS_PER_90_DEG;
        m2_calib_stage = 1; 
        magnet2Detected = false;
      }
    }
    // FÁZE 2: Hlídání přesného odjezdu o 9975 pulzů z enkodéru
    else if (m2_calib_stage == 1) {
      bool reached_proti = (m2_velocity_target < 0 && current_ticks <= m2_target_encoder_ticks);
      bool reached_po = (m2_velocity_target > 0 && current_ticks >= m2_target_encoder_ticks);

      if (reached_proti || reached_po) {
        m2_velocity_target = 0; 
        updateM2State();
        m2_calib_stage = 2; 
        CustomSerial.println(">>> M2 dosahla pozice 90° od senzoru. Zastavuji...");
      }
    }
    
    magnet1Detected = false;
    magnet2Detected = false;
  } else {
    magnet1Detected = false;
    magnet2Detected = false;
  }

  // ---------------------------------------------------------
  // 6. VYHODNOCENÍ CELKOVÉHO KONCE KALIBRACE
  // ---------------------------------------------------------
  if (isCalibrating && !m1_auto_active && m2_calib_stage == 2 && m1_current_freq == 0 && m2_current_freq == 0) {
    isCalibrating = false;
    encoder.clearCount(); 
    CustomSerial.println("\n>>> KALIBRACE KOMPLETNE DOKONCENA <<<");
    CustomSerial.println(">>> AS5304 vynulovan.");
  }

  updateMotory(); 
}