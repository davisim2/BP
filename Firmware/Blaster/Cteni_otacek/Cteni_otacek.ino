// ===== UART1 na PA10 (RX) a PA9 (TX) =====
HardwareSerial MySerial(PA10, PA9);

// ===== PWM piny =====
const int pwmPin1 = PB5;    // TIM8_CH3N
const int pwmPin2 = PA3;    // TIM15_CH2
const int pwmPin3 = PA2;    // TIM15_CH1
const int pwmPin4 = PB8;    // TIM8_CH2

// ===== Ostatní piny =====
const int ledPin    = PA11;
const int magnetPin = PA15; // TIM8_CH1

// ===== Senzory QRD1114 (TIM_ETR) =====
const int sens1Pin = PA12;  // TIM1_ETR
const int sens2Pin = PA5;   // TIM2_ETR
const int sens3Pin = PB3;   // TIM3_ETR
const int sens4Pin = PA8;   // TIM4_ETR

// ===== Proměnné pro počítání pulzů (volatile pro přístup z ISR) =====
volatile uint32_t pulseCount1 = 0;
volatile uint32_t pulseCount2 = 0;
volatile uint32_t pulseCount3 = 0;
volatile uint32_t pulseCount4 = 0;

// ===== Proměnné pro výpočet RPM =====
uint32_t lastRpmTime = 0;
const uint32_t rpmInterval = 250; // Interval výpočtu v ms (250 ms = 4x za vteřinu)

uint32_t lastPulseCount1 = 0;
uint32_t lastPulseCount2 = 0;
uint32_t lastPulseCount3 = 0;
uint32_t lastPulseCount4 = 0;

uint32_t currentRpm1 = 0;
uint32_t currentRpm2 = 0;
uint32_t currentRpm3 = 0;
uint32_t currentRpm4 = 0;

// ===== Úrovně výkonu =====
uint8_t quarter       = 64;   //  25 %
uint8_t half          = 128;  //  50 %
uint8_t threeQuarters = 192;  //  75 %
uint8_t full          = 250;  // ~100 %

bool ledState    = true;
bool magnetState = false;

// ===== Obslužné rutiny přerušení (ISR) =====
void isr1() { pulseCount1++; }
void isr2() { pulseCount2++; }
void isr3() { pulseCount3++; }
void isr4() { pulseCount4++; }

void setup() {
  const uint32_t freq = 1000;  // 1 kHz PWM

  pinMode(pwmPin1, OUTPUT);
  pinMode(pwmPin2, OUTPUT);
  pinMode(pwmPin3, OUTPUT);
  pinMode(pwmPin4, OUTPUT);

  pinMode(ledPin, OUTPUT);
  pinMode(magnetPin, OUTPUT);

  // Nastavení vstupů pro senzory
  // Máte zapojený 10k rezistor na zem, takže RISING hrana znamená detekci odrazu
  pinMode(sens1Pin, INPUT);
  pinMode(sens2Pin, INPUT);
  pinMode(sens3Pin, INPUT);
  pinMode(sens4Pin, INPUT);

  // Připojení přerušení
  attachInterrupt(digitalPinToInterrupt(sens1Pin), isr1, RISING);
  attachInterrupt(digitalPinToInterrupt(sens2Pin), isr2, RISING);
  attachInterrupt(digitalPinToInterrupt(sens3Pin), isr3, RISING);
  attachInterrupt(digitalPinToInterrupt(sens4Pin), isr4, RISING);

  analogWriteResolution(8);
  analogWriteFrequency(freq);

  // Vše vypnuto
  analogWrite(pwmPin1, 0);
  analogWrite(pwmPin2, 0);
  analogWrite(pwmPin3, 0);
  analogWrite(pwmPin4, 0);

  digitalWrite(ledPin, HIGH);
  digitalWrite(magnetPin, LOW);

  MySerial.begin(115200);
  delay(500);
  
  MySerial.println("STM32 Ready (RPM Mereni)");
  MySerial.println("Ovladani: 1,2,3,4 (1/2 vykonu jednotlivce)");
  MySerial.println("Hromadne: z (1/4), b (1/2), n (3/4), m (plny)");
  MySerial.println("v : Vypsat RPM");
  MySerial.println("o : Vypnout vse");
}

void loop() {
  uint32_t currentMillis = millis();

  // ===== Periodický výpočet RPM na pozadí =====
  if (currentMillis - lastRpmTime >= rpmInterval) {
    uint32_t dt = currentMillis - lastRpmTime;

    // Dočasné zakázání přerušení pro bezpečné přečtení 32bitových proměnných
    noInterrupts();
    uint32_t pc1 = pulseCount1;
    uint32_t pc2 = pulseCount2;
    uint32_t pc3 = pulseCount3;
    uint32_t pc4 = pulseCount4;
    interrupts(); // Okamžité opětovné povolení

    // Výpočet: (Přírůstek pulzů * 60000 ms) / časový rozdíl
    currentRpm1 = ((pc1 - lastPulseCount1) * 60000UL) / dt;
    currentRpm2 = ((pc2 - lastPulseCount2) * 60000UL) / dt;
    currentRpm3 = ((pc3 - lastPulseCount3) * 60000UL) / dt;
    currentRpm4 = ((pc4 - lastPulseCount4) * 60000UL) / dt;

    // Uložení stavů pro další cyklus
    lastPulseCount1 = pc1;
    lastPulseCount2 = pc2;
    lastPulseCount3 = pc3;
    lastPulseCount4 = pc4;
    lastRpmTime = currentMillis;
  }

  // ===== Zpracování příkazů z UARTu =====
  if (MySerial.available() > 0) {
    char cmd = MySerial.read();

    switch (cmd) {
      // ===== VÝPIS RPM =====
      case 'v':
        MySerial.println("--- Aktualni RPM ---");
        MySerial.print("M1 (PB5):  "); MySerial.println(currentRpm1);
        MySerial.print("M2 (PA3):  "); MySerial.println(currentRpm2);
        MySerial.print("M3 (PA2):  "); MySerial.println(currentRpm3);
        MySerial.print("M4 (PB8):  "); MySerial.println(currentRpm4);
        MySerial.println("--------------------");
        break;

      // ===== INDIVIDUÁLNÍ výkon (1/2) =====
      case '1': analogWrite(pwmPin1, half); break;
      case '2': analogWrite(pwmPin2, half); break;
      case '3': analogWrite(pwmPin3, half); break;
      case '4': analogWrite(pwmPin4, half); break;

      // ===== HROMADNÝ výkon =====
      case 'z': // 1/4
        analogWrite(pwmPin1, quarter); analogWrite(pwmPin2, quarter);
        analogWrite(pwmPin3, quarter); analogWrite(pwmPin4, quarter); 
        break;
        
      case 'b': // 1/2
        analogWrite(pwmPin1, half); analogWrite(pwmPin2, half);
        analogWrite(pwmPin3, half); analogWrite(pwmPin4, half); 
        break;

      case 'n': // 3/4
        analogWrite(pwmPin1, threeQuarters); analogWrite(pwmPin2, threeQuarters);
        analogWrite(pwmPin3, threeQuarters); analogWrite(pwmPin4, threeQuarters); 
        break;

      case 'm': // PLNY
        analogWrite(pwmPin1, full); analogWrite(pwmPin2, full);
        analogWrite(pwmPin3, full); analogWrite(pwmPin4, full); 
        break;

      // ===== LED toggle =====
      case 'l':
        ledState = !ledState;
        digitalWrite(ledPin, ledState);
        break;

      // ===== Elektromagnet toggle =====
      case ' ':
      case 'e':
        magnetState = !magnetState;
        digitalWrite(magnetPin, magnetState);
        delay(500);
        magnetState = !magnetState;
        digitalWrite(magnetPin, magnetState);
        break;

      // ===== Vypnout vše =====
      case 'o':
        analogWrite(pwmPin1, 0);
        analogWrite(pwmPin2, 0);
        analogWrite(pwmPin3, 0);
        analogWrite(pwmPin4, 0);
        digitalWrite(ledPin, LOW);
        digitalWrite(magnetPin, LOW);
        ledState = false;
        magnetState = false;
        break;

      default:
        // Ignoruj prázdné znaky (CR/LF z terminálu)
        if (cmd != '\r' && cmd != '\n' && cmd > 0) {
          MySerial.print("Neznamy prikaz: ");
          MySerial.println(cmd);
        }
        break;
    }
  }
}