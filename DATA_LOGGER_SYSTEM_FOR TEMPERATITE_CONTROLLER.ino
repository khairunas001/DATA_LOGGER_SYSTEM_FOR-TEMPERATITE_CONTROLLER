#include <ModbusMaster.h>
#include <SD.h>
#include <Wire.h>
#include <RTClib.h>
#include <Ethernet.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h> 

#define MAX485_DE 8  // Pin DE connected to pin 8 on Arduino Mega
#define MAX485_RE 8  // Pin RE connected to pin 8 on Arduino Mega
#define SD_CS 4      // Pin CS for the microSD card
#define BUTTON_A 2  // Pin untuk push button A (Start loop)
#define BUTTON_B 3  // Pin untuk push button B (Stop loop)


LiquidCrystal_I2C lcd(0x27, 16, 2);  // Set the LCD address to 0x27 for a 16x2 display
// Ethernet settings
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; // MAC address
IPAddress server;    // Server address
int port;            // Server port 
String endpoint;     // Endpoint

ModbusMaster node;
RTC_DS3231 rtc;
File dataFile;
EthernetClient client;
bool sendDataToServerFlag = false;  // Default, tidak mengirim data ke server
// Variable to control the loop
bool loopRunning = true;  // Default: loop is running

unsigned long displayTimeout = 0;
bool displayingMessage = false; // Track jika sedang menampilkan pesan
unsigned long messageDuration = 10000; // Durasi pesan (3 detik)

// Variabel alamat controller, PV, dan SV
uint16_t controllerAddress = 1;  // Default controller address
uint16_t pvAddress = 0x03E8;     // Default PV address (misalnya 0x03E8)
uint16_t svAddress = 0x03EB;     // Default SV address (misalnya 0x03EB)

uint32_t baudrate;               // Variabel untuk menyimpan baudrate

// Global variables for parity and stopBits
String parity = "none";
int stopBits = 1;

unsigned long logInterval = 10000;  // Default log interval, misalnya 10 detik (10000 ms)
unsigned long lastLogTime = 0;      // Menyimpan waktu terakhir kali readAndLogData() dijalankan

// Function prototypes
void setupModbus();
void setupSDCard();
void setupRTC();
void setupEthernet();
void readAndLogData();
void writeDataToSD(int16_t processValue, int16_t setValue, DateTime now);
void sendDataToServer(int16_t processValue, int16_t setValue, DateTime now);
int16_t signedValue(uint16_t modbusValue);
int16_t readModbusValue(uint16_t address);
void readUserInput(bool initialSetup);
void saveUserInputToSD();
bool loadUserInputFromSD();
void readAddressConfiguration();
void saveAddressConfigToSD();
bool loadAddressConfigFromSD();
bool isValidIP(const String& ip);
bool isValidPort(int port);
void deleteConfigFromSD();
void resetRTCDateTime();
uint32_t loadBaudrateFromSD();  // Function to load baudrate from SD
void updateBaudrate(uint32_t newBaudrate);  // Function to update baudrate dynamically
void saveSerialConfigToSD(const String& parity, int stopBits);
void loadSerialConfigFromSD(String &parity, int &stopBits);
void configureSerial1(const String& parity, int stopBits);

void preTransmission() {
  digitalWrite(MAX485_RE, 1);
  digitalWrite(MAX485_DE, 1);
}

void postTransmission() {
  digitalWrite(MAX485_RE, 0);
  digitalWrite(MAX485_DE, 0);
}

void setup() {
  pinMode(MAX485_RE, OUTPUT);
  pinMode(MAX485_DE, OUTPUT);
  digitalWrite(MAX485_RE, 0);
  digitalWrite(MAX485_DE, 0);

  pinMode(BUTTON_A, INPUT_PULLUP);  // Setup button A with internal pull-up resistor
  pinMode(BUTTON_B, INPUT_PULLUP);  // Setup button B with internal pull-up resistor
  
  Serial.begin(9600);  // Serial USB tetap di 9600 untuk debugging

  // Setup SD card dan load baudrate dari SD card
  setupSDCard();

  // Load logInterval dari SD card
  logInterval = loadLogIntervalFromSD();

  // Load Serial settings from SD
  String parity = "no";
  int stopBits = 1;
  loadSerialConfigFromSD(parity, stopBits);  // Load parity and stopBits

  // Load baudrate dari SD card
  baudrate = loadBaudrateFromSD();

  Serial1.begin(baudrate);  // Serial1 menggunakan baudrate dari SD card
  // Hanya sekali lakukan konfigurasi Serial1 dengan semua parameter
  configureSerial1(parity, stopBits);  // Konfigurasikan Serial1 dengan parity, stopBits, dan baudrate
  Serial.print("Baudrate saat ini: ");
  Serial.println(baudrate);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Inisialisasi...");
  Serial.println("Inisialisasi...");

  // Inisialisasi Ethernet dulu sebelum melakukan apapun yang lain
  setupEthernet();

  // Jika Ethernet berhasil diinisialisasi, baru lakukan loadUserInputFromSD()
  if (Ethernet.hardwareStatus() != EthernetNoHardware) { 
    if (!loadUserInputFromSD()) {
      Serial.println("Konfigurasi tidak ditemukan.");
      Serial.println("Konfigurasi ulang dimulai.");
      readUserInput(true);
      saveUserInputToSD();
    }
  } else {  
    Serial.println("Ethernet shield tidak ditemukan, melewati konfigurasi jaringan.");
  }

  // Load alamat komunikasi
  if (!loadAddressConfigFromSD()) {
    Serial.println("Konfigurasi alamat tidak ditemukan.");
    Serial.println("Menggunakan alamat default.");
  }

  setupModbus();
  setupRTC();
  
  // Update nilai awal di LCD
  lcd.clear();  // Bersihkan layar sebelum menampilkan nilai baru
  lcd.setCursor(0, 0);
  lcd.print("PV=50C SV=30C");  // Initial values
}


void loop() {
  static unsigned long displayTimeout = 0;
  static bool displayingMessage = false;  // Tracks if we're currently displaying "started" or "stopped"

  if (digitalRead(BUTTON_A) == LOW) {  // Button A pressed
    if (!loopRunning) {
      Serial.println("Loop started.");
      loopRunning = true;
      displayingMessage = true;
      displayStatus("Logging Started");
      displayTimeout = millis() + 3000;  // Show "Started" for 3 seconds
    }
    delay(200);  // Debouncing delay
  } 
  
  if (digitalRead(BUTTON_B) == LOW) {  // Button B pressed
    if (loopRunning) {
      Serial.println("Loop stopped.");
      loopRunning = false;
      displayStatus("Logging Stopped");
      displayingMessage = true;  // Keep "Stopped" displayed until button A is pressed
    }
    delay(200);  // Debouncing delay
  }

  // After 3 seconds, revert to displaying PV and SV if the loop is running
  if (millis() > displayTimeout && displayingMessage && loopRunning) {
    displayingMessage = false;
    readAndLogData();  // This will update the LCD with PV, SV, and date/time
  }

  if (Serial.available()) {
    handleSerialInput();  // Handle the serial inputs separately
  }

 // Hanya jalankan jika loopRunning bernilai true
  if (loopRunning && !displayingMessage) {
    unsigned long currentTime = millis();
    
    // Cek apakah sudah melewati interval log yang diatur oleh user
    if (currentTime - lastLogTime >= logInterval) {
      readAndLogData();  // Log data dan update LCD
      lastLogTime = currentTime;  // Perbarui lastLogTime
    }
  }
  delay(1000);  // Adjust delay as needed
}

// Fungsi untuk menampilkan status di LCD
void displayStatus(const String &status) {
  lcd.clear();  // Bersihkan layar LCD
  lcd.setCursor(0, 0);  // Atur kursor ke posisi awal
  lcd.print(status);  // Tampilkan status
  displayTimeout = millis() + messageDuration;  // Atur timeout untuk kembali ke tampilan sebelumnya
  displayingMessage = true;  // Tandai bahwa kita sedang menampilkan pesan
}

// Fungsi untuk menampilkan status di LCD
void displayStatus2(const String &status) {
  lcd.setCursor(0, 1);  // Atur kursor ke posisi awal
  lcd.print(status);  // Tampilkan status
  displayTimeout = millis() + messageDuration;  // Atur timeout untuk kembali ke tampilan sebelumnya
  displayingMessage = true;  // Tandai bahwa kita sedang menampilkan pesan
}


// Function to handle input from Serial Monitor
void handleSerialInput() {
  String input = Serial.readStringUntil('\n');
  input.trim();  // Ensuring input is properly formatted
  
  if (input == "1") {
    Serial.println("Loop started.");
    displayStatus("Loop dimulai.");  // Tampilkan status di LCD, dipotong otomatis ke 16 karakter
    loopRunning = true;  // Mulai loop
  } 
  else if (input == "0") {
    Serial.println("Loop stopped.");
    displayStatus("Loop dihentikan.");  // Tampilkan status di LCD
    loopRunning = false;  // Hentikan loop
  } 
  else if (input == "0101") {
    Serial.println("Konfigurasi ulang dimulai, use server?(y/n)");
    displayStatus("server?(y/n)");  // Tampilkan status di LCD, pastikan maksimal 16 karakter
    readUserInput(false);  // Meminta input dari user dengan konfirmasi y/n
    saveUserInputToSD();   // Simpan konfigurasi baru
    displayStatus("Konfig. disimpan");  // Tampilkan status di LCD, dipotong otomatis ke 16 karakter
    delay(3000);  
  }
  else if (input == "config-delete") {
    Serial.println("Menghapus konfigurasi jaringan...");
    displayStatus("delete config.");  // Tampilkan status di LCD
    deleteConfigFromSD();
    displayStatus("Konfig. dihapus.");  // Tampilkan status di LCDsss
    delay(3000);
  }
  else if (input == "1111") {  // New option to reset RTC date and time
    Serial.println("Penyetelan ulang tanggal dan waktu dimulai.");
    displayStatus("Reset tanggal...");  // Tampilkan status di LCD
    resetRTCDateTime();  // Panggil fungsi untuk reset RTC
    displayStatus("Tanggal di-reset.");  // Tampilkan status di LCD
    delay(3000);
  }
  else if (input == "0000") {
    Serial.println("Konfigurasi alamat controller, PV, dan SV dimulai.");
    displayStatus("Konfig. alamat...");  // Tampilkan status di LCD
    c();  // Meminta input untuk alamat baru
    saveAddressConfigToSD();     // Simpan konfigurasi alamat ke SD card
    node.begin(controllerAddress, Serial1);  // Apply new controller address
    displayStatus("Alamat disimpan.");  // Tampilkan status di LCD
    delay(3000);
  }
  else if (input == "01") {  // User inputs 01 to configure baudrate
    Serial.println("Masukkan baudrate baru:");
    displayStatus("Input baudrate...");  // Tampilkan status di LCD
    while (!Serial.available()) {}  // Wait for user to input baudrate
    String baudrateStr = Serial.readStringUntil('\n');  // Read baudrate input
    baudrateStr.trim();
    uint32_t newBaudrate = baudrateStr.toInt();  // Convert to integer

    if (newBaudrate > 0) {
      updateBaudrate(newBaudrate);  // Update baudrate
      saveBaudrateToSD(newBaudrate); // Save baudrate to SD card
      displayStatus("Baudrate updated");  // Tampilkan status di LCD
    } else {
      Serial.println("Baudrate tidak valid.");
      displayStatus("Baudrate tidak valid.");  // Tampilkan status di LCD
      delay(3000);
    }
  }
  else if (input == "11") {  // Configure parity and stop bits
    displayStatus("reconfig com");
    configureParityAndStopBits();
    displayStatus("com updated");
    delay(3000);
  }
  else if (input == "00") {  // Konfigurasi delay untuk readAndLogData
    Serial.println("Masukkan delay baru (dalam milidetik):");
    displayStatus("Input delay...");  // Tampilkan status di LCD
    while (!Serial.available()) {}  // Tunggu input dari user
    String delayStr = Serial.readStringUntil('\n');  // Membaca input delay
    delayStr.trim();
    unsigned long newDelay = delayStr.toInt();  // Konversi input ke integer

    if (newDelay > 0) {
      logInterval = newDelay;  // Update delay interval
      saveLogIntervalToSD(logInterval);  // Simpan ke SD card
      Serial.print("Delay baru: ");
      Serial.print(logInterval);
      Serial.println(" ms");
      displayStatus("Delay updated");  // Tampilkan status di LCD
    } else {
      Serial.println("Delay tidak valid.");
      displayStatus("Delay tidak valid.");  // Tampilkan status di LCD
    }
    delay(3000);
  }
}

// Fungsi untuk menyimpan logInterval ke SD card
void saveLogIntervalToSD(unsigned long interval) {
  dataFile = SD.open("config.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.println("logInterval=" + String(interval));
    dataFile.close();
    Serial.println("Log interval disimpan ke SD card.");
  } else {
    Serial.println("Gagal menyimpan log interval ke SD card.");
  }
}

// Fungsi untuk memuat logInterval dari SD card
unsigned long loadLogIntervalFromSD() {
  unsigned long interval = 10000;  // Nilai default jika tidak ditemukan
  dataFile = SD.open("config.txt");
  if (dataFile) {
    while (dataFile.available()) {
      String line = dataFile.readStringUntil('\n');
      line.trim();
      if (line.startsWith("logInterval=")) {
        interval = line.substring(line.indexOf('=') + 1).toInt();
        Serial.print("Log interval dimuat dari SD: ");
        Serial.println(interval);
        break;
      }
    }
    dataFile.close();
  } else {
    Serial.println("File config.txt tidak ditemukan. Menggunakan logInterval default.");
  }
  return interval;
}


// New function to handle configuring parity and stop bits
void configureParityAndStopBits() {
  String parityInput, stopBitsInput;

  // Configure parity
  Serial.println("Configure parity (even/odd/none):");
  displayStatus2("(even/odd/none):");
  while (!Serial.available()) {
    // Wait for user input
  }
  parityInput = Serial.readStringUntil('\n');
  parityInput.trim();  // Ensure no extra spaces

  // Display the entered parity
  Serial.println("Parity yang dimasukkan: " + parityInput);

  // Adding a slight delay to ensure user can read and respond
  delay(500);

  // Configure stop bits
  Serial.println("Configure stop bits (1/2):");
  displayStatus2("bits(1/2):");
  while (!Serial.available()) {
    // Wait for user input
  }
  stopBitsInput = Serial.readStringUntil('\n');
  stopBitsInput.trim();
  int stopBitsValue = stopBitsInput.toInt();

  // Display the entered stop bits
  Serial.println("Stop bits yang dimasukkan: " + String(stopBitsValue));

  // Validate inputs
  if ((parityInput == "even" || parityInput == "odd" || parityInput == "none") &&
      (stopBitsValue == 1 || stopBitsValue == 2)) {
    saveSerialConfigToSD(parityInput, stopBitsValue);
    configureSerial1(parityInput, stopBitsValue);
    displayStatus("Serial config set.");
    delay(3000);
    Serial.println("Please reset the Arduino to apply changes.");
  } else {
    Serial.println("Invalid input. Try again.");
    displayStatus("Invalid input.");
    delay(3000);
  }
}


// Fungsi untuk menyimpan konfigurasi serial ke SD card
void saveSerialConfigToSD(const String& parity, int stopBits) {
  File dataFile;  // Deklarasi objek File lokal

  // Menghapus file jika sudah ada
  if (SD.exists("comm.txt")) {
    SD.remove("comm.txt");
  }

  // Membuka file untuk menulis
  dataFile = SD.open("comm.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.print("parity=");
    dataFile.println(parity);
    dataFile.print("stopBits=");
    dataFile.println(stopBits);
    dataFile.close();
    Serial.println("Serial config saved to SD card.");
  } else {
    Serial.println("Failed to open comm.txt for writing.");
  }
}


void loadSerialConfigFromSD(String &parity, int &stopBits) {
  File dataFile;  // Pastikan variabel File ada
  parity = "no";  // Default value
  stopBits = 1;   // Default value

  dataFile = SD.open("comm.txt", FILE_READ);
  if (dataFile) {
    while (dataFile.available()) {
      String line = dataFile.readStringUntil('\n');
      line.trim();  // Menghilangkan spasi kosong atau newline
      Serial.println("Membaca: " + line);  // Debug untuk melihat hasil yang dibaca

      // Cek apakah baris memiliki format yang benar
      if (line.startsWith("parity=")) {
        parity = line.substring(7);
        Serial.println("Parity ditemukan: " + parity);  // Debug
      } else if (line.startsWith("stopBits=")) {
        stopBits = line.substring(9).toInt();
        Serial.println("StopBits ditemukan: " + String(stopBits));  // Debug
      }
    }
    dataFile.close();
    Serial.println("Konfigurasi serial berhasil dimuat dari SD card.");
  } else {
    Serial.println("File comm.txt tidak ditemukan, menggunakan default.");
  }

  // Debug untuk memastikan nilai parity dan stopBits yang akan digunakan
  Serial.print("Menggunakan parity: ");
  Serial.println(parity);
  Serial.print("Menggunakan stopBits: ");
  Serial.println(stopBits);

  // Panggil fungsi untuk mengatur serial berdasarkan nilai yang sudah dibaca
  configureSerial1(parity, stopBits);
}

// Function to configure Serial1 based on loaded parity and stop bits
void configureSerial1(const String& parity, int stopBits) {
  uint32_t config = SERIAL_8N1;  // Default to 8 data bits, no parity, 1 stop bit

  if (parity == "even") {
    config = (stopBits == 1) ? SERIAL_8E1 : SERIAL_8E2;
  } else if (parity == "odd") {
    config = (stopBits == 1) ? SERIAL_8O1 : SERIAL_8O2;
  } else if (parity == "none") {  // Adjusted to "none" instead of "no"
    config = (stopBits == 1) ? SERIAL_8N1 : SERIAL_8N2;
  }

  Serial1.end();  // End the current configuration
  Serial1.begin(baudrate, config);  // Start Serial1 with the new settings
  Serial.println("Serial1 dikonfigurasi ulang.");
  Serial.print("Menggunakan parity: ");
  Serial.print(parity);
  Serial.print(", stopBits: ");
  Serial.println(stopBits);
}

// Function to load baudrate from SD card
uint32_t loadBaudrateFromSD() {
  uint32_t baudrate = 9600;  // Default baudrate
  dataFile = SD.open("baudrate.txt");
  if (dataFile) {
    String baudrateStr = dataFile.readStringUntil('\n');
    baudrate = baudrateStr.toInt();  // Convert string to int
    dataFile.close();
    Serial.println("Baudrate loaded from SD: " + String(baudrate));
  } else {
    Serial.println("Baudrate file not found. Using default baudrate.");
  }
  return baudrate;
}

// Function to update baudrate dynamically and save to SD
void updateBaudrate(uint32_t newBaudrate) {
  baudrate = newBaudrate;  // Update the baudrate variable
  Serial1.end();           // End current Serial1
  Serial1.begin(baudrate); // Restart Serial1 with the new baudrate
  Serial.println("Baudrate updated to: " + String(baudrate));
  displayStatus2("New Speed:" + String(baudrate));
  delay(3000);
  
}

// Function to save baudrate to SD card
void saveBaudrateToSD(uint32_t baudrate) {
  // Delete the existing file first
  if (SD.exists("baudrate.txt")) {
    SD.remove("baudrate.txt");  // Remove the file if it exists
  }
  
  // Open the file in write mode, which will create a new file
  dataFile = SD.open("baudrate.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.print(baudrate);  // Write the new baudrate value
    dataFile.close();
    Serial.println("Baudrate saved to SD card.");
  } else {
    Serial.println("Failed to open baudrate file for writing.");
  }
}

// Deklarasi flag untuk melacak status pengiriman data
bool dataSentFlag = false;

// Function to read and log process value (PV) and set value (SV)
void readAndLogData() {
  // Reading Process Value (PV)
  int16_t processValue = readModbusValue(pvAddress);
  if (processValue == (int16_t)-32768) {
    Serial.println("Failed to read PV.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Read PV failed");
  } else {
    Serial.print("Received process value: ");
    Serial.print(processValue);
    Serial.println(" °C");
  }

  // Reading Set Value (SV)
  int16_t setValue = readModbusValue(svAddress);
  if (setValue == (int16_t)-32768) {
    Serial.println("Failed to read SV.");
    lcd.setCursor(0, 1);
    lcd.print("Read SV failed");
  } else {
    Serial.print("Received set value: ");
    Serial.print(setValue);
    Serial.println(" °C");

    // Get current time
    DateTime now = rtc.now();

    // Update LCD with PV, SV, date, and time (limited to 14 characters)
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("PV:");
    lcd.print(processValue);
    lcd.print((char)223);  // Symbol derajat
    lcd.print("C SV:");
    lcd.print(setValue);
    lcd.print((char)223);  // Symbol derajat
    lcd.print("C");

    // Display date and time
    lcd.setCursor(0, 1);
    lcd.print(now.year());
    lcd.print('/');
    if (now.month() < 10) lcd.print('0');
    lcd.print(now.month());
    lcd.print('/');
    if (now.day() < 10) lcd.print('0');
    lcd.print(now.day());
    lcd.print(' ');
    if (now.hour() < 10) lcd.print('0');
    lcd.print(now.hour());
    lcd.print(':');
    if (now.minute() < 10) lcd.print('0');
    lcd.print(now.minute());

    // Output date and time to Serial Monitor
    Serial.print("Date: ");
    Serial.print(now.year());
    Serial.print("-");
    Serial.print(now.month());
    Serial.print("-");
    Serial.print(now.day());
    Serial.print(" Time: ");
    Serial.print(now.hour());
    Serial.print(":");
    Serial.print(now.minute());
    Serial.print(":");
    Serial.println(now.second());

    // Log data to SD card
    writeDataToSD(processValue, setValue, now);

    // Cek apakah flag untuk pengiriman data ke server aktif
    if (sendDataToServerFlag) {
      // Coba kirim data ke server (tanpa mengubah sendDataToServer)
      sendDataToServer(processValue, setValue, now);

      // Set flag indicating data sent status
      dataSentFlag = true;  // Set flag untuk menandakan data terkirim
    } else {
      dataSentFlag = false;  // Set flag untuk menandakan data tidak terkirim
    }

    // Tampilkan simbol sesuai status pengiriman
    if (dataSentFlag) {
      // // Hapus simbol sebelumnya di posisi 15,0
      // lcd.setCursor(15, 0);
      // lcd.print(" ");  // Tampilkan spasi kosong untuk membersihkan posisi
      // lcd.setCursor(15, 0);  // Set posisi kursor di LCD untuk simbol
      // lcd.print("V");  // Tampilkan simbol panah keatas jika data terkirim
    } else {
      // Hapus simbol sebelumnya di posisi 14,0
      lcd.setCursor(15, 0);
      lcd.print(" ");  // Tampilkan spasi kosong untuk membersihkan posisi
      lcd.setCursor(15, 0);  // Set posisi kursor di LCD untuk simbol
      lcd.print("X");  // Tampilkan simbol panah kebawah jika data tidak terkirim
    }
  }
}

// Fungsi untuk membaca konfigurasi alamat dari user 
void readAddressConfiguration() {
  Serial.print("Masukkan alamat controller (default 1): ");
  displayStatus2("Device address:");

  while (!Serial.available()) {
    // Menunggu input
  }
  String controllerInput = Serial.readStringUntil('\n');
  controllerInput.trim();

  // Ensure valid input
  if (controllerInput.length() > 0 && controllerInput.toInt() > 0) {
    controllerAddress = controllerInput.toInt();
  } else {
    Serial.println("Input tidak valid, menggunakan default.");
    controllerAddress = 1;  // Default value
  }

  // Display the entered controller address
  Serial.println("Alamat controller yang dimasukkan: " + String(controllerAddress));

  Serial.print("Masukkan alamat PV (default 0x03E8): ");
  displayStatus2("PV address:");
  while (!Serial.available()) {
    // Menunggu input
  }
  String pvInput = Serial.readStringUntil('\n');
  pvInput.trim();
  
  // Convert input to hexadecimal and validate
  if (pvInput.length() > 0) {
    pvAddress = (uint16_t)strtol(pvInput.c_str(), NULL, 16);  // Konversi dari hexadecimal
  } else {
    pvAddress = 0x03E8;  // Default value
  }

  // Display the entered PV address
  Serial.println("Alamat PV yang dimasukkan: 0x" + String(pvAddress, HEX));

  Serial.print("Masukkan alamat SV (default 0x03EB): ");
  displayStatus2("SV address:");
  while (!Serial.available()) {
    // Menunggu input
  }
  String svInput = Serial.readStringUntil('\n');
  svInput.trim();

  // Convert input to hexadecimal and validate
  if (svInput.length() > 0) {
    svAddress = (uint16_t)strtol(svInput.c_str(), NULL, 16);  // Konversi dari hexadecimal
  } else {
    svAddress = 0x03EB;  // Default value
  }

  // Display the entered SV address
  Serial.println("Alamat SV yang dimasukkan: 0x" + String(svAddress, HEX));

  Serial.println("Konfigurasi alamat selesai.");
}


// Fungsi untuk menyimpan konfigurasi alamat ke SD card
void saveAddressConfigToSD() {
  // Hapus file address.txt jika ada
  if (SD.exists("address.txt")) {
    SD.remove("address.txt");  // Menghapus file sebelum menulis ulang
  }

  dataFile = SD.open("address.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.println(controllerAddress);
    dataFile.println(pvAddress, HEX);
    dataFile.println(svAddress, HEX);
    dataFile.close();
    Serial.println("Alamat tersimpan.");
  } else {
    Serial.println("Gagal menyimpan alamat.");
  }
}

// Fungsi untuk memuat konfigurasi alamat dari SD card
bool loadAddressConfigFromSD() {
  if (SD.exists("address.txt")) {
    dataFile = SD.open("address.txt", FILE_READ);
    if (dataFile) {
      controllerAddress = dataFile.readStringUntil('\n').toInt();
      pvAddress = (uint16_t)strtol(dataFile.readStringUntil('\n').c_str(), NULL, 16);
      svAddress = (uint16_t)strtol(dataFile.readStringUntil('\n').c_str(), NULL, 16);
      dataFile.close();
      Serial.println("Konfigurasi alamat berhasil dimuat.");
      return true;
    } else {
      Serial.println("Gagal memuat konfigurasi alamat.");
    }
  } else {
    Serial.println("File konfigurasi alamat tidak ditemukan.");
  }
  return false;
}


// Function to read a Modbus register and return its value as a signed 16-bit integer
int16_t readModbusValue(uint16_t address) {
  // Read 1 register at the given address
  uint8_t result = node.readInputRegisters(address, 1);
  
  // Check if the read was successful
  if (result == node.ku8MBSuccess) {
    // Convert unsigned Modbus value to signed integer and return it
    return signedValue(node.getResponseBuffer(0));
  }

  // Handle error: if reading fails, return a specific error code
  Serial.print("Error reading Modbus register: ");
  Serial.println(result);  // Print error code for debugging
  
  return (int16_t)-32768;  // Return a distinct error value
}

// Function to convert unsigned 16-bit Modbus value to signed 16-bit integer
int16_t signedValue(uint16_t modbusValue) {
  // If the value is greater than 32767, treat it as a negative number
  if (modbusValue > 32767) {
    return modbusValue - 65536;  // Convert to negative value
  }
  return modbusValue;  // If it's less than 32767, return as is
}


// Function to reset RTC date and time based on user input
void resetRTCDateTime() {
  Serial.println("Masukkan tanggal dalam format YYYY-MM-DD:");
  displayStatus2("input YYYY-MM-DD");
  while (!Serial.available()) {
    // Waiting for input
  }
  String dateInput = Serial.readStringUntil('\n');
  dateInput.trim();
  
  // Display the entered date on the Serial Monitor
  Serial.println("Tanggal yang dimasukkan: " + dateInput);

  int year = dateInput.substring(0, 4).toInt();
  int month = dateInput.substring(5, 7).toInt();
  int day = dateInput.substring(8, 10).toInt();

  Serial.println("Masukkan waktu dalam format HH:MM:SS:");
  displayStatus2("input HH:MM:SS:");
  while (!Serial.available()) {
    // Waiting for input
  }
  String timeInput = Serial.readStringUntil('\n');
  timeInput.trim();
  
  // Display the entered time on the Serial Monitor
  Serial.println("Waktu yang dimasukkan: " + timeInput);

  int hour = timeInput.substring(0, 2).toInt();
  int minute = timeInput.substring(3, 5).toInt();
  int second = timeInput.substring(6, 8).toInt();

  // Set the RTC with new date and time
  rtc.adjust(DateTime(year, month, day, hour, minute, second));
  Serial.println("Tanggal dan waktu berhasil diatur ulang.");
}

// Function to log data to SD card
void writeDataToSD(int16_t processValue, int16_t setValue, DateTime now) {
  String dataString = String(now.year()) + "-" +
                      String(now.month()) + "-" +
                      String(now.day()) + "," +
                      String(now.hour()) + ":" +
                      String(now.minute()) + ":" +
                      String(now.second()) + "," +
                      String(processValue) + "," +
                      String(setValue);

  dataFile = SD.open("datalog.csv", FILE_WRITE);
  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
    Serial.println("Data written to SD card.");
  } else {
    Serial.println("Error opening datalog.csv.");
  }
}


// Function to send data to the server
void sendDataToServer(int16_t processValue, int16_t setValue, DateTime now) {
  EthernetClient client;

  // Prepare date and time for JSON
  char dateBuffer[11];
  snprintf(dateBuffer, sizeof(dateBuffer), "%04d-%02d-%02d", now.year(), now.month(), now.day());

  char timeBuffer[9];
  snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

  // Prepare JSON data
  DynamicJsonDocument jsonDoc(256);
  jsonDoc["tanggal"] = dateBuffer;
  jsonDoc["waktu"] = timeBuffer;
  jsonDoc["suhu"] = processValue;
  jsonDoc["sv"] = setValue;

  String jsonData;
  serializeJson(jsonDoc, jsonData);

  // Display the data being sent in a structured format
  Serial.println("==================================");
  Serial.println("         DATA YANG DIKIRIM        ");
  Serial.println("==================================");
  Serial.println("Tanggal       : " + String(dateBuffer));
  Serial.println("Waktu         : " + String(timeBuffer));
  Serial.println("Suhu          : " + String(processValue));
  Serial.println("Set Value (SV): " + String(setValue));
  Serial.println("==================================");
  Serial.println("JSON Format   :");
  Serial.println(jsonData);
  Serial.println("==================================");

  Serial.println("Connecting to server...");

  if (client.connect(server, port)) {
    Serial.println("Connected to server!");

    // Send HTTP POST request
    client.println("POST " + endpoint + " HTTP/1.1");
    client.print("Host: ");
    client.println(String(server[0]) + "." + String(server[1]) + "." + String(server[2]) + "." + String(server[3]));
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(jsonData.length());
    client.println();
    client.print(jsonData);

    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        Serial.println(">>> Client Timeout!");
        client.stop();
        lcd.setCursor(15, 0);
        lcd.print("X");
        return;
      }
    }

    // Read server response
    String responseBody;
    bool jsonStarted = false;

    while (client.available()) {
      char c = client.read();
      if (c == '{') {
        jsonStarted = true; // JSON body starts
      }
      if (jsonStarted) {
        responseBody += c;
      }
    }

    Serial.println("Server response (raw):");
    Serial.println(responseBody);

    // Parse JSON response
    DynamicJsonDocument responseDoc(512);
    DeserializationError error = deserializeJson(responseDoc, responseBody);
    if (error) {
      Serial.println("Failed to parse response JSON!");
      Serial.print("Error: ");
      Serial.println(error.c_str());
    } else {
      Serial.println("Parsed JSON response:");
      String prettyResponse;
      serializeJsonPretty(responseDoc, prettyResponse);
      Serial.println(prettyResponse);

      // Access specific JSON fields if needed
      const char* status = responseDoc["status"];
      const char* message = responseDoc["message"];
      int id = responseDoc["data"]["id"];
      Serial.println("Status: " + String(status));
      Serial.println("Message: " + String(message));
      Serial.println("ID: " + String(id));
    }

    lcd.setCursor(15, 0);
    lcd.print("V");
    client.stop();
  } else {
    Serial.println("Failed to connect to server.");
    lcd.setCursor(15, 0);
    lcd.print("X");
  }
}

// Function to read user input for IP, port, and endpoint
void readUserInput(bool initialSetup) {
  if (initialSetup) {
    Serial.println("Apakah Anda ingin mengirim data ke server? (y/n)");
  }

  // Wait for user input
  while (!Serial.available()) {
    // Waiting for input
  }

  String confirm = Serial.readStringUntil('\n');
  confirm.trim();  // Remove extra spaces

  if (confirm == "y" || confirm == "Y") {
    sendDataToServerFlag = true;

    Serial.println("Masukkan konfigurasi jaringan baru:");

    // Input IP Address
    Serial.print("IP server (mis. 192.168.1.100): ");
    displayStatus2("input IP address");
    String ipString = "";
    while (true) {
      if (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') break;
        Serial.print(c);  // Echo input character
        ipString += c;
      }
    }
    ipString.trim();

    // Validate IP
    while (!isValidIP(ipString)) {
      Serial.println("\nFormat IP tidak valid. Coba lagi.");
      Serial.print("IP server (mis. 192.168.1.100): ");
      ipString = "";
      while (true) {
        if (Serial.available()) {
          char c = Serial.read();
          if (c == '\n') break;
          Serial.print(c);  // Echo input character
          ipString += c;
        }
      }
      ipString.trim();
    }
    server.fromString(ipString);  // Set IP

    // Input Port
    Serial.print("\nPort server (mis. 80): ");
    displayStatus2("input IP Port");
    String portString = "";
    while (true) {
      if (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') break;
        Serial.print(c);  // Echo input character
        portString += c;
      }
    }
    portString.trim();
    port = portString.toInt();  // Convert string to int

    // Validate Port
    while (!isValidPort(port)) {
      Serial.println("\nPort tidak valid. Coba lagi.");
      Serial.print("Port server (mis. 80): ");
      portString = "";
      while (true) {
        if (Serial.available()) {
          char c = Serial.read();
          if (c == '\n') break;
          Serial.print(c);  // Echo input character
          portString += c;
        }
      }
      portString.trim();
      port = portString.toInt();  // Convert string to int
    }

    // Input Endpoint
    Serial.print("\nEndpoint server (mis. /api/data): ");
    displayStatus2("input Endpoint");
    endpoint = "";
    while (true) {
      if (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') break;
        Serial.print(c);  // Echo input character
        endpoint += c;
      }
    }
    endpoint.trim();

  } else {
    sendDataToServerFlag = false;
  }
}


// Function to save user input to SD card
void saveUserInputToSD() {
  // Hapus file network.txt jika ada
  if (SD.exists("network.txt")) {
    SD.remove("network.txt");  // Menghapus file sebelum menulis ulang
  }

  // Buka file dengan FILE_WRITE untuk menulis ulang
  dataFile = SD.open("network.txt", FILE_WRITE);

  if (dataFile) {
    dataFile.println(server);              // IP address
    dataFile.println(port);                // Port number
    dataFile.println(endpoint);            // Endpoint
    dataFile.println(sendDataToServerFlag); // Flag
    dataFile.close();
    Serial.println("Konfigurasi disimpan.");
  } else {
    Serial.println("Gagal membuka file untuk menulis.");
  }
}

// Function to load user input from SD card
bool loadUserInputFromSD() {
  if (SD.exists("network.txt")) {
    dataFile = SD.open("network.txt", FILE_READ);
    if (dataFile) {
      String ipString = dataFile.readStringUntil('\n');
      ipString.trim();  // Remove extra spaces
      if (!server.fromString(ipString)) {
        Serial.println("Format IP tidak valid.");
        dataFile.close();
        return false;  // IP format error
      }

      String portString = dataFile.readStringUntil('\n');
      portString.trim();  // Remove extra spaces
      port = portString.toInt();  // Convert string to int

      endpoint = dataFile.readStringUntil('\n');
      endpoint.trim();  // Remove extra spaces

      String flagString = dataFile.readStringUntil('\n');
      flagString.trim();  // Remove extra spaces
      sendDataToServerFlag = (flagString == "1");  // Convert to boolean

      dataFile.close();
      Serial.println("Konfigurasi jaringan dimuat dari SD card.");
      return true;  // Successful load
    } else {
      Serial.println("Gagal membuka file network.txt.");
      return false;  // Failed to open file
    }
  } else {
    Serial.println("File network.txt tidak ditemukan di SD card.");
    return false;  // File does not exist
  }
}


// Function to delete network configuration from SD card
void deleteConfigFromSD() {
  if (SD.exists("network.txt")) {
    if (SD.remove("network.txt")) {
      Serial.println("File network.txt dihapus.");
    } else {
      Serial.println("Gagal menghapus file.");
    }
  } else {
    Serial.println("File network.txt tidak ada.");
  }
}

// Function to check if IP address is valid
bool isValidIP(const String& ip) {
  int octets[4];
  int octetIndex = 0;
  int lastIndex = -1;

  for (int i = 0; i < 3; i++) {
    int dotIndex = ip.indexOf('.', lastIndex + 1);  // Find the next dot
    if (dotIndex == -1) {
      return false;  // Not enough dots
    }
    octets[octetIndex++] = ip.substring(lastIndex + 1, dotIndex).toInt();  // Extract octet
    lastIndex = dotIndex;
  }

  // Get the final octet after the last dot
  octets[octetIndex] = ip.substring(lastIndex + 1).toInt();

  // Validate each octet is within the range 0-255
  for (int i = 0; i < 4; i++) {
    if (octets[i] < 0 || octets[i] > 255) {
      return false;
    }
  }

  return true;
}


// Function to check if port number is valid
bool isValidPort(int port) {
  return (port > 0 && port <= 65535);
}

void setupModbus() {
  node.begin(controllerAddress, Serial1);  // Initialize Modbus with loaded controller address
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
  delay(100); 
}

void setupSDCard() {
  if (!SD.begin(SD_CS)) {
    Serial.println("Initialization of SD card failed.");
    while (1);
  }
  Serial.println("SD card initialized.");
}

void setupRTC() {
  if (!rtc.begin()) {
    Serial.println("RTC tidak ditemukan!");
    while (1);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC kehilangan daya, mengatur ulang waktu!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
}

void setupEthernet() {
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Ethernet shield tidak ditemukan. Kode akan tetap berjalan.");
    // Kamu bisa menambahkan penanganan kesalahan lainnya di sini jika perlu
    return; // Kembali tanpa melakukan hal lain
  }
  delay(1000);  // Allow some time for the Ethernet shield to initialize
  Serial.println("Ethernet shield diinisialisasi.");
}

