// ==========================================
// --- NASTAVENÍ SENZORŮ ---
// ==========================================

// AS5600 (Analog)
const int analogPinAS5600 = 33; 

// AS5304A (Kvadraturní PCNT)
const int pinA = 36;
const int pinB = 39;
ESP32Encoder encoder;

// DRV5023 (Hallovy koncové senzory)
const int hall1Pin = 32; 
const int hall2Pin = 34; 

volatile bool magnet1Detected = false;
volatile bool magnet2Detected = false;

void IRAM_ATTR isrHall1() { magnet1Detected = true; }
void IRAM_ATTR isrHall2() { magnet2Detected = true; }

void setupSenzory() {
  analogReadResolution(12);
  analogSetPinAttenuation(analogPinAS5600, ADC_11db);
  
  encoder.attachFullQuad(pinA, pinB);
  encoder.clearCount();

  pinMode(hall1Pin, INPUT); 
  pinMode(hall2Pin, INPUT); 
  attachInterrupt(digitalPinToInterrupt(hall1Pin), isrHall1, FALLING);
  attachInterrupt(digitalPinToInterrupt(hall2Pin), isrHall2, FALLING);
}

// Vrací aktuální úhel z AS5600 s upraveným souřadnicovým systémem
float getAS5600Angle() {
  int samples[50];
  
  for (int i = 0; i < 50; i++) {
    samples[i] = analogRead(analogPinAS5600);
  }
  
  for (int i = 0; i < 49; i++) {
    for (int j = 0; j < 49 - i; j++) {
      if (samples[j] > samples[j + 1]) {
        int temp = samples[j];
        samples[j] = samples[j + 1];
        samples[j + 1] = temp;
      }
    }
  }
  
  long sum = 0;
  for (int i = 10; i < 40; i++) {
    sum += samples[i];
  }
  
  // Vypočítáme fyzický úhel a rovnou z něj odečteme 145 stupňů
  float absolute_angle = (sum / 30.0 / 4095.0) * 360.0;
  return absolute_angle - 145.0;
}

void printSenzoryInfo() {
  extern HardwareSerial CustomSerial; 
  
  float angleAS5600 = getAS5600Angle();
  long currentPositionAS5304A = encoder.getCount();
  float distanceAS5304A = currentPositionAS5304A * 0.025; 

  CustomSerial.print("\n=== STAV SENZORU ===\n");
  // Zobrazujeme i znaménko (např. -35.2 st. nebo +45.0 st.) pro snazší orientaci
  CustomSerial.printf("[AS5600] Relativni uhel: %+.1f st.\n", angleAS5600);
  CustomSerial.printf("[AS5304A] Vzdalenost: %.3f mm\n", distanceAS5304A);
  
  extern int m1_current_freq, m2_current_freq;
  CustomSerial.printf("M1: %d Hz | M2: %d Hz\n", m1_current_freq, m2_current_freq);
  CustomSerial.print("====================\n");
}