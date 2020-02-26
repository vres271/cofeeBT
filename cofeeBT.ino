#include <OneWire.h>
#include <SoftwareSerial.h>

const int GRX_PIN = 5;
const int GTX_PIN = 6;
const int TEMP_PIN = 7;
const int HEATER_PIN = 3;
const int LED_GREEN_PIN = 9;
const int LED_RED_PIN = 8;

unsigned int PWM_T = 40;
unsigned int REQ_T = 94;
unsigned int EXT_T = 180;

boolean LOG_T = true;
boolean MUTE = true;

// Beeper
#define beepPIN A4
#define beepGND A3

// EEPROM
#define EEPROM_OFFSET 32   // сдвиг памяти - размер области для служебных данных
#include <EEPROM.h>

byte state = 0;
boolean sensorInited = false;
float T = 0;
unsigned long t = 0;
unsigned long start_time = 0;
unsigned long ext_start_time = 0;

byte i;
byte data[12];
byte addr[8];
byte present = 0;
byte type_s;

// BT SoftwareSerial

char divider = ' ';
char ending = ';';
const char *headers[]  = {
  "getconfig",
  "T0",
  "T1",
  "extt",
  "log",
  "mute",
  "help",
};
enum names {
  GET_CONFIG, 
  _PWM, 
  _REQ, 
  _EXT, 
  _LOG_T, 
  _MUTE, 
  HELP, 
};
names thisName;
byte headers_am = sizeof(headers) / 2;
uint32_t prsTimer;
String prsValue = "";
String prsHeader = "";
enum stages {WAIT, HEADER, GOT_HEADER, VALUE, SUCCESS};
stages parseStage = WAIT;
boolean recievedFlag;

SoftwareSerial BTSerial(GRX_PIN, GTX_PIN);


OneWire  ds(TEMP_PIN);  // подключен к 7 пину (резистор на 4.7к обязателен)


void setup(void) {

  pinMode(HEATER_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);

  pinMode(beepPIN, OUTPUT);
  pinMode(beepGND, OUTPUT);
  digitalWrite(beepGND, LOW);

  Serial.begin(9600);
  BTSerial.begin(9600); BTSerial.setTimeout(100);

  log("\n\nCofeeBT\n");
  

  readSettings();
  hello();

  sensorInited = initTempSensor();
  printConfig();
  state = 1;
  ext_start_time = 0;
  start_time = millis();
}

void loop(void) {

	T = getTempSensorValue();
  t = millis();

  echoT();
  parsingSeparate();
  SerialRouter();

  if(state == 5) return;
  if((ext_start_time>0) && (t - ext_start_time > 1000*EXT_T) && (state != 5)) {
    heaterOff();
    switchGreen(true);
    switchRed(false);
    log("\nReady!\n");
    state = 5;
    music1();
    return;
  }

  if (T < PWM_T) {
    heaterOn();
    switchRed(true);
    state = 2;
  } else if((PWM_T<=T) & (T<=REQ_T+2)) {
    pwm(T,t);
    if(REQ_T-2<=T) {
      blinkRed(map(t - ext_start_time, 0, 1000*EXT_T, 500, 25));
      if(ext_start_time==0) {
        ext_start_time = millis();
        log("\nt: ");log((t-start_time)/1000); log(";  Extraction started");
        beep(1000,140); delay(150);
        beep(1000,140); delay(150);
        beep(1800,140); delay(150);
      }
      state = 4;
    } else {
      switchRed(true);
      state = 3;
    }
  } else {
    heaterOff();
    state = 6;
  }

}

unsigned long last_pwm_tick = 0;
unsigned long pwm_tick_period = 1000;
void pwm(float T, long unsigned t) {
  if(T>REQ_T) {heaterOff(); return;}
  unsigned int t1 = map(T, PWM_T, REQ_T+2, 1100, 50);
  if(t - last_pwm_tick > pwm_tick_period) {
    heaterOn(); 
  }
  if((t - t1 - last_pwm_tick) > pwm_tick_period) {
    heaterOff();
    last_pwm_tick = t - t1;
  }
}


unsigned long last_echo_time = 0;
void echoT() {
  if(!LOG_T) return;
  unsigned long tE = millis();
  if(tE-last_echo_time >= 1000) {
    log("\nt: "); 
    log((t-start_time)/1000); 
    log("; T: "); 
    log(T); 
    log("; State: "); 
    log(state);
    last_echo_time = tE;
  }
}

void hello() {
  beep(3000,200);  delay(210);
  beep(5000,200);  delay(210);
  heaterOff();  delay(150);
  heaterOn();  delay(150);
  heaterOff();  delay(150);
  switchGreen(true); delay(200); switchRed(true); delay(200);
  switchGreen(false); delay(200); switchRed(false);
}

void heaterOn() {
	digitalWrite(HEATER_PIN, LOW);
}

void heaterOff() {
	digitalWrite(HEATER_PIN, HIGH);
}

void switchGreen(boolean value) {
  digitalWrite(LED_GREEN_PIN, value);
}

void switchRed(boolean value) {
  digitalWrite(LED_RED_PIN, value);
}

unsigned long last_blink_red_time = 0;
void blinkRed(unsigned long period) {
  unsigned long dt = millis() - last_blink_red_time;
  if(dt < period) {switchRed(true); return;}
  if((period <= dt) & (dt <= period*2)) {switchRed(false); return;}
  if(period*2 < dt) {last_blink_red_time = millis(); switchRed(true); return;}
}

void printConfig() {
  log("\nInited: "); log(sensorInited); 
  log("\nPWM start temperature: "); log(PWM_T);
  log("\nExtraction temperature: "); log(REQ_T);
  log("\nExtraction time: "); log(EXT_T);
  log("\nLogging: "); log(LOG_T);
  log("\nMute sounds: "); log(MUTE);
  log("\n\n"); 
}

boolean initTempSensor() {
  
  if ( !ds.search(addr)) {
    log("No more addresses.\n");
    ds.reset_search();
    delay(50);
    return false;
  }
  
  //BTSerial.print("ROM =");
  //for( i = 0; i < 8; i++) {
  //  BTSerial.write(' ');
  //  BTSerial.print(addr[i], HEX);
  //}

  if (OneWire::crc8(addr, 7) != addr[7]) {
      log("CRC is not valid!\n");
      return false;
  }
 
  // the first ROM byte indicates which chip
  switch (addr[0]) {
    case 0x10:
      log("Chip = DS18S20\n");  // or old DS1820
      type_s = 1;
      break;
    case 0x28:
      log("Chip = DS18B20\n");
      type_s = 0;
      break;
    case 0x22:
      log("Chip = DS1822\n");
      type_s = 0;
      break;
    default:
      log("Device is not a DS18x20 family device.\n");
      return false;
  }
  return true; 	
}

float getTempSensorValue() {
  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);        // начало коммуникации
  
  delay(50);
  
  present = ds.reset();
  ds.select(addr);    
  ds.write(0xBE);         // читаем значение

  //Serial.print("  Data = ");
  //Serial.print(present, HEX);
  //Serial.print(" ");
  for ( i = 0; i < 9; i++) {           // смотрим 9 байтов
    data[i] = ds.read();
    //Serial.print(data[i], HEX);
    //Serial.print(" ");
  }
  //Serial.print(" CRC=");
  //Serial.print(OneWire::crc8(data, 8), HEX);
  //Serial.println();

  // Преобразуем получненный данные в температуру
  // Используем int16_t тип, т.к. он равен 16 битам
  // даже при компиляции под 32-х битный процессор
  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3;
    if (data[7] == 0x10) {
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    if (cfg == 0x00) raw = raw & ~7;
    else if (cfg == 0x20) raw = raw & ~3;
    else if (cfg == 0x40) raw = raw & ~1;
  }
  return (float)raw / 16.0;

}


void readSettings() {

  PWM_T = EEPROM.get(2, PWM_T);
  //if(PWM_T != true || PWM_T != false) PWM_T = false;
  //log("PWM_T: "); log(PWM_T); log("\n");

  REQ_T = EEPROM.get(4, REQ_T);
  //if(REQ_T != true || REQ_T != false) REQ_T = false;
  //log("REQ_T: "); log(REQ_T); log("\n");

  EXT_T = EEPROM.get(6, EXT_T);
  //if(EXT_T != true || EXT_T != false) EXT_T = false;
  //log("EXT_T: "); log(EXT_T); log("\n");

  LOG_T = EEPROM.get(8, LOG_T);
  //if(LOG_T != true || LOG_T != false) LOG_T = false;
  //log("LOG_T: "); log(LOG_T); log("\n");

  MUTE = EEPROM.get(9, MUTE);
  //if(MUTE != true || MUTE != false) MUTE = false;
  //log("MUTE: "); log(MUTE); log("\n");

  //log("\n");
}


void parsingSeparate() {
  if (BTSerial.available() > 0) {
    if (parseStage == WAIT) {
      parseStage = HEADER;
      prsHeader = "";
      prsValue = "";
    }
    if (parseStage == GOT_HEADER) parseStage = VALUE;
    char incoming = (char)BTSerial.read();
    if (incoming == divider) {
      parseStage = GOT_HEADER;
    } else if (incoming == ending) {
      parseStage = SUCCESS;
    }
    if (parseStage == HEADER) {
      prsHeader += incoming;
    }
    else if (parseStage == VALUE) prsValue += incoming;
    prsTimer = millis();
  }
  if (parseStage == SUCCESS) {
    for (byte i = 0; i < headers_am; i++) { if (prsHeader == headers[i]) { thisName = i; } } recievedFlag = true; parseStage = WAIT; } if ((millis() - prsTimer > 10) && (parseStage != WAIT)) {  // таймаут
    parseStage = WAIT;
  }
}

void SerialRouter() {
  if (recievedFlag) {
    recievedFlag = false;
    if (thisName == GET_CONFIG) {
      printConfig();
    } else if (thisName == _PWM) {
      if(prsValue != "") {
        PWM_T = prsValue.toInt();
        EEPROM.put(2, PWM_T);
      }
      log("\nPWM start temperature: "); log(PWM_T); log("\n");
    } else if (thisName == _REQ) {
      if(prsValue != "") {
        REQ_T = prsValue.toInt();
        EEPROM.put(4, REQ_T);
      }
      log("\nExtraction temperature: "); log(REQ_T); log("\n");
    } else if (thisName == _EXT) {
      if(prsValue != "") {
        EXT_T = prsValue.toInt();
        EEPROM.put(6, EXT_T);
      }
      log("\nExtraction time: "); log(EXT_T); log("\n");
    } else if (thisName == _LOG_T) {
      if(prsValue != "") {
        LOG_T = prsValue.toInt();
        EEPROM.put(8, LOG_T);
      }
      log("\nLogging: "); log(LOG_T); log("\n");
    } else if (thisName == _MUTE) {
      if(prsValue != "") {
        MUTE = prsValue.toInt();
        EEPROM.put(9, MUTE);
      }
      log("\nMute sounds: "); log(MUTE); log("\n");
    } else if (thisName == HELP) {
      printHelp();
    }
    thisName = '0'; prsValue=""; parseStage = WAIT;
  }
}

void beep(unsigned int freq, unsigned int duration) {
  if(MUTE) return;
  tone(beepPIN, freq,duration);
}


template<typename T>
T log(T text) {
  Serial.print(text);
  BTSerial.print(text);
}

void printHelp() {
  log("\ngetconfig : returns config");
  log("\nT0 <int[Celsius degree]>: get/set PWM start temperature");
  log("\nT1 <int[Celsius degree]>: get/set Extraction temperature");
  log("\nextt <int[seconds]>: get/set Extraction time");
  log("\nlog <1-0>: get/set Logging");
  log("\nmute <1-0>: get/set Mute sounds");
  log("\nhelp : print this^");
  log("\n");


}


void music1() {
  if(MUTE) return;
  tone(beepPIN, 700, 300); delay(600);
  tone(beepPIN, 700, 300); delay(600);
  tone(beepPIN, 780, 150); delay(300);
  tone(beepPIN, 700, 150); delay(300);
  tone(beepPIN, 625, 450); delay(600);
  tone(beepPIN, 590, 150); delay(300);
  tone(beepPIN, 520, 150); delay(300);
  tone(beepPIN, 460, 450); delay(600);
  tone(beepPIN, 350, 450); delay(600); delay(600);
  tone(beepPIN, 350, 450); delay(600);
  tone(beepPIN, 460, 450); delay(600);
  tone(beepPIN, 520, 150); delay(300);
  tone(beepPIN, 590, 150); delay(300);
  tone(beepPIN, 625, 450); delay(600);
  tone(beepPIN, 590, 150); delay(300);
  tone(beepPIN, 520, 150); delay(300);
  tone(beepPIN, 700, 1350); delay(1800);
  tone(beepPIN, 700, 300); delay(600);
  tone(beepPIN, 700, 300); delay(600);
  tone(beepPIN, 780, 150); delay(300);
  tone(beepPIN, 700, 150); delay(300);
  tone(beepPIN, 625, 450); delay(600);
  tone(beepPIN, 590, 150); delay(300);
  tone(beepPIN, 520, 150); delay(300);
  tone(beepPIN, 460, 450); delay(600);
  tone(beepPIN, 350, 450); delay(600); delay(600); 
  tone(beepPIN, 350, 450); delay(600); 
  tone(beepPIN, 625, 450); delay(600); 
  tone(beepPIN, 590, 150); delay(300); 
  tone(beepPIN, 520, 150); delay(300); 
  tone(beepPIN, 700, 450); delay(600); 
  tone(beepPIN, 590, 150); delay(300); 
  tone(beepPIN, 520, 150); delay(300); 
  tone(beepPIN, 460, 1350); delay(5000);  
}

void music2() {
  if(MUTE) return;
  tone(beepPIN,1318,150); delay(150);
  tone(beepPIN,1318,300); delay(300);
  tone(beepPIN,1318,150); delay(300);
  tone(beepPIN,1046,150); delay(150);
  tone(beepPIN,1318,300); delay(300);
  tone(beepPIN,1568,600); delay(600);
  tone(beepPIN,784,600); delay(600);
}
