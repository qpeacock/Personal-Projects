// ===== Quadrature Encoder RPM =====
const int ENC_A = 2; // must be interrupt-capable
const int ENC_B = 3; // must be interrupt-capable

volatile long encoderPos = 0;
volatile int lastEncoded = 0;

unsigned long lastTime = 0;
float rpm = 0.0;
const unsigned int PULSES_PER_REV = 800; // encoder pulses per revolution

void setup() {
  Serial.begin(115200);

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ENC_A), updateEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), updateEncoder, CHANGE);

  lastTime = millis();
}

void loop() {
  delay(500); // update twice per second

  unsigned long now = millis();
  unsigned long deltaTime = now - lastTime;

  long count;
  noInterrupts();
  count = encoderPos;
  encoderPos = 0; // reset for next interval
  interrupts();

  // RPM calculation: revolutions per minute
  rpm = (count / (float)PULSES_PER_REV) / (deltaTime / 60000.0);

  Serial.print("RPM:");
  Serial.println(rpm);

  lastTime = now;
}

void updateEncoder() {
  int MSB = digitalRead(ENC_A); // Most Significant Bit
  int LSB = digitalRead(ENC_B); // Least Significant Bit

  int encoded = (MSB << 1) | LSB;
  int sum = (lastEncoded << 2) | encoded;

  if(sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) encoderPos++;
  if(sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) encoderPos--;

  lastEncoded = encoded;
}