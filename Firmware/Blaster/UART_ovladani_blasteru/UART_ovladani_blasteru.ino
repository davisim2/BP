// ===== UART1 na PA10 (RX) a PA9 (TX) =====
HardwareSerial MySerial(PA10, PA9);

// ===== PWM piny a periferie =====
const int pwmPin1   = PB5;  // TIM8_CH3N
const int pwmPin2   = PA3;  // TIM15_CH2
const int pwmPin3   = PA2;  // TIM15_CH1
const int pwmPin4   = PB8;  // TIM8_CH2
const int ledPin    = PA11; // LED dioda
const int laserPin  = PB4;  // Laser
const int magnetPin = PA15; // TIM8_CH1 (Elektromagnet)
const int buttonPin = PB0;  // Fyzické tlačítko

// ===== Analogové piny pro měření =====
const int adcCurrPin = PA0; // ADC1_IN1 (Proud - INA293)
const int adcVoltPin = PA1; // ADC1_IN2 (Napětí - Dělič)

// ===== Piny pro optické závory =====
const int gate1Pin = PA7;  // Závora 1
const int gate2Pin = PA6;  // Závora 2

// ===== Úrovně výkonu a frekvence PWM =====
uint8_t quarter       = 64;   //  25 %
uint8_t half          = 128;  //  50 %
uint8_t full          = 250;  // ~100 %
const uint32_t freq   = 1000; // 1 kHz PWM

// ===== Konstanty pro výpočet napětí a proudu =====
const float vRef = 3.3;             // Referenční napětí ADC STM32
const float vDividerRatio = 11.0;   // Dělič (10k + 1k) / 1k
const float shuntResistor = 0.001;  // Bočník 1 mOhm
const float ina293A4Gain = 200.0;   // Zisk INA293

// ===== Aktuální stavy (pro kontrolu tlačítkem) =====
uint8_t pwmVal1 = 0;
uint8_t pwmVal2 = 0;
uint8_t pwmVal3 = 0;
uint8_t pwmVal4 = 0;
bool ledState = true; // Řídí současně stav LED a Laseru

// ===== Časovače pro neblokující elmag =====
volatile bool magnetActive = false; 
uint32_t magnetStartTime = 0;
const uint32_t magnetDuration = 100; // Sepnutí
const uint32_t magnetCooldown = 100; // Rozepnutí

// ===== Proměnné pro tlačítko =====
bool buttonWasPressed = false;
bool buttonActionDone = false;
uint32_t buttonPressTime = 0;

// ===== Proměnné pro výpočet rychlosti =====
const float distance_m = 0.02; // Vzdálenost závor: 2 cm

volatile uint32_t t1 = 0;
volatile uint32_t t2 = 0;
volatile bool hit1 = false;
volatile bool hit2 = false;

// ===== Obslužné rutiny přerušení (ISR) =====
void isrGate1() {
  if (magnetActive) {
    t1 = micros();
    hit1 = true;
  }
}

void isrGate2() {
  if (magnetActive) {
    t2 = micros();
    hit2 = true;
  }
}

// ===== Pomocná funkce: Je něco zapnuté? =====
bool isSystemActive() {
  return ledState || magnetActive || (pwmVal1 > 0) || (pwmVal2 > 0) || (pwmVal3 > 0) || (pwmVal4 > 0);
}

// ===== Funkce pro výpis nápovědy =====
void printHelp() {
  MySerial.println("----------------------------");
  MySerial.println("Napoveda k ovladani blasteru:");
  MySerial.println("1,2,3,4 : Jednotlive motory na 1/2 vykonu");
  MySerial.println("b       : Vsechny motory na 1/4 vykonu");
  MySerial.println("n       : Vsechny motory na 1/2 vykonu");
  MySerial.println("m       : Vsechny motory na PLNY vykon");
  MySerial.println("o       : Vypnout vse (motory, LED, Laser, elmag)");
  MySerial.println("l       : Prepnout LED a Laser");
  MySerial.println("i       : Informace o napajeni (Napeti / Proud)");
  MySerial.println("[mezera]: Vystrel elmagu (100ms) - POUZE KDYZ BEZI VSECHNY MOTORY");
  MySerial.println("h       : Zobrazit tuto napovedu");
  MySerial.println("----------------------------");
}

// ===== Funkce pro měření napájení =====
void measurePower() {
  // Průměrování 10 měření pro lepší stabilitu výsledku
  uint32_t sumVolt = 0;
  uint32_t sumCurr = 0;
  for (int i = 0; i < 10; i++) {
    sumVolt += analogRead(adcVoltPin);
    sumCurr += analogRead(adcCurrPin);
  }
  
  float avgVoltAdc = sumVolt / 10.0;
  float avgCurrAdc = sumCurr / 10.0;

  // Převod ADC hodnot (0-4095) na napětí na pinech
  float pinVoltage = (avgVoltAdc / 4095.0) * vRef;
  float pinCurrentVoltage = (avgCurrAdc / 4095.0) * vRef;

  // Výpočet skutečných hodnot
  float finalVoltage = pinVoltage * vDividerRatio;
  float finalCurrent = pinCurrentVoltage / (shuntResistor * ina293A4Gain);

  MySerial.println("====== NAPAJENI ======");
  MySerial.print("Vstupni napeti: "); 
  MySerial.print(finalVoltage, 2); 
  MySerial.println(" V");
  
  MySerial.print("Odber proudu:   "); 
  MySerial.print(finalCurrent, 2); 
  MySerial.println(" A");
  MySerial.println("======================");
}

void setup() {
  // Nastavení pinů motorů, LED, Laseru a magnetu
  pinMode(pwmPin1, OUTPUT);
  pinMode(pwmPin2, OUTPUT);
  pinMode(pwmPin3, OUTPUT);
  pinMode(pwmPin4, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(laserPin, OUTPUT);
  pinMode(magnetPin, OUTPUT);

  // Nastavení pinu tlačítka s interním pull-up rezistorem
  pinMode(buttonPin, INPUT_PULLUP);

  // Nastavení pinů závor jako vstupní
  pinMode(gate1Pin, INPUT);
  pinMode(gate2Pin, INPUT);

  // Připojení přerušení pro senzory (RISING hrana pro invertující Schmitt)
  attachInterrupt(digitalPinToInterrupt(gate1Pin), isrGate1, RISING);
  attachInterrupt(digitalPinToInterrupt(gate2Pin), isrGate2, RISING);

  // Inicializace PWM
  analogWriteResolution(8);
  analogWriteFrequency(freq);

  // Nastavení ADC na 12 bitů (hodnoty 0 - 4095) pro vyšší přesnost
  analogReadResolution(12);

  // Výchozí stavy (Vše vypnuto, LED a Laser rozsvíceny)
  analogWrite(pwmPin1, pwmVal1);
  analogWrite(pwmPin2, pwmVal2);
  analogWrite(pwmPin3, pwmVal3);
  analogWrite(pwmPin4, pwmVal4);
  digitalWrite(ledPin, ledState);
  digitalWrite(laserPin, ledState);
  digitalWrite(magnetPin, LOW);

  MySerial.begin(115200);
  delay(500); // Startup delay
  
  MySerial.println("Blaster Ready");
}

void loop() {
  uint32_t currentMillis = millis();

  // ===== 1. Zpracování příkazů z UARTu =====
  if (MySerial.available() > 0) {
    char cmd = MySerial.read();

    switch (cmd) {
      case 'h': printHelp(); break;

      case '1': pwmVal1 = half; analogWrite(pwmPin1, pwmVal1); break;
      case '2': pwmVal2 = half; analogWrite(pwmPin2, pwmVal2); break;
      case '3': pwmVal3 = half; analogWrite(pwmPin3, pwmVal3); break;
      case '4': pwmVal4 = half; analogWrite(pwmPin4, pwmVal4); break;

      case 'b': // 1/4 výkon
        pwmVal1 = quarter; pwmVal2 = quarter; pwmVal3 = quarter; pwmVal4 = quarter;
        analogWrite(pwmPin1, pwmVal1); analogWrite(pwmPin2, pwmVal2);
        analogWrite(pwmPin3, pwmVal3); analogWrite(pwmPin4, pwmVal4); 
        break;

      case 'n': // 1/2 výkon
        pwmVal1 = half; pwmVal2 = half; pwmVal3 = half; pwmVal4 = half;
        analogWrite(pwmPin1, pwmVal1); analogWrite(pwmPin2, pwmVal2);
        analogWrite(pwmPin3, pwmVal3); analogWrite(pwmPin4, pwmVal4); 
        break;

      case 'm': // PLNY výkon
        pwmVal1 = full; pwmVal2 = full; pwmVal3 = full; pwmVal4 = full;
        analogWrite(pwmPin1, pwmVal1); analogWrite(pwmPin2, pwmVal2);
        analogWrite(pwmPin3, pwmVal3); analogWrite(pwmPin4, pwmVal4); 
        break;

      case 'l':
        ledState = !ledState;
        digitalWrite(ledPin, ledState);
        digitalWrite(laserPin, ledState);
        break;
        
      case 'i': 
        measurePower(); 
        break;

      case ' ': 
        // Kontrola: Elmag není aktivní A ZÁROVEŇ uběhl čas na vychladnutí (duration + cooldown)
        if (!magnetActive && (currentMillis - magnetStartTime >= (magnetDuration + magnetCooldown))) {
          if (pwmVal1 > 0 && pwmVal2 > 0 && pwmVal3 > 0 && pwmVal4 > 0) {
            digitalWrite(magnetPin, HIGH);
            magnetStartTime = currentMillis;
            magnetActive = true; 
            hit1 = false;
            hit2 = false;
          } else {
            MySerial.println("--- ELMAG BLOKOVAN: Nebezi vsechny motory! ---");
          }
        }
        break;

      case 'o':
        pwmVal1 = 0; pwmVal2 = 0; pwmVal3 = 0; pwmVal4 = 0;
        analogWrite(pwmPin1, pwmVal1); analogWrite(pwmPin2, pwmVal2);
        analogWrite(pwmPin3, pwmVal3); analogWrite(pwmPin4, pwmVal4);
        
        digitalWrite(ledPin, LOW);
        digitalWrite(laserPin, LOW);
        ledState = false;
        
        digitalWrite(magnetPin, LOW);
        magnetActive = false; 
        MySerial.println("--- Vypnuto pres UART ---");
        break;

      default:
        if (cmd != '\r' && cmd != '\n' && cmd > 0) {
          MySerial.print("Neznamy prikaz: ");
          MySerial.println(cmd);
        }
        break;
    }
  }

  // ===== 2. Zpracování fyzického tlačítka =====
  bool buttonIsPressed = (digitalRead(buttonPin) == LOW);

  if (buttonIsPressed && !buttonWasPressed) {
    buttonPressTime = currentMillis;
    buttonWasPressed = true;
    buttonActionDone = false;
  }

  if (buttonIsPressed && buttonWasPressed && !buttonActionDone) {
    uint32_t holdDuration = currentMillis - buttonPressTime;
    bool systemActive = isSystemActive();

    if (systemActive && holdDuration >= 1000) {
      pwmVal1 = 0; pwmVal2 = 0; pwmVal3 = 0; pwmVal4 = 0;
      analogWrite(pwmPin1, pwmVal1); analogWrite(pwmPin2, pwmVal2);
      analogWrite(pwmPin3, pwmVal3); analogWrite(pwmPin4, pwmVal4);
      
      digitalWrite(ledPin, LOW);
      digitalWrite(laserPin, LOW);
      ledState = false;
      
      digitalWrite(magnetPin, LOW);
      magnetActive = false;
      
      MySerial.println("--- TLACITKO: Vse vypnuto (1s stisk) ---");
      buttonActionDone = true; 
    }
    else if (!systemActive && holdDuration >= 3000) {
      pwmVal1 = half; pwmVal2 = half; pwmVal3 = half; pwmVal4 = half;
      analogWrite(pwmPin1, pwmVal1); analogWrite(pwmPin2, pwmVal2);
      analogWrite(pwmPin3, pwmVal3); analogWrite(pwmPin4, pwmVal4);
      
      ledState = true;
      digitalWrite(ledPin, HIGH);
      digitalWrite(laserPin, HIGH);
      
      MySerial.println("--- TLACITKO: Motory 1/2 a LED+Laser zapnuto (3s stisk) ---");
      buttonActionDone = true;
    }
  }

  if (!buttonIsPressed) {
    buttonWasPressed = false;
  }

  // ===== 3. Neblokující vypnutí elektromagnetu =====
  if (magnetActive && (currentMillis - magnetStartTime >= magnetDuration)) {
    digitalWrite(magnetPin, LOW);
    magnetActive = false; 
  }

  // ===== 4. Výpočet rychlosti průletu =====
  if (hit1 && hit2) {
    // Dočasné vypnutí přerušení pro bezpečné zkopírování asynchronních proměnných
    noInterrupts();
    uint32_t time1 = t1;
    uint32_t time2 = t2;
    hit1 = false;
    hit2 = false;
    interrupts();

    uint32_t dt_micros = 0;

    if (time2 > time1) {
      dt_micros = time2 - time1;
      
      if (dt_micros > 100 && dt_micros < 50000) { //Softwarová filtrace rušení
        float speed = (distance_m * 1000000.0) / dt_micros;
        
        MySerial.println(">> DETEKOVÁN PRŮLET Šipky <<");
        MySerial.print("Čas průletu: "); 
        MySerial.print(dt_micros);
        MySerial.print(" us, Rychlost: ");
        MySerial.print(speed, 2); 
        MySerial.println(" m/s");
      }
    } 
    else {
      dt_micros = time1 - time2; 
      // Zachycení anomálie, kdy druhá závora sepnula dříve než první
      if (dt_micros > 100 && dt_micros < 50000) {
        MySerial.println("t2 < t1 : Chyba smeru");
      }
    }
  }

  // ===== 5. Timeout měření rychlosti (0.5 sekundy) =====
  uint32_t currentMicros = micros();
  if (hit1 && !hit2 && (currentMicros - t1 > 500000)) { hit1 = false; }
  if (hit2 && !hit1 && (currentMicros - t2 > 500000)) { hit2 = false; }
}