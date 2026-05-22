// ==========================================
// --- NASTAVENÍ MOTORŮ A PINŮ ---
// ==========================================
const int M1_STEP_PIN = 5;
const int M1_DIR_PIN = 23;
const int M1_EN_PIN = 14;

const int M2_STEP_PIN = 18;
const int M2_DIR_PIN = 19;
const int M2_EN_PIN = 13;

const int MS_PIN = 27;

const int PWM_RESOLUTION = 8; 
const int DUTY_CYCLE = 128;   
const int MIN_FREQ = 200; 

int m1_current_freq = 0;
int m1_target_freq = 0;
unsigned long m1_last_update = 0;
bool m1_dir_state = LOW; 
bool m1_pending_reverse = false;
int m1_resume_freq = 0;

int m2_current_freq = 0;
int m2_target_freq = 0;
unsigned long m2_last_update = 0;
bool m2_dir_state = LOW; 
bool m2_pending_reverse = false;
int m2_resume_freq = 0;

int m1_velocity_target = 0;
int m2_velocity_target = 0;

// Proměnné pro hlídání puštěné klávesy (Timeout)
unsigned long last_wasd_time = 0;
bool wasd_active = false;

// --- DYNAMIKA RAMPY (Zvýšeno pro cíle 4k a 10k) ---
const int RAMP_INTERVAL = 10; 
const int M1_RAMP_MIN = 50;   
const int M1_RAMP_MAX = 200;   
const int M2_RAMP_MIN = 100;   
const int M2_RAMP_MAX = 500;  
const int SMOOTH_ZONE = 1000; 

const int BRAKE_MULTIPLIER = 3; 

void setM1Dir(bool dir) {
  m1_dir_state = dir;
  digitalWrite(M1_DIR_PIN, m1_dir_state);
}

void setM2Dir(bool dir) {
  m2_dir_state = dir;
  digitalWrite(M2_DIR_PIN, m2_dir_state);
}

void updateM1State() {
  bool desired_dir = (m1_velocity_target >= 0) ? LOW : HIGH;
  int desired_freq = abs(m1_velocity_target);

  if (desired_dir != m1_dir_state && m1_current_freq > 0) {
    m1_target_freq = 0;            
    m1_pending_reverse = true;     
    m1_resume_freq = desired_freq; 
  } else {
    setM1Dir(desired_dir);
    m1_target_freq = desired_freq;
    m1_pending_reverse = false;
  }
}

void updateM2State() {
  bool desired_dir = (m2_velocity_target >= 0) ? LOW : HIGH;
  int desired_freq = abs(m2_velocity_target);

  if (desired_dir != m2_dir_state && m2_current_freq > 0) {
    m2_target_freq = 0;
    m2_pending_reverse = true;
    m2_resume_freq = desired_freq;
  } else {
    setM2Dir(desired_dir);
    m2_target_freq = desired_freq;
    m2_pending_reverse = false;
  }
}

void setupMotory() {
  pinMode(M1_DIR_PIN, OUTPUT); pinMode(M1_EN_PIN, OUTPUT);
  pinMode(M2_DIR_PIN, OUTPUT); pinMode(M2_EN_PIN, OUTPUT);
  pinMode(MS_PIN, OUTPUT);

  digitalWrite(MS_PIN, HIGH); 
  digitalWrite(M1_EN_PIN, LOW);  
  digitalWrite(M2_EN_PIN, LOW);  

  digitalWrite(M1_DIR_PIN, m1_dir_state);
  digitalWrite(M2_DIR_PIN, m2_dir_state);

  ledcAttach(M1_STEP_PIN, 1001, PWM_RESOLUTION); 
  ledcAttach(M2_STEP_PIN, 1002, PWM_RESOLUTION); 
  ledcWrite(M1_STEP_PIN, 0); ledcWrite(M2_STEP_PIN, 0);
}

void updateMotorRamp(int step_pin, int &current_freq, int target_freq, unsigned long &last_update, int min_step, int max_step) {
  if (current_freq != target_freq && millis() - last_update >= RAMP_INTERVAL) {
    int distance_to_target = abs(target_freq - current_freq);
    int current_step = max_step;

    if (current_freq < SMOOTH_ZONE || distance_to_target < SMOOTH_ZONE) current_step = min_step; 
    else if (current_freq < (SMOOTH_ZONE * 2) || distance_to_target < (SMOOTH_ZONE * 2)) current_step = min_step + (max_step - min_step) / 2;

    if (current_freq < target_freq) {
      if (current_freq == 0) current_freq = MIN_FREQ; 
      else current_freq += current_step;
      if (current_freq > target_freq) current_freq = target_freq;
    } else if (current_freq > target_freq) {
      current_freq -= (current_step * BRAKE_MULTIPLIER);
      if (current_freq <= MIN_FREQ && target_freq == 0) current_freq = 0; 
      else if (current_freq < target_freq) current_freq = target_freq;
    }

    if (current_freq == 0) ledcWrite(step_pin, 0); 
    else { ledcWrite(step_pin, DUTY_CYCLE); ledcWriteTone(step_pin, current_freq); }
    last_update = millis();
  }
}

void updateMotory() {
  updateMotorRamp(M1_STEP_PIN, m1_current_freq, m1_target_freq, m1_last_update, M1_RAMP_MIN, M1_RAMP_MAX);
  updateMotorRamp(M2_STEP_PIN, m2_current_freq, m2_target_freq, m2_last_update, M2_RAMP_MIN, M2_RAMP_MAX);

  if (m1_pending_reverse && m1_current_freq == 0) {
    setM1Dir(!m1_dir_state);
    m1_target_freq = m1_resume_freq; 
    m1_pending_reverse = false;      
  }

  if (m2_pending_reverse && m2_current_freq == 0) {
    setM2Dir(!m2_dir_state);
    m2_target_freq = m2_resume_freq; 
    m2_pending_reverse = false;      
  }

  // --- HLÍDÁNÍ TIMEOUTU (150 ms) ---
  extern bool isCalibrating;
  extern bool m1_auto_active;
  
  if (wasd_active && millis() - last_wasd_time > 100) {
    wasd_active = false; // Vypršel čas, klávesa byla zřejmě puštěna
    
    // Zastavíme motory, ale jen pokud zrovna nepracují na nějaké auto-úloze
    if (!m1_auto_active) {
      m1_velocity_target = 0;
      updateM1State();
    }
    if (!isCalibrating) {
      m2_velocity_target = 0;
      updateM2State();
    }
  }
}

void zpracujPrikazBT(char cmd) {
  extern BluetoothSerial SerialBT; 
  extern bool m1_auto_active; 

  switch (cmd) {
    case 's': case 'S': 
      m1_auto_active = false; 
      m1_velocity_target = 2000; // Pevná rychlost
      updateM1State();
      last_wasd_time = millis(); // Reset timeoutu
      wasd_active = true;
      break;
      
    case 'w': case 'W': 
      m1_auto_active = false; 
      m1_velocity_target = -2000; 
      updateM1State();
      last_wasd_time = millis();
      wasd_active = true;
      break;

    case 'd': case 'D': 
      m2_velocity_target = 8000; 
      updateM2State();
      last_wasd_time = millis();
      wasd_active = true;
      break;
      
    case 'a': case 'A': 
      m2_velocity_target = -8000; 
      updateM2State();
      last_wasd_time = millis();
      wasd_active = true;
      break;

    case 'x': case 'X':
      m1_auto_active = false; 
      m1_velocity_target = 0; updateM1State();
      m2_velocity_target = 0; updateM2State();
      SerialBT.println(">>> NOUZOVE ZASTAVENI MOTORU (X) <<<");
      break;
  }
}