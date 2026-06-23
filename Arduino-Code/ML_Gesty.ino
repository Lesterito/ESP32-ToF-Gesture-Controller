#include <ESP32_Gestures_inferencing.h> 

#include <Wire.h>
#include <SparkFun_VL53L5CX_Library.h> 
#include <BleKeyboard.h>

SparkFun_VL53L5CX myImager;
VL53L5CX_ResultsData measurementData;

// Inicjalizacja Klawiatury Bluetooth
BleKeyboard bleKeyboard("Czujnik Gestow", "ESP32", 100);

const int LASER_PIN = 4;

// --- ZMIENNE DLA SZTUCZNEJ INTELIGENCJI ---
float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE]; 
int feature_ix = 0; 

// --- ZMIENNE DLA COOLDOWNU ---
unsigned long last_gesture_time = 0;
const int COOLDOWN_MS = 1500; 
const float THRESHOLD = 0.80; 

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LASER_PIN, OUTPUT);
  digitalWrite(LASER_PIN, HIGH); 

  Wire.begin(21, 22);
  Wire.setClock(400000); 

  if (myImager.begin() == false) {
    Serial.println("G:ERROR_SENSOR"); 
    while (1);
  }

  myImager.setResolution(4 * 4); 
  myImager.setRangingFrequency(60); 
  myImager.startRanging(); 
  
  bleKeyboard.begin();
  Serial.println("G:SYSTEM_READY");
}

void loop() {
  if (myImager.isDataReady() == true) {
    myImager.getRangingData(&measurementData);

    // --- 1. WYSYŁANIE RAMKI DANYCH DO LABVIEW ---
    Serial.print("D:");
    for (int i = 0; i < 16; i++) {
        Serial.print(measurementData.distance_mm[i]);
        if (i < 15) Serial.print(",");
    }
    Serial.println(); 

    // --- 2. ZARZĄDZANIE OKNEM PRZESUWNYM ---
    for (int i = 0; i < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE - 16; i++) {
        features[i] = features[i + 16];
    }
    for (int i = 0; i < 16; i++) {
        features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE - 16 + i] = measurementData.distance_mm[i];
    }
    if (feature_ix < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
        feature_ix += 16;
    }

    // --- 3. INFERENCJA ---
    if (feature_ix >= EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
        
        signal_t signal;
        numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
        ei_impulse_result_t result = { 0 };
        run_classifier(&signal, &result, false);

        float max_val = 0.0;
        int best_idx = -1;
        for (int ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            if (result.classification[ix].value > max_val) {
                max_val = result.classification[ix].value;
                best_idx = ix;
            }
        }

        // --- 4. KALKULATOR DYSTANSU ---
        float start_dist = 0;
        for(int i=0; i<48; i++) start_dist += features[i];
        start_dist /= 48.0;

        float current_dist = 0;
        for(int i=0; i<16; i++) current_dist += features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE - 16 + i];
        current_dist /= 16.0;

        static unsigned long hold_start_time = 0;
        static bool is_holding = false;
        bool hold_aktywny = false; 

        
        if (current_dist < 600.0) {
            if (!is_holding) {
                is_holding = true;
                hold_start_time = millis(); 
            } 
            else if (millis() - hold_start_time >= 2000) {
                hold_aktywny = true; 
                static unsigned long last_hold_print = 0;
                if (millis() - last_hold_print > 200) {
                    
                    Serial.println("G:HOLD");
                    if(bleKeyboard.isConnected()) {
                        bleKeyboard.write(KEY_ESC);b
                    }
                    
                    last_hold_print = millis();
                }
            }
        } 
        else {
            is_holding = false; 
        }

        // LOGIKA AI
        if (best_idx != -1 && max_val > THRESHOLD && !hold_aktywny) {
            String predicted_label = String(result.classification[best_idx].label);
            
            if (!predicted_label.equalsIgnoreCase("idle")) {
                
                if (predicted_label.equalsIgnoreCase("push")) {
                    if ((start_dist - current_dist) > 150.0) {
                        if (millis() - last_gesture_time > COOLDOWN_MS) {
                            
                            Serial.println("G:PUSH");
                            if(bleKeyboard.isConnected()) {
                                bleKeyboard.write('b');
                            }
                            
                            last_gesture_time = millis();
                        }
                    }
                }
                else {    
                    if (millis() - last_gesture_time > COOLDOWN_MS) {
                        predicted_label.toUpperCase();
                        Serial.print("G:");
                        Serial.println(predicted_label);
                        
                        if(bleKeyboard.isConnected()) {
                            if (predicted_label == "SWIPE_LEFT") {
                                bleKeyboard.write(KEY_LEFT_ARROW);
                            } 
                            else if (predicted_label == "SWIPE_RIGHT") {
                                bleKeyboard.write(KEY_RIGHT_ARROW);
                            }
                        }
                        
                        last_gesture_time = millis(); 
                    }
                } 
            } 
        }
    }
  }
}