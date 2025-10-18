# 🌡️ IoT Modbus Logger with RS485, Ethernet, SD Card, and LCD Display

Proyek ini merupakan **sistem logging data Modbus RTU berbasis Arduino Mega** yang dilengkapi dengan **komunikasi RS485**, **penyimpanan data ke microSD**, **sinkronisasi waktu melalui RTC DS3231**, serta **pengiriman data ke server via Ethernet**.  
Pengguna dapat **memulai dan menghentikan proses logging** menggunakan dua push button, serta memantau data PV dan SV melalui **LCD 16x2 I2C**.

---

## 🧩 Fitur Utama

| Fitur | Deskripsi |
|-------|------------|
| 🔌 **Modbus RTU via RS485** | Membaca data PV (Process Value) dan SV (Set Value) dari perangkat Modbus slave. |
| 💾 **Logging ke SD Card** | Data PV, SV, tanggal, dan waktu disimpan dalam format CSV ke microSD card. |
| 🌐 **Ethernet Connection** | Mengirimkan data log ke server dengan alamat IP dan endpoint yang dapat dikonfigurasi. |
| ⏰ **RTC DS3231** | Menyediakan waktu dan tanggal yang akurat untuk setiap log. |
| 📟 **LCD 16x2 I2C** | Menampilkan status sistem dan data PV/SV secara real-time. |
| 🔘 **Push Button Control** | Tombol A = Start logging, Tombol B = Stop logging. |
| ⚙️ **Configurable Parameters** | Pengaturan IP, port, endpoint, parity, stop bits, dan baudrate tersimpan di SD Card. |
| 🔄 **Baudrate & Serial Config Load** | Sistem membaca konfigurasi serial dari SD card setiap kali startup. |

---

## 🧠 Arsitektur Sistem

```
+-------------------------------------------------------------+
|                       Arduino Mega 2560                     |
|-------------------------------------------------------------|
| RS485 (MAX485 Module)  --> Komunikasi Modbus RTU            |
| RTC DS3231             --> Pencatatan waktu log             |
| SD Card Module         --> Penyimpanan konfigurasi & log     |
| Ethernet Shield (W5100/W5500) --> Pengiriman data ke server  |
| LCD 16x2 I2C           --> Tampilan status dan data          |
| Push Button A/B        --> Kontrol start/stop logging        |
+-------------------------------------------------------------+
```

---

## ⚙️ Konfigurasi Pin

| Komponen | Pin Arduino | Keterangan |
|-----------|--------------|------------|
| MAX485 DE/RE | 8 | Mengatur mode TX/RX RS485 |
| SD Card CS | 4 | Chip Select microSD |
| Push Button A | 2 | Start logging (INPUT_PULLUP) |
| Push Button B | 3 | Stop logging (INPUT_PULLUP) |
| LCD I2C | SDA/SCL | Komunikasi I2C (0x27) |
| Modbus TX | TX1 (Pin 18) | Data keluar ke RS485 |
| Modbus RX | RX1 (Pin 19) | Data masuk dari RS485 |

---

## 🗂️ Struktur File Konfigurasi di SD Card

| File | Isi |
|------|-----|
| `network.cfg` | IP Address, Port, Endpoint |
| `serial.cfg` | Baudrate, Parity, StopBits |
| `address.cfg` | Modbus Controller Address, PV Address, SV Address |
| `interval.cfg` | Interval logging dalam milidetik |

---

## 🖥️ Tampilan di LCD

| Kondisi | Tampilan |
|----------|-----------|
| Saat startup | `Inisialisasi...` |
| Saat tombol A ditekan | `Logging Started` |
| Saat tombol B ditekan | `Logging Stopped` |
| Saat berjalan | `PV=XXC SV=YYC` + tanggal/waktu di baris kedua |

---

## 🔘 Cara Penggunaan

1. **Persiapkan perangkat keras:**
   - Arduino Mega + Ethernet Shield (W5100/W5500)
   - MAX485 module untuk RS485
   - RTC DS3231
   - microSD card
   - LCD 16x2 I2C
   - Dua push button

2. **Pasang library yang dibutuhkan di Arduino IDE:**
   - `ModbusMaster`
   - `SD`
   - `RTClib`
   - `Ethernet`
   - `ArduinoJson`
   - `LiquidCrystal_I2C`

3. **Upload kode ke Arduino Mega.**

4. **Siapkan file konfigurasi di SD card (opsional)** atau biarkan sistem membuat konfigurasi default.

5. **Operasikan perangkat:**
   - Tekan tombol **A** → memulai logging
   - Tekan tombol **B** → menghentikan logging
   - Data akan tersimpan di SD card dan/atau dikirim ke server.

---

## 🧾 Format Data Log (di SD Card)

Contoh isi file log di SD card:

```
2025-10-18, 14:30:15, PV=27.6, SV=30.0
2025-10-18, 14:30:25, PV=27.8, SV=30.0
```

---

## 🌐 Format Data yang Dikirim ke Server

```json
{
  "tanggal": "2025-10-18",
  "waktu": "14:30:25",
  "suhu": 27.6,
  "sv": 30.0
}
```

---

## ⚠️ Catatan Tambahan

- Jika SD card tidak terdeteksi, sistem akan tetap berjalan tanpa logging lokal.
- Jika Ethernet shield tidak ditemukan, sistem hanya akan menyimpan data ke SD card.
- Semua konfigurasi (IP, baudrate, parity, stop bits, dsb.) **tersimpan di SD card**, sehingga akan tetap berlaku setelah restart/power-off.
- Interval logging dapat diubah melalui file konfigurasi `interval.cfg`.

---

## 🧑‍💻 Author

**Khairunas Rhamadhani Wiasanto**  
📘 Universitas Gadjah Mada — Electronics & Instrumentation  
💡 Fokus: Backend Development, IoT, Blockchain, dan Cloud Maintenance  
