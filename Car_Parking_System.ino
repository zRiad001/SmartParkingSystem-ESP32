#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h> // Servo kütüphanesi şimdi aktif

// --- Pin Definitions ---
const int IR_PIN_1 = 26;    // IR sensor connected to GPIO 26
const int IR_PIN_2 = 27;    // IR sensor connected to GPIO 27
const int SDA_PIN = 21;     // I2C SDA pin (GPIO 21)
const int SCL_PIN = 22;     // I2C SCL pin (GPIO 22)
const int SERVO_PIN = 2;    // Servo motor control pin (GPIO 2)

// --- LCD Configuration ---
// CHECK YOUR SPECIFIC LCD MODULE FOR THE ADDRESS (0x27 and 0x3F are common)
// and the number of columns and rows. Ensure these match your LCD.
const int LCD_ADDRESS = 0x27; // Make sure this is the correct address (e.g., 0x27 or 0x3F)
const int LCD_COLS = 16;      // Set based on your LCD (e.g., 16, 20)
const int LCD_ROWS = 2;       // Set based on your LCD (e.g., 2, 4)

// --- Object Instantiation ---
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);
Servo parkingServo; // Servo nesnesi şimdi aktif

// --- Variables ---
int irState1 = 0;
int irState2 = 0;

void setup() {
  // --- Initialize Serial (Optional, for debugging) ---
  Serial.begin(115200);
  Serial.println("ESP32 Smart Parking System Starting...");

  // --- Initialize I2C Communication ---
  // ESP32 requires specifying SDA and SCL pins for Wire.begin()
  Wire.begin(SDA_PIN, SCL_PIN);

  // --- Initialize LCD ---
  lcd.init();      // Initialize the LCD
  lcd.backlight(); // Turn on the backlight
  lcd.clear();     // Clear the display
  lcd.setCursor(0, 0);
  lcd.print("Smart Parking");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(2000); // Display message for 2 seconds
  lcd.clear();

  // --- Configure Sensor Pins ---
  pinMode(IR_PIN_1, INPUT);
  pinMode(IR_PIN_2, INPUT);

  // --- Attach Servo ---
  // Connect the servo control pin (usually yellow or orange wire) to SERVO_PIN
  // REMEMBER TO POWER THE SERVO SEPARATELY IF IT DRAWS SIGNIFICANT CURRENT!
  // Connect the servo's GND to the ESP32's GND even if powered separately.
  parkingServo.attach(SERVO_PIN);
  // Set initial servo position (e.g., closed gate)
  parkingServo.write(0); // Adjust angle as needed (0 to 180 degrees)
  delay(1000); // Give servo time to move to initial position
}

void loop() {
  // --- Read Sensor States ---
  // IR sensors usually output LOW when detecting something, HIGH otherwise.
  // Adjust the logic (== LOW or == HIGH) based on your specific sensor behavior.
  irState1 = digitalRead(IR_PIN_1);
  irState2 = digitalRead(IR_PIN_2);

  // --- Your Parking Logic Here ---
  // Implement the logic based on sensor readings to control the servo and update the LCD.

  // Example Logic: Update LCD with sensor states
  lcd.setCursor(0, 0);
  lcd.print("IR1:");
  // Assuming LOW = occupied, HIGH = Free. Adjust if your sensors are the opposite.
  lcd.print(irState1 == LOW ? "Occupied" : "Free    ");
  lcd.setCursor(0, 1);
  lcd.print("IR2:");
  lcd.print(irState2 == LOW ? "Occupied" : "Free    ");

  // Example Logic: Control servo with IR Sensor 1
  if (irState1 == LOW) { // If IR1 detects a car (assuming LOW is detected)
    Serial.println("IR1 detected. Opening gate.");
    parkingServo.write(90); // Open gate (adjust angle 0-180)
    // You might want to add a delay here after opening the gate
    // delay(2000);
  } else { // If IR1 is clear
     Serial.println("IR1 clear. Closing gate.");
    parkingServo.write(0);  // Close gate (adjust angle)
  }

  // Add a small delay to prevent the loop from running too fast
  delay(100);
}