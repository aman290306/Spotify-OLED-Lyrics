#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>      

constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 64;
constexpr int OLED_RESET = -1;
constexpr uint8_t OLED_ADDRESS = 0x3C;

constexpr unsigned long UPDATE_INTERVAL_MS = 1000;
String loadedSong = "";
String loadedArtist = "";
String loadedAlbum = "";
unsigned long displayedLyricTimestamp =
    0xFFFFFFFFUL;
constexpr unsigned long LYRIC_SYNC_OFFSET_MS = 4500;
bool lyricsLoaded = false;
bool previousNowPlaying = false;
const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";

const char* LASTFM_USERNAME = "";
const char* LASTFM_API_KEY =
    "";

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    OLED_RESET
);

unsigned long lastUpdateTime = 0;
constexpr int LYRIC_QUEUE_SIZE = 10;

struct LyricEntry {
    unsigned long timestamp;
    String text;
};

LyricEntry lyricQueue[LYRIC_QUEUE_SIZE];

int queueHead = 0;
int queueCount = 0;

String rawLyrics = "";
int rawLyricsPosition = 0;

unsigned long songStartTime = 0;
bool songTimerRunning = false;

void resetLyrics() {
    displayedLyricTimestamp = 0xFFFFFFFFUL;
    for (int i = 0; i < LYRIC_QUEUE_SIZE; i++) {
        lyricQueue[i].timestamp = 0;
        lyricQueue[i].text = "";
    }

    queueHead = 0;
    queueCount = 0;

    rawLyrics = "";
    rawLyricsPosition = 0;

    songStartTime = 0;
    songTimerRunning = false;
}
bool enqueueLyric(
    unsigned long timestamp,
    const String& text
) {
    if (queueCount >= LYRIC_QUEUE_SIZE) {
        return false;
    }

    int queueTail =
        (queueHead + queueCount) %
        LYRIC_QUEUE_SIZE;

    lyricQueue[queueTail].timestamp = timestamp;
    lyricQueue[queueTail].text = text;

    queueCount++;

    return true;
}
bool dequeueLyric() {
    if (queueCount == 0) {
        return false;
    }

    lyricQueue[queueHead].timestamp = 0;
    lyricQueue[queueHead].text = "";

    queueHead =
        (queueHead + 1) %
        LYRIC_QUEUE_SIZE;

    queueCount--;

    return true;
}
LyricEntry* getQueuedLyric(int offset) {
    if (offset < 0 || offset >= queueCount) {
        return nullptr;
    }

    int index =
        (queueHead + offset) %
        LYRIC_QUEUE_SIZE;

    return &lyricQueue[index];
}
// --------------------------------------------------
// Convert unsupported UTF-8 characters to ASCII
// --------------------------------------------------
String makeOLEDCompatible(const String& input) {
    String output;

    for (unsigned int i = 0; i < input.length(); i++) {
        uint8_t current = (uint8_t)input[i];

        // Accented characters beginning with C3
        if (
            current == 0xC3 &&
            i + 1 < input.length()
        ) {
            uint8_t next = (uint8_t)input[++i];

            switch (next) {
                // Uppercase A
                case 0x80:
                case 0x81:
                case 0x82:
                case 0x83:
                case 0x84:
                case 0x85:
                    output += 'A';
                    break;

                // Uppercase C
                case 0x87:
                    output += 'C';
                    break;

                // Uppercase E
                case 0x88:
                case 0x89:
                case 0x8A:
                case 0x8B:
                    output += 'E';
                    break;

                // Uppercase I
                case 0x8C:
                case 0x8D:
                case 0x8E:
                case 0x8F:
                    output += 'I';
                    break;

                // Uppercase N
                case 0x91:
                    output += 'N';
                    break;

                // Uppercase O
                case 0x92:
                case 0x93:
                case 0x94:
                case 0x95:
                case 0x96:
                    output += 'O';
                    break;

                // Uppercase U
                case 0x99:
                case 0x9A:
                case 0x9B:
                case 0x9C:
                    output += 'U';
                    break;

                // Lowercase a
                case 0xA0:
                case 0xA1:
                case 0xA2:
                case 0xA3:
                case 0xA4:
                case 0xA5:
                    output += 'a';
                    break;

                // Lowercase c
                case 0xA7:
                    output += 'c';
                    break;

                // Lowercase e
                case 0xA8:
                case 0xA9:
                case 0xAA:
                case 0xAB:
                    output += 'e';
                    break;

                // Lowercase i
                case 0xAC:
                case 0xAD:
                case 0xAE:
                case 0xAF:
                    output += 'i';
                    break;

                // Lowercase n
                case 0xB1:
                    output += 'n';
                    break;

                // Lowercase o
                case 0xB2:
                case 0xB3:
                case 0xB4:
                case 0xB5:
                case 0xB6:
                    output += 'o';
                    break;

                // Lowercase u
                case 0xB9:
                case 0xBA:
                case 0xBB:
                case 0xBC:
                    output += 'u';
                    break;

                default:
                    output += '?';
                    break;
            }
        }

        // UTF-8 punctuation beginning with E2 80
        else if (
            current == 0xE2 &&
            i + 2 < input.length() &&
            (uint8_t)input[i + 1] == 0x80
        ) {
            uint8_t punctuation =
                (uint8_t)input[i + 2];

            switch (punctuation) {
                // ‘ and ’
                case 0x98:
                case 0x99:
                    output += '\'';
                    break;

                // “ and ”
                case 0x9C:
                case 0x9D:
                    output += '"';
                    break;

                // En dash and em dash
                case 0x93:
                case 0x94:
                    output += '-';
                    break;

                // Ellipsis
                case 0xA6:
                    output += "...";
                    break;

                default:
                    output += '?';
                    break;
            }

            i += 2;
        }

        // Standard ASCII
        else if (current < 128) {
            output += (char)current;
        }

        // Unknown unsupported character
        else {
            output += '?';
        }
    }

    return output;
}
bool parseLyricLine(
    const String& line,
    unsigned long& timestamp,
    String& lyricText
) {
    // Valid lines begin like: [00:12.34]
    if (
        line.length() < 10 ||
        line[0] != '[' ||
        !isDigit(line[1])
    ) {
        return false;
    }

    int colonPosition = line.indexOf(':');
    int bracketPosition = line.indexOf(']');

    if (
        colonPosition < 0 ||
        bracketPosition < 0 ||
        bracketPosition <= colonPosition
    ) {
        return false;
    }

    int minutes =
        line.substring(1, colonPosition).toInt();

    float seconds =
        line.substring(
            colonPosition + 1,
            bracketPosition
        ).toFloat();

    timestamp =
        minutes * 60000UL +
        (unsigned long)(seconds * 1000.0F);

    lyricText =
        line.substring(bracketPosition + 1);

    lyricText.trim();

    if (lyricText.length() == 0) {
        lyricText = "...";
    }

    lyricText =
        makeOLEDCompatible(lyricText);

    return true;
}
bool enqueueNextRawLyric() {
    // Do not parse another line if the queue is full
    if (queueCount >= LYRIC_QUEUE_SIZE) {
        return false;
    }

    while (rawLyricsPosition < rawLyrics.length()) {
        int lineEnd =
            rawLyrics.indexOf(
                '\n',
                rawLyricsPosition
            );

        // Final line may not end with \n
        if (lineEnd < 0) {
            lineEnd = rawLyrics.length();
        }

        String rawLine =
            rawLyrics.substring(
                rawLyricsPosition,
                lineEnd
            );

        rawLyricsPosition = lineEnd + 1;

        unsigned long timestamp;
        String lyricText;

        if (
            parseLyricLine(
                rawLine,
                timestamp,
                lyricText
            )
        ) {
            return enqueueLyric(
                timestamp,
                lyricText
            );
        }
    }

    // No more valid lyric lines remain
    return false;
}
void fillLyricQueue() {
    while (queueCount < LYRIC_QUEUE_SIZE) {
        if (!enqueueNextRawLyric()) {
            break;
        }
    }

    Serial.print("Lyrics in queue: ");
    Serial.println(queueCount);
}
String urlEncode(const String& text) {
    String encoded = "";
    char buffer[4];

    for (unsigned int i = 0; i < text.length(); i++) {
        uint8_t character =
            (uint8_t)text[i];

        if (
            isalnum(character) ||
            character == '-' ||
            character == '_' ||
            character == '.' ||
            character == '~'
        ) {
            encoded += (char)character;
        } else {
            snprintf(
                buffer,
                sizeof(buffer),
                "%%%02X",
                character
            );

            encoded += buffer;
        }
    }

    return encoded;
}
// --------------------------------------------------
// Display up to two lines
// --------------------------------------------------
void displayTwoLines(
    const String& text,
    int firstLineY
) {
    constexpr int CHARACTERS_PER_LINE = 21;

    display.setCursor(0, firstLineY);

    if (text.length() <= CHARACTERS_PER_LINE) {
        display.println(text);
        return;
    }

    display.println(
        text.substring(0, CHARACTERS_PER_LINE)
    );

    display.setCursor(0, firstLineY + 8);

    display.println(
        text.substring(
            CHARACTERS_PER_LINE,
            CHARACTERS_PER_LINE * 2
        )
    );
}

// --------------------------------------------------
// Display song information
// --------------------------------------------------
void showSong(
    const String& song,
    const String& artist,
    bool nowPlaying
) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextWrap(false);

    display.setCursor(0, 0);

    if (nowPlaying) {
        display.println("NOW PLAYING");
    } else {
        display.println("LAST PLAYED");
    }

    display.drawLine(
        0,
        10,
        SCREEN_WIDTH - 1,
        10,
        SSD1306_WHITE
    );

    displayTwoLines(song, 14);

    display.setCursor(0, 34);
    display.println("ARTIST");

    displayTwoLines(artist, 44);

    display.display();
}
void showLyric(const String& lyric) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextWrap(false);

    constexpr int CHARACTERS_PER_LINE = 21;

    int position = 0;

    for (int line = 0; line < 4; line++) {
        if (position >= lyric.length()) {
            break;
        }

        int endPosition =
            min(
                position + CHARACTERS_PER_LINE,
                (int)lyric.length()
            );

        // Avoid splitting a word where possible
        if (endPosition < lyric.length()) {
            int spacePosition =
                lyric.lastIndexOf(
                    ' ',
                    endPosition
                );

            if (spacePosition > position) {
                endPosition = spacePosition;
            }
        }

        String displayLine =
            lyric.substring(
                position,
                endPosition
            );

        displayLine.trim();

        display.setCursor(0, line * 16);
        display.println(displayLine);

        position = endPosition;

        while (
            position < lyric.length() &&
            lyric[position] == ' '
        ) {
            position++;
        }
    }

    display.display();
}
void updateLyricPlayback() {
    if (
        !lyricsLoaded ||
        !songTimerRunning ||
        queueCount == 0
    ) {
        return;
    }

    unsigned long elapsedTime =
    millis() - songStartTime +
    LYRIC_SYNC_OFFSET_MS;

    // Advance while the next lyric's time has arrived
    while (queueCount > 1) {
        LyricEntry* nextLyric =
            getQueuedLyric(1);

        if (
            nextLyric == nullptr ||
            elapsedTime < nextLyric->timestamp
        ) {
            break;
        }

        // Remove the completed current lyric
        dequeueLyric();

        // Add the next lyric from rawLyrics
        enqueueNextRawLyric();
    }

    LyricEntry* currentLyric =
        getQueuedLyric(0);

    if (currentLyric == nullptr) {
        return;
    }

    if (
        elapsedTime >= currentLyric->timestamp &&
        displayedLyricTimestamp !=
            currentLyric->timestamp
    ) {
        displayedLyricTimestamp =
            currentLyric->timestamp;

        showLyric(currentLyric->text);
    }
}
// --------------------------------------------------
// Display a message
// --------------------------------------------------
void showMessage(const String& message) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextWrap(true);
    display.setCursor(0, 0);
    display.println(message);
    display.display();
}

// --------------------------------------------------
// Forward declaration because the function is defined later
bool downloadLyrics(
    const String& song,
    const String& artist,
    const String& album
);

// Read one complete JSON object from a larger JSON array.
// Braces inside quoted lyric text are ignored.
bool readNextJsonObject(
    Stream& stream,
    String& objectJson,
    unsigned long timeoutMs
) {
    objectJson = "";
    objectJson.reserve(16000);

    bool objectStarted = false;
    bool insideString = false;
    bool escaped = false;
    int objectDepth = 0;
    unsigned long lastDataTime = millis();

    while (millis() - lastDataTime < timeoutMs) {
        while (stream.available()) {
            char character = (char)stream.read();
            lastDataTime = millis();

            if (!objectStarted) {
                if (character != '{') {
                    continue;
                }

                objectStarted = true;
                objectDepth = 1;
                objectJson += character;
                continue;
            }

            objectJson += character;

            if (insideString) {
                if (escaped) {
                    escaped = false;
                } else if (character == '\\') {
                    escaped = true;
                } else if (character == '"') {
                    insideString = false;
                }

                continue;
            }

            if (character == '"') {
                insideString = true;
            } else if (character == '{') {
                objectDepth++;
            } else if (character == '}') {
                objectDepth--;

                if (objectDepth == 0) {
                    return true;
                }
            }
        }

        delay(1);
    }

    return false;
}

// Request latest track from Last.fm
// --------------------------------------------------
void updateSpotifySong() {
    if (WiFi.status() != WL_CONNECTED) {
        showMessage("WiFi disconnected");
        return;
    }

    String url =
        "https://ws.audioscrobbler.com/2.0/"
        "?method=user.getrecenttracks"
        "&user=" + String(LASTFM_USERNAME) +
        "&api_key=" + String(LASTFM_API_KEY) +
        "&format=json"
        "&limit=1"
        "&cache=" + String(millis());

    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    http.setTimeout(5000);

    if (!http.begin(secureClient, url)) {
        showMessage("Request failed");
        return;
    }

    int responseCode = http.GET();

    if (responseCode != HTTP_CODE_OK) {
        Serial.print("HTTP error: ");
        Serial.println(responseCode);

        showMessage(
            "Last.fm error: " +
            String(responseCode)
        );

        http.end();
        return;
    }

    String response = http.getString();
    http.end();

    JsonDocument document;

    DeserializationError error =
        deserializeJson(document, response);

    if (error) {
        Serial.print("JSON error: ");
        Serial.println(error.c_str());

        showMessage("Invalid JSON");
        return;
    }

    JsonObject track =
        document["recenttracks"]["track"][0];

    if (track.isNull()) {
        showMessage("No tracks found");
        return;
    }

    String originalSong =
        track["name"] | "Unknown song";

    String originalArtist =
        track["artist"]["#text"] | "Unknown artist";

    String originalAlbum =
        track["album"]["#text"] | "";

    String oledSong =
        makeOLEDCompatible(originalSong);

    String oledArtist =
        makeOLEDCompatible(originalArtist);

    bool nowPlaying =
        track["@attr"]["nowplaying"] == "true";
    bool songChanged =
    originalSong != loadedSong ||
    originalArtist != loadedArtist ||
    originalAlbum != loadedAlbum;

bool playbackStarted =
    nowPlaying && !previousNowPlaying;

// A different song started
if (songChanged) {
    loadedSong = originalSong;
    loadedArtist = originalArtist;
    loadedAlbum = originalAlbum;

    if (nowPlaying) {
        lyricsLoaded =
            downloadLyrics(
                originalSong,
                originalArtist,
                originalAlbum
            );
    } else {
        resetLyrics();
        lyricsLoaded = false;
    }
}

// The same song resumed after being stopped
else if (playbackStarted) {
    lyricsLoaded =
        downloadLyrics(
            originalSong,
            originalArtist,
            originalAlbum
        );
}

// Playback stopped
else if (!nowPlaying && previousNowPlaying) {
    resetLyrics();
    lyricsLoaded = false;
}

previousNowPlaying = nowPlaying;
    // Serial keeps the correct UTF-8 spelling
    Serial.print(
        nowPlaying
            ? "Now playing: "
            : "Last played: "
    );

    Serial.print(originalSong);
    Serial.print(" - ");
    Serial.println(originalArtist);

    // OLED receives the compatible spelling
    if (!lyricsLoaded || !nowPlaying || songChanged) {
    showSong(
        oledSong,
        oledArtist,
        nowPlaying
    );
}
}
bool downloadLyrics(
    const String& song,
    const String& artist,
    const String& album
) {
    resetLyrics();

    String url =
        "https://lrclib.net/api/search"
        "?track_name=" + urlEncode(song) +
        "&artist_name=" + urlEncode(artist) +
        "&album_name=" + urlEncode(album);

    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;

// HTTP/1.0 avoids problems with chunked responses
http.useHTTP10(true);

// Allow more time for large lyric responses
http.setTimeout(20000);

http.setUserAgent("ESP32SpotifyOLED/1.0");
    if (!http.begin(secureClient, url)) {
        Serial.println(
            "Could not connect to LRCLIB"
        );

        return false;
    }

    int responseCode = http.GET();

    if (responseCode != HTTP_CODE_OK) {
        Serial.print("LRCLIB HTTP error: ");
        Serial.println(responseCode);

        http.end();
        return false;
    }
    Stream& responseStream = http.getStream();
    bool foundSyncedLyrics = false;

    // Inspect results one at a time instead of downloading the
    // complete search array into ESP32 memory.
    for (int resultNumber = 0; resultNumber < 10; resultNumber++) {
        String objectJson;

        if (!readNextJsonObject(
                responseStream,
                objectJson,
                20000
            )) {
            break;
        }

        JsonDocument filter;
        filter["syncedLyrics"] = true;

        JsonDocument result;

        DeserializationError error =
            deserializeJson(
                result,
                objectJson,
                DeserializationOption::Filter(filter)
            );

        if (error) {
            Serial.print("Lyrics result JSON error: ");
            Serial.println(error.c_str());
            continue;
        }

        const char* syncedLyrics =
            result["syncedLyrics"];

        if (
            syncedLyrics != nullptr &&
            strlen(syncedLyrics) > 0
        ) {
            rawLyrics = String(syncedLyrics);
            foundSyncedLyrics = true;
            break;
        }
    }

    http.end();

    if (foundSyncedLyrics) {
        rawLyricsPosition = 0;
        fillLyricQueue();

        songStartTime = millis();
        songTimerRunning = true;

        Serial.println("Synced lyrics loaded");

        return queueCount > 0;
    }

    Serial.println(
        "No synchronized lyrics found"
    );

    return false;
}
// --------------------------------------------------
// Setup
// --------------------------------------------------
void setup() {
    Serial.begin(115200);

    Wire.begin(21, 22);

    if (!display.begin(
            SSD1306_SWITCHCAPVCC,
            OLED_ADDRESS
        )) {
        Serial.println(
            "OLED initialization failed"
        );

        while (true) {
            delay(100);
        }
    }

    showMessage("Connecting WiFi...");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi connected");

    showMessage("WiFi connected");

    updateSpotifySong();
    lastUpdateTime = millis();
}

// --------------------------------------------------
// Main loop
// --------------------------------------------------
void loop() {
    unsigned long currentTime = millis();

    if (
        currentTime - lastUpdateTime >=
        UPDATE_INTERVAL_MS
    ) {
        lastUpdateTime = currentTime;
        updateSpotifySong();
    }

    // Must remain outside the interval block
    updateLyricPlayback();
}
