#include <EEPROM.h>
#include <string.h>

// ======================== Block-Struktur (muss VOR collectBlocks stehen) ========================
struct Block {
  uint16_t start;
  uint16_t end;
};

// ======================== EEPROM Layout ========================
#define EEPROM_MAGIC_ADDR      0
#define EEPROM_USER_COUNT_ADDR 1
#define EEPROM_USER_START      2      // user records: 64 bytes each (name[16]+pass[16]+motd[32])
#define MAX_USERS              5
#define USER_RECORD_SIZE       64
#define EEPROM_FILE_TABLE      (EEPROM_USER_START + MAX_USERS * USER_RECORD_SIZE) // 322
#define MAX_FILES              15
#define FILE_RECORD_SIZE       26    // name[20] + owner[1] + type[1] + size[2] + start[2]
#define EEPROM_DATA_START      (EEPROM_FILE_TABLE + MAX_FILES * FILE_RECORD_SIZE) // 712
#define EEPROM_SIZE            1024
#define EEPROM_DATA_TOP        1024

#define TYPE_FREE 0xFF
#define TYPE_FILE 0

// ======================== Globale Variablen ========================
bool logged_in = false;
uint8_t current_user_id = 0;
char current_user_name[16];
bool blink_mode = false;
unsigned long blink_interval = 500;
unsigned long last_blink_toggle = 0;
int led_state = LOW;

// ======================== Hilfsfunktion: Zeilen lesen (MIT BACKSPACE) ========================
void readLine(char* buffer, size_t bufsize, bool echo = true) {
  size_t idx = 0;
  while (idx < bufsize - 1) {
    if (Serial.available()) {
      char c = Serial.read();

      // Backspace (ASCII 8) oder DEL (ASCII 127)
      if (c == '\b' || c == 127) {
        if (idx > 0) {
          idx--;
          if (echo) {
            Serial.print("\b \b");   // Letztes Zeichen auf Terminal löschen
          }
        }
        continue;
      }

      if (c == '\n' || c == '\r') {
        if (idx == 0 && c == '\r') continue;   // einzelnes CR ignorieren
        Serial.println();                      // Zeilenumbruch ausgeben
        break;
      }

      buffer[idx++] = c;
      if (echo) {
        Serial.print((char)toupper(c));        // Echo als Großbuchstabe
      }
    }
  }
  buffer[idx] = '\0';
}

// ======================== EEPROM Basisoperationen ========================
void eepromWriteString(int addr, const char* str, int maxlen) {
  size_t len = strlen(str);
  if (len >= maxlen) len = maxlen - 1;
  for (int i = 0; i < maxlen; i++) {
    EEPROM.write(addr + i, (i < len) ? str[i] : 0);
  }
}

void eepromReadString(int addr, char* buffer, int maxlen) {
  for (int i = 0; i < maxlen; i++) {
    buffer[i] = EEPROM.read(addr + i);
    if (buffer[i] == 0) break;
  }
  buffer[maxlen - 1] = '\0';
}

// ======================== Benutzerverwaltung ========================
void readUser(uint8_t id, char* name, char* pass, char* motd) {
  int addr = EEPROM_USER_START + id * USER_RECORD_SIZE;
  eepromReadString(addr, name, 16);
  eepromReadString(addr + 16, pass, 16);
  eepromReadString(addr + 32, motd, 32);
}

void writeUser(uint8_t id, const char* name, const char* pass, const char* motd) {
  int addr = EEPROM_USER_START + id * USER_RECORD_SIZE;
  eepromWriteString(addr, name, 16);
  eepromWriteString(addr + 16, pass, 16);
  eepromWriteString(addr + 32, motd, 32);
}

uint8_t getUserCount() {
  return EEPROM.read(EEPROM_USER_COUNT_ADDR);
}

void setUserCount(uint8_t count) {
  EEPROM.write(EEPROM_USER_COUNT_ADDR, count);
}

int findUser(const char* name) {
  uint8_t count = getUserCount();
  for (uint8_t i = 0; i < count; i++) {
    char n[16];
    eepromReadString(EEPROM_USER_START + i * USER_RECORD_SIZE, n, 16);
    if (strcasecmp(n, name) == 0) return i;
  }
  return -1;
}

// ======================== Dateisystem (flach) ========================
void getFileEntry(uint8_t idx, char* name, uint8_t &owner, uint8_t &type, uint16_t &size, uint16_t &start) {
  int addr = EEPROM_FILE_TABLE + idx * FILE_RECORD_SIZE;
  eepromReadString(addr, name, 20);
  owner = EEPROM.read(addr + 20);
  type  = EEPROM.read(addr + 21);
  size  = EEPROM.read(addr + 22) | (uint16_t)(EEPROM.read(addr + 23)) << 8;
  start = EEPROM.read(addr + 24) | (uint16_t)(EEPROM.read(addr + 25)) << 8;
}

void setFileEntry(uint8_t idx, const char* name, uint8_t owner, uint8_t type, uint16_t size, uint16_t start) {
  int addr = EEPROM_FILE_TABLE + idx * FILE_RECORD_SIZE;
  eepromWriteString(addr, name, 20);
  EEPROM.write(addr + 20, owner);
  EEPROM.write(addr + 21, type);
  EEPROM.write(addr + 22, size & 0xFF);
  EEPROM.write(addr + 23, (size >> 8) & 0xFF);
  EEPROM.write(addr + 24, start & 0xFF);
  EEPROM.write(addr + 25, (start >> 8) & 0xFF);
}

int findFreeFileEntry() {
  for (int i = 0; i < MAX_FILES; i++) {
    uint8_t type = EEPROM.read(EEPROM_FILE_TABLE + i * FILE_RECORD_SIZE + 21);
    if (type == TYPE_FREE) return i;
  }
  return -1;
}

int findFileByUserIndex(uint8_t owner, uint8_t userIndex) {
  uint8_t cnt = 0;
  for (int i = 0; i < MAX_FILES; i++) {
    char name[20];
    uint8_t o, type;
    uint16_t size, start;
    getFileEntry(i, name, o, type, size, start);
    if (type == TYPE_FILE && o == owner) {
      if (cnt == userIndex) return i;
      cnt++;
    }
  }
  return -1;
}

int findFileByName(uint8_t owner, const char* name) {
  for (int i = 0; i < MAX_FILES; i++) {
    char n[20];
    uint8_t o, type;
    uint16_t size, start;
    getFileEntry(i, n, o, type, size, start);
    if (type == TYPE_FILE && o == owner && strcasecmp(n, name) == 0) return i;
  }
  return -1;
}

int collectBlocks(Block blocks[], int maxBlocks) {
  int n = 0;
  for (int i = 0; i < MAX_FILES; i++) {
    char name[20];
    uint8_t owner, type;
    uint16_t size, start;
    getFileEntry(i, name, owner, type, size, start);
    if (type == TYPE_FILE && size > 0) {
      if (n < maxBlocks) {
        blocks[n].start = start;
        blocks[n].end = start + size;
        n++;
      }
    }
  }
  for (int i = 0; i < n - 1; i++) {
    for (int j = 0; j < n - i - 1; j++) {
      if (blocks[j].start > blocks[j + 1].start) {
        Block tmp = blocks[j];
        blocks[j] = blocks[j + 1];
        blocks[j + 1] = tmp;
      }
    }
  }
  return n;
}

bool findFreeSpace(uint16_t needed, uint16_t &out_start) {
  Block blocks[MAX_FILES];
  int bc = collectBlocks(blocks, MAX_FILES);
  uint16_t data_bottom = EEPROM_DATA_START;
  uint16_t data_top = EEPROM_DATA_TOP;
  uint16_t cur = data_bottom;
  for (int i = 0; i < bc; i++) {
    if (blocks[i].start > cur) {
      uint16_t gap = blocks[i].start - cur;
      if (gap >= needed) {
        out_start = cur;
        return true;
      }
    }
    cur = blocks[i].end;
  }
  if (data_top - cur >= needed) {
    out_start = cur;
    return true;
  }
  return false;
}

// ======================== Dateioperationen ========================
bool createFile(uint8_t owner, const char* name, const char* data, uint16_t len) {
  int idx = findFreeFileEntry();
  if (idx == -1) return false;
  uint16_t start_addr;
  if (!findFreeSpace(len, start_addr)) return false;
  for (uint16_t i = 0; i < len; i++) EEPROM.write(start_addr + i, data[i]);
  setFileEntry(idx, name, owner, TYPE_FILE, len, start_addr);
  return true;
}

bool deleteFile(uint8_t owner, uint8_t userIndex) {
  int idx = findFileByUserIndex(owner, userIndex);
  if (idx == -1) return false;
  setFileEntry(idx, "", 0, TYPE_FREE, 0, 0);
  return true;
}

uint16_t readFileContent(uint8_t owner, uint8_t userIndex, char* buffer, uint16_t bufsize) {
  int idx = findFileByUserIndex(owner, userIndex);
  if (idx == -1) return 0;
  char name[20];
  uint8_t o, type;
  uint16_t size, start;
  getFileEntry(idx, name, o, type, size, start);
  if (type != TYPE_FILE || o != owner) return 0;
  uint16_t len = size < bufsize - 1 ? size : bufsize - 1;
  for (uint16_t i = 0; i < len; i++) buffer[i] = EEPROM.read(start + i);
  buffer[len] = '\0';
  return len;
}

// ======================== EEPROM Format (Werkseinstellungen) ========================
void eepromFormat() {
  EEPROM.write(EEPROM_MAGIC_ADDR, 0x4F);
  setUserCount(1);
  writeUser(0, "ADMIN", "ADMIN", "=== WELCOME ADMIN ===");
  for (int i = 0; i < MAX_FILES; i++) {
    setFileEntry(i, "", 0, TYPE_FREE, 0, 0);
  }
}

// ======================== Bildschirm löschen ========================
void clearScreen() {
  for (int i = 0; i < 40; i++) Serial.println();
}

// ======================== Befehlshandler ========================
struct Command {
  const char* name;
  void (*handler)(char* arg);
  bool adminOnly;
};

void cmdHelp(char*);
void cmdLs(char*);
void cmdEdit(char*);
void cmdRead(char*);
void cmdRm(char*);
void cmdUseradd(char*);
void cmdUserdel(char*);
void cmdPasswd(char*);
void cmdMotd(char*);
void cmdClear(char*);
void cmdReset(char*);
void cmdLed(char*);
void cmdExit(char*);

Command commands[] = {
  {"HELP",    cmdHelp,    false},
  {"LS",      cmdLs,      false},
  {"EDIT",    cmdEdit,    false},
  {"READ",    cmdRead,    false},
  {"RM",      cmdRm,      false},
  {"ADDUSER", cmdUseradd, true},
  {"DELUSER", cmdUserdel, true},
  {"PASSWD",  cmdPasswd,  false},
  {"MOTD",    cmdMotd,    false},
  {"CLEAR",   cmdClear,   false},
  {"RESET",   cmdReset,   true},
  {"LED",     cmdLed,     false},
  {"EXIT",    cmdExit,    false},
  {NULL, NULL, false}
};

// ======================== Befehl Implementierungen ========================
void cmdHelp(char*) {
  Serial.println(F("AVAILABLE COMMANDS:"));
  for (int i = 0; commands[i].name != NULL; i++) {
    if (!commands[i].adminOnly || current_user_id == 0) {
      Serial.print(F("  "));
      Serial.println(commands[i].name);
    }
  }
  Serial.println("LINUXMC OS IS LICENSED UNDER THE GNU GPL 3");
  Serial.println("LINUXMC OS IS MADE BY THE LINUXMC IPV& INF");
  Serial.println("THE CODE IS AVAILABLE AT: GITHUB.COM/THUNDERBOLT1003USA/LINUXMC-OS");
}

void cmdLs(char*) {
  uint8_t cnt = 0;
  for (int i = 0; i < MAX_FILES; i++) {
    char name[20];
    uint8_t o, type;
    uint16_t size, start;
    getFileEntry(i, name, o, type, size, start);
    if (type == TYPE_FILE && o == current_user_id) {
      Serial.print(cnt);
      Serial.print(F(" "));
      Serial.println(name);
      cnt++;
    }
  }
  if (cnt == 0) Serial.println(F("NO FILES"));
}

void interactiveEditor(char* filename, bool editMode) {
  char full[20];
  strncpy(full, filename, 19); full[19]=0;
  for (char* p = full; *p; p++) *p = toupper(*p);

  if (editMode) {
    int idx = findFileByName(current_user_id, full);
    if (idx == -1) {
      Serial.println(F("FILE NOT FOUND. CREATE NEW? (Y/N)"));
      char answer[4];
      readLine(answer, sizeof(answer), true);
      if (toupper(answer[0]) != 'Y') { Serial.println(F("ABORTED")); return; }
    }
  } else {
    if (findFileByName(current_user_id, full) != -1) {
      Serial.println(F("FILE EXISTS. OVERWRITE? (Y/N)"));
      char answer[4];
      readLine(answer, sizeof(answer), true);
      if (toupper(answer[0]) != 'Y') { Serial.println(F("ABORTED")); return; }
    }
  }

  Serial.println(F("ENTER TEXT, '.' ON A LINE TO FINISH:"));
  char fileBuf[512];
  uint16_t pos = 0;
  fileBuf[0] = '\0';

  while (true) {
    char line[64];
    readLine(line, sizeof(line), true);
    for (char* p = line; *p; p++) *p = toupper(*p);
    if (strcmp(line, ".") == 0) break;
    if (pos + strlen(line) + 2 >= sizeof(fileBuf)) {
      Serial.println(F("TOO LARGE, ABORTED"));
      return;
    }
    if (pos > 0) fileBuf[pos++] = '\n';
    strcpy(fileBuf + pos, line);
    pos += strlen(line);
  }

  int oldIdx = findFileByName(current_user_id, full);
  if (oldIdx != -1) {
    setFileEntry(oldIdx, "", 0, TYPE_FREE, 0, 0);
  }

  if (!createFile(current_user_id, full, fileBuf, pos)) {
    Serial.println(F("WRITE FAILED (DISK FULL?)"));
  } else {
    Serial.println(F("OK"));
  }
}

void cmdEdit(char* arg) {
  if (arg[0] == '\0') { Serial.println(F("MISSING FILENAME")); return; }
  interactiveEditor(arg, false);
}

void cmdRead(char* arg) {
  if (arg[0] == '\0') { Serial.println(F("MISSING INDEX")); return; }
  int idx = atoi(arg);
  char buf[512];
  uint16_t len = readFileContent(current_user_id, idx, buf, sizeof(buf));
  if (len == 0) Serial.println(F("FILE NOT FOUND OR EMPTY"));
  else Serial.println(buf);
}

void cmdRm(char* arg) {
  if (arg[0] == '\0') { Serial.println(F("MISSING INDEX")); return; }
  int idx = atoi(arg);
  if (deleteFile(current_user_id, idx)) Serial.println(F("OK"));
  else Serial.println(F("FILE NOT FOUND"));
}

void cmdUseradd(char* arg) {
  if (current_user_id != 0) { Serial.println(F("ADMIN ONLY")); return; }
  char name[16], pass[16];
  if (sscanf(arg, "%15s %15s", name, pass) != 2) {
    Serial.println(F("USAGE: USERADD <NAME> <PASSWORD>"));
    return;
  }
  for (char* p = name; *p; p++) *p = toupper(*p);
  for (char* p = pass; *p; p++) *p = toupper(*p);

  if (findUser(name) != -1) { Serial.println(F("USER EXISTS")); return; }
  uint8_t count = getUserCount();
  if (count >= MAX_USERS) { Serial.println(F("MAX USERS REACHED")); return; }

  char motd[32];
  snprintf(motd, 32, "=== WELCOME %s ===", name);
  writeUser(count, name, pass, motd);
  setUserCount(count + 1);
  Serial.println(F("USER CREATED"));
}

void cmdUserdel(char* arg) {
  if (current_user_id != 0) { Serial.println(F("ADMIN ONLY")); return; }
  if (arg[0] == '\0') { Serial.println(F("MISSING NAME")); return; }
  for (char* p = arg; *p; p++) *p = toupper(*p);
  if (strcmp(arg, "ADMIN") == 0) { Serial.println(F("CANNOT DELETE ADMIN")); return; }
  int id = findUser(arg);
  if (id == -1) { Serial.println(F("USER NOT FOUND")); return; }
  uint8_t count = getUserCount();
  for (int i = 0; i < MAX_FILES; i++) {
    char name[20]; uint8_t o, type; uint16_t size, start;
    getFileEntry(i, name, o, type, size, start);
    if (type == TYPE_FILE && o == id) setFileEntry(i, "", 0, TYPE_FREE, 0, 0);
  }
  if (id != count - 1) {
    char n[16], p[16], m[32];
    readUser(count - 1, n, p, m);
    writeUser(id, n, p, m);
  }
  setUserCount(count - 1);
  Serial.println(F("USER DELETED"));
}

void cmdPasswd(char*) {
  Serial.print(F("OLD PASSWORD:"));
  char old[16], newp[16];
  readLine(old, sizeof(old), false);
  for (char* p = old; *p; p++) *p = toupper(*p);

  char stored[16];
  int addr = EEPROM_USER_START + current_user_id * USER_RECORD_SIZE + 16;
  eepromReadString(addr, stored, 16);
  if (strcmp(old, stored)) { Serial.println(F("WRONG PASSWORD")); return; }

  Serial.print(F("NEW PASSWORD:"));
  readLine(newp, sizeof(newp), false);
  for (char* p = newp; *p; p++) *p = toupper(*p);
  if (strlen(newp) == 0) { Serial.println(F("TOO SHORT")); return; }
  eepromWriteString(addr, newp, 16);
  Serial.println(F("PASSWORD CHANGED"));
}

void cmdMotd(char* arg) {
  int addr = EEPROM_USER_START + current_user_id * USER_RECORD_SIZE + 32;
  if (arg[0] == '\0') {
    char current[32];
    eepromReadString(addr, current, 32);
    Serial.print(F("MOTD: "));
    Serial.println(current);
  } else {
    for (char* p = arg; *p; p++) *p = toupper(*p);
    eepromWriteString(addr, arg, 32);
    Serial.println(F("MOTD UPDATED"));
  }
}

void cmdClear(char*) {
  clearScreen();
}

void cmdReset(char*) {
  if (current_user_id != 0) { Serial.println(F("ADMIN ONLY")); return; }
  eepromFormat();
  Serial.println(F("SYSTEM RESET. YOU WILL BE LOGGED OUT."));
  logged_in = false;
}

void cmdLed(char* arg) {
  if (strcmp(arg, "ON") == 0) {
    digitalWrite(LED_BUILTIN, HIGH); blink_mode = false; Serial.println(F("LED ON"));
  } else if (strcmp(arg, "OFF") == 0) {
    digitalWrite(LED_BUILTIN, LOW); blink_mode = false; Serial.println(F("LED OFF"));
  } else if (strncmp(arg, "BLINK", 5) == 0) {
    char* val = arg + 5; while (*val == ' ') val++;
    if (*val == '\0') { Serial.println(F("MISSING MILLISECONDS")); return; }
    int ms = atoi(val);
    if (ms <= 0) { Serial.println(F("VALUE > 0")); return; }
    blink_interval = ms; blink_mode = true; last_blink_toggle = millis();
    Serial.println(F("BLINK STARTED"));
  } else {
    Serial.println(F("USAGE: LED ON|OFF|BLINK <MS>"));
  }
}

void cmdExit(char*) {
  logged_in = false;
  Serial.println(F("EXITING..."));
  clearScreen();
}

// ======================== Befehl ausführen ========================
void processCommand(char* input) {
  for (char* p = input; *p; p++) *p = toupper(*p);
  char* space = strchr(input, ' ');
  char cmd[16] = {0};
  char arg[64] = {0};
  if (space) {
    size_t len = space - input;
    if (len > 15) len = 15;
    strncpy(cmd, input, len);
    cmd[len] = '\0';
    strncpy(arg, space + 1, 63);
    arg[63] = '\0';
  } else {
    strncpy(cmd, input, 15);
    cmd[15] = '\0';
  }

  for (int i = 0; commands[i].name != NULL; i++) {
    if (strcmp(cmd, commands[i].name) == 0) {
      if (commands[i].adminOnly && current_user_id != 0) {
        Serial.println(F("ADMIN ONLY"));
        return;
      }
      commands[i].handler(arg);
      return;
    }
  }
  Serial.println(F("UNKNOWN COMMAND"));
  cmdHelp(nullptr);
}

// ======================== Login-Bildschirm ========================
void showLoginScreen() {
  clearScreen();
  Serial.println(F("=== LINUXMC OS ==="));
  Serial.println(F("CONNECT 9600"));
  int attempts = 0;
  while (attempts < 3) {
    Serial.print(F("USERNAME:"));
    char user[16];
    readLine(user, sizeof(user), true);
    for (char* p = user; *p; p++) *p = toupper(*p);

    int id = findUser(user);
    if (id == -1) {
      Serial.println(F("INVALID USER"));
      attempts++;
      continue;
    }
    Serial.print(F("PASSWORD:"));
    char pass[16];
    readLine(pass, sizeof(pass), false);
    for (char* p = pass; *p; p++) *p = toupper(*p);

    char stored[16];
    int passAddr = EEPROM_USER_START + id * USER_RECORD_SIZE + 16;
    eepromReadString(passAddr, stored, 16);
    if (strcmp(pass, stored) == 0) {
      current_user_id = id;
      eepromReadString(EEPROM_USER_START + id * USER_RECORD_SIZE, current_user_name, 16);
      logged_in = true;
      Serial.println(F("LOGIN SUCCESSFUL"));

      char motd[32];
      eepromReadString(EEPROM_USER_START + id * USER_RECORD_SIZE + 32, motd, 32);
      Serial.println(motd);
      return;
    } else {
      Serial.println(F("WRONG PASSWORD"));
      attempts++;
    }
  }
  Serial.println(F("NO CARRIER"));
  delay(2000);
}

// ======================== Setup ========================
void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  pinMode(2, INPUT_PULLUP);

  if (EEPROM.read(EEPROM_MAGIC_ADDR) != 0x4F) {
    eepromFormat();
  }
  if (digitalRead(2) == LOW) {
    delay(50);
    if (digitalRead(2) == LOW) {
      eepromFormat();
      Serial.println(F("FACTORY RESET DONE"));
    }
  }

  while (!Serial) { ; }
  delay(100);
}

// ======================== Hauptschleife ========================
void loop() {
  if (!logged_in) {
    showLoginScreen();
    if (!logged_in) return;
  }

  Serial.print(F(">"));
  while (logged_in) {
    if (blink_mode) {
      unsigned long now = millis();
      if (now - last_blink_toggle >= blink_interval) {
        last_blink_toggle = now;
        led_state = !led_state;
        digitalWrite(LED_BUILTIN, led_state);
      }
    }

    if (Serial.available()) {
      char input[80];
      readLine(input, sizeof(input), true);
      processCommand(input);
      if (!logged_in) return;
      Serial.print(F(">"));
    }
  }
}
