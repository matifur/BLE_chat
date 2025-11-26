# BLE Chat – projekt startowy (ESP32-C3 + ESP-IDF)

Projekt służy jako baza do budowy prostego **czatu tekstowego** między płytkami z ESP32-C3 (komunikacja BLE), z **interfejsem tekstowym po USB (Serial/JTAG)**.  
Ten README opisuje:

- jak sklonować repo i uruchomić projekt,
- jak ustawić konsolę w `menuconfig`,
- jak korzystać z programu (PuTTY / inne terminale),
- jakie API udostępnia komponent `chat` i jak go używać w swoim kodzie.

---

## 1. Klonowanie i otwarcie projektu w ESP-IDF

### 1.1. Klonowanie repozytorium

```bash
git clone https://github.com/matifur/BLE_chat.git
cd BLE_chat
```

Projekt jest standardowym projektem ESP-IDF – **nie trzeba niczego generować**.

### 1.2. VS Code

Jeśli korzystasz z rozszerzenia **Espressif IDF** w VS Code:

1. Wybierz **File → Open Folder…** i wskaż katalog z projektem.
2. Z lewego paska wybierz odpowiedni **Target: esp32c3**.
3. Wybierz select port to use na przykład **COM5**
4. Wybierz select flash method **UART**
5. Potem możesz używać przycisków „Build”, „Flash”, „Monitor” bezpośrednio w VS Code.

W razie problemów napisz do mnie XD

---

## 2. Konfiguracja konsoli w `menuconfig`

Projekt zakłada, że komunikacja z płytką odbywa się przez **USB Serial/JTAG** w ESP32-C3 (ten sam port, którego używa `idf.py flash`).

Możesz ustawić to w `menuconfig`:

Z poziomu terminala ESP (znowu lewy pasek)

```bash
idf.py menuconfig
```

Ścieżka w menu:

> **Component config → ESP System Settings → Channel for console output**

Ustaw:

- `Channel for console output` → **USB Serial/JTAG Controller**

Drugi kanał konsoli (`Channel for console secondary output`) zostaw jako „No secondary console”.

Zapisz konfigurację (`S`) i wyjdź (`Q`).

---

## 3. Budowanie, flashowanie i monitor

1. Budowanie:

   ```bash
   idf.py build
   ```

2. Flashowanie (podmień `COMx` na swój port – ten sam, którego używasz do flashowania):

   ```bash
   idf.py -p COMx flash
   ```

3. Monitor (terminal ESP-IDF – przydatny do logów/debugowania):

   ```bash
   idf.py -p COMx monitor
   ```

> Uwaga: `flash monitor` można wykonać jedną komendą:  
> `idf.py -p COMx flash monitor`

---

## 4. Używanie programu – interfejs czatu

Program udostępnia prosty „czat” w terminalu szeregowym. Po starcie zobaczysz np.:

```text
--- Simple chat echo demo ---
Wpisz linie tekstu i nacisnij Enter.
Kazda linia zostanie wyswietlona jako WIADOMOSC Z ZEWNATRZ.

>
```

### 4.1. Parametry połączenia szeregowego

Jeśli korzystasz z zewnętrznego terminala (PuTTY / CoolTerm / itp.):

- **Port**: ten sam, którego używasz w `idf.py -p COMx`  
- **Baud rate**: `115200`
- **Data bits**: `8`
- **Parity**: `None`
- **Stop bits**: `1`
- **Flow control**: **None**

### 4.2. Rekomendowane ustawienia PuTTY

1. **Session → Serial**  
   - Serial line: `COMx`  
   - Speed: `115200`

2. **Terminal → Local echo**: `Force on`  
3. **Terminal → Local line editing**: `Force on`  
4. **Terminal → Keyboard → The Return key**: `CR` (lub `CR+LF` – obie opcje są OK).

Dzięki temu:

- to, co wpisujesz, widać od razu w oknie,
- Backspace działa jak w zwykłej konsoli (usuwa znak po lewej),
- ESP dostaje gotową linię tekstu dopiero po wciśnięciu Enter.

> **Ważne:** Nie możesz mieć jednocześnie otwartego `idf.py monitor` i PuTTY na tym samym porcie. Zawsze zamknij jedno narzędzie przed otwarciem drugiego.

### 4.3. Zachowanie programu

- Po pokazaniu zachęty `>` wpisujesz linię tekstu i naciskasz **Enter**.
- Program traktuje ją jako „wiadomość z zewnątrz” i wyświetla w formacie:

  ```text
  [IN ][--------][REMOTE ]: Twoja wiadomosc
  >
  ```

Później, kiedy BLE i NTP będą zaimplementowane:

- wiadomości z innych płytek będą wyświetlane w tym samym formacie,
- `--------` zastąpi aktualny czas (NTP),
- `"REMOTE"` zostanie zastąpione nazwą nadawcy.

---

## 5. Struktura projektu

Ważniejsze elementy projektu:

```text
.
├── CMakeLists.txt           # główny plik CMake
├── main
│   ├── main.c               # przykład wykorzystania interfejsu czatu
│   └── CMakeLists.txt
└── components
    └── chat
        ├── chat.c           # implementacja warstwy I/O czatu
        ├── chat.h           # publiczne API komponentu
        └── CMakeLists.txt
```

Komponent `chat` jest przeznaczony do ponownego użycia:

- z `main`,
- z modułem BLE,
- z modułem NTP itd.

---

## 6. API komponentu `chat`

Plik nagłówkowy: `components/chat/chat.h`

### 6.1. Typy

```c
typedef enum {
    CHAT_DIR_LOCAL = 0,  // wiadomość lokalna (np. od użytkownika)
    CHAT_DIR_REMOTE,     // wiadomość zdalna (np. z BLE)
} chat_direction_t;

typedef struct {
    chat_direction_t direction;  // kierunek: lokalna / zdalna
    const char *sender;          // nazwa nadawcy ("YOU", "NODE_A", itp.) – może być NULL
    const char *timestamp;       // np. "12:34:56" z NTP – może być NULL
    const char *text;            // treść wiadomości (wymagane)
} chat_message_t;
```

### 6.2. Funkcje

#### `esp_err_t chat_io_init(void);`

- Wołana **raz na starcie** (np. w `app_main()`).
- Inicjalizuje warstwę I/O czatu (USB Serial/JTAG).
- Zwraca `ESP_OK` lub kod błędu (`esp_err_t`).

#### `esp_err_t chat_io_read_line(char *buf, size_t buf_len, TickType_t timeout);`

- Czyta **jedną linię tekstu** od użytkownika (po USB Serial/JTAG).
- Parametry:
  - `buf` – bufor na tekst (musi mieć co najmniej 2 bajty),
  - `buf_len` – rozmiar bufora,
  - `timeout` – maksymalny czas oczekiwania (np. `portMAX_DELAY` – czekaj w nieskończoność).
- Zachowanie:
  - obsługuje **Backspace** (kasuje znak w buforze, niezależnie od terminala),
  - kończy linię na `CR` lub `LF`,
  - tekst jest zawsze zakończony `' '` (C-string).
- Zwraca:
  - `ESP_OK` – gdy linia została poprawnie wczytana,
  - `ESP_ERR_TIMEOUT` – jeśli upłynął timeout, a nie wprowadzono żadnych danych,
  - inne kody błędu (`ESP_FAIL` itd.) – przy problemach z interfejsem.

#### `void chat_io_print_message(const chat_message_t *msg);`

- Formatuje i wyświetla wiadomość w terminalu w postaci:

  ```text
  [IN ][12:34:56][NODE_A ]: Treść
  [OUT][--------][YOU    ]: Treść
  ```

- Używana zarówno przez moduł BLE (do logowania wiadomości zdalnych), jak i lokalnie – przy testach.

#### `void chat_io_print_prompt(void);`

- Wyświetla prostą zachętę `> `.
- Można wołać przed kolejnym `chat_io_read_line()` (tak jak w przykładowym `main.c`).

