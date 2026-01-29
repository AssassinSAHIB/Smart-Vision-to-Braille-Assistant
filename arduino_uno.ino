#include <SoftwareSerial.h>
#include <Servo.h>

SoftwareSerial espIn(2, 3); // RX=D2, TX=D3
String line;

// Servo objects
Servo dots[6]; 
const int servoPins[6] = {4, 5, 6, 7, 8, 9}; // Pins for Dot 1, 2, 3, 4, 5, 6

// Define angles (Adjust these based on your mechanical assembly)
const int DOT_UP = 90;   // Angle when dot is raised
const int DOT_DOWN = 0;  // Angle when dot is flat

void setup() {
  Serial.begin(115200);
  espIn.begin(9600);

  // Initialize Servos
  for (int i = 0; i < 6; i++) {
    dots[i].attach(servoPins[i]);
    dots[i].write(DOT_DOWN); // Start all dots down
  }

  Serial.println("UNO ready. Servos Initialized. Waiting for TXT:...");
}

uint8_t brailleLetterPattern(char c) {
  if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  switch (c) {
    case 'a': return (1<<0);
    case 'b': return (1<<0) | (1<<1);
    case 'c': return (1<<0) | (1<<3);
    case 'd': return (1<<0) | (1<<3) | (1<<4);
    case 'e': return (1<<0) | (1<<4);
    case 'f': return (1<<0) | (1<<1) | (1<<3);
    case 'g': return (1<<0) | (1<<1) | (1<<3) | (1<<4);
    case 'h': return (1<<0) | (1<<1) | (1<<4);
    case 'i': return (1<<1) | (1<<3);
    case 'j': return (1<<1) | (1<<3) | (1<<4);
    case 'k': return (1<<0) | (1<<2);
    case 'l': return (1<<0) | (1<<1) | (1<<2);
    case 'm': return (1<<0) | (1<<2) | (1<<3);
    case 'n': return (1<<0) | (1<<2) | (1<<3) | (1<<4);
    case 'o': return (1<<0) | (1<<2) | (1<<4);
    case 'p': return (1<<0) | (1<<1) | (1<<2) | (1<<3);
    case 'q': return (1<<0) | (1<<1) | (1<<2) | (1<<3) | (1<<4);
    case 'r': return (1<<0) | (1<<1) | (1<<2) | (1<<4);
    case 's': return (1<<1) | (1<<2) | (1<<3);
    case 't': return (1<<1) | (1<<2) | (1<<3) | (1<<4);
    case 'u': return (1<<0) | (1<<2) | (1<<5);
    case 'v': return (1<<0) | (1<<1) | (1<<2) | (1<<5);
    case 'w': return (1<<1) | (1<<3) | (1<<4) | (1<<5);
    case 'x': return (1<<0) | (1<<2) | (1<<3) | (1<<5);
    case 'y': return (1<<0) | (1<<2) | (1<<3) | (1<<4) | (1<<5);
    case 'z': return (1<<0) | (1<<2) | (1<<4) | (1<<5);
    case ' ': return 0;
    default:  return 0;
  }
}

void updatePhysicalBraille(uint8_t p) {
  for (int i = 0; i < 6; i++) {
    // Check if the i-th bit is set
    if ((p >> i) & 1) {
      dots[i].write(DOT_UP);
    } else {
      dots[i].write(DOT_DOWN);
    }
  }
}

void processText(String text) {
  text.trim();
  for (unsigned int i = 0; i < text.length(); i++) {
    char c = text[i];
    uint8_t pat = brailleLetterPattern(c);

    Serial.print("Displaying Char: "); Serial.println(c);
    updatePhysicalBraille(pat);

    delay(1500); // Wait 1.5 seconds for the user to "read" the tactile dot
    
    // Reset dots briefly between letters (Optional)
    updatePhysicalBraille(0);
    delay(200); 
  }
}

void loop() {
  while (espIn.available()) {
    char c = espIn.read();
    if (c == '\n') {
      line.trim();
      if (line.startsWith("TXT:")) {
        String text = line.substring(4);
        Serial.print("📥 OCR TEXT: ");
        Serial.println(text);
        processText(text);
      }
      line = "";
    } else {
      line += c;
    }
  }
}