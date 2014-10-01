/*
  Conditioning
  to regulate camera, CS (LED, tone, whisker puff, ...), US. 
 */
 
// Outputs
int camera=8;
int led = 9;
int whisker = 10;
int tonech = 11;
int puff = 13;

// Task variables (time in ms, freq in hz)
int campretime=200;
int camposttime=800;
int cs = 500;
int csch = 1;
int ISI = 200;
int us = 20;
int residual;
int tonefreq5 = 1000;

unsigned long trialtime=0;  // For keeping track of elapsed time during trial

// the setup routine runs once when you press reset:
void setup() {                
  // initialize the digital pin as an output.
  pinMode(camera, OUTPUT); 
  pinMode(led, OUTPUT);     
  pinMode(puff, OUTPUT);  
  pinMode(whisker, OUTPUT);  

  // Default all output pins to LOW - for some reason they were floating high on the Due before I (Shane) added this
  digitalWrite(camera, LOW);
  digitalWrite(led, LOW);
  digitalWrite(puff, LOW);
  digitalWrite(whisker, LOW);

  Serial.begin(9600);
}

// the loop routine runs over and over again forever:
void loop() {
  // Consider using attachInterrupt() to allow better realtime control of starting and stopping, etc.
  
  checkVars();
  if (Serial.available()>0) {
    if (Serial.peek()==1) {  // This is the header for triggering; difference from variable communication is that only one byte is sent telling to trigger
      Serial.read();  // Clear the value from the buffer
      Triggered();
    }
  }  
  delay(1);
}


// Check to see if Matlab is trying to send updated variables
void checkVars() {
  int header;
  int value;
   // Matlab sends data in 3 byte packets: first byte is header telling which variable to update, 
   // next two bytes are the new variable data as 16 bit int
   // Header is coded numerically such that 1=trigger, 2=continuous, 3=CS channel, 4=CS dur, 
   // 0 is reserved for some future function, possibly as a bailout (i.e. stop reading from buffer).
   while (Serial.available() > 2) {
     header = Serial.read();
     value = Serial.read() | Serial.read()<<8;
     
     if (header==0) {
       break;
     }
     
     switch (header) {
      case 3:
        campretime=value;
        break;
      case 4:
        csch=value;
        break;
      case 5:
        cs=value;
        break;
      case 6:
        us=value;
        break; 
      case 7:
        ISI=value;
        break;
      case 8:
        tonefreq5=value;
        break;
      case 9:
        camposttime=value;
        break;
     }
     delay(4); // Delay enough to allow next 3 bytes into buffer (24 bits/9600 bps = 2.5 ms, so double it for safety).
  }
}


// --- function executed to start a trial ----
void Triggered() {
  unsigned long now; 
  // triggering camera 
  digitalWrite(camera, HIGH);  // Camera will collect the number of frames that we specify while TTL is high
 
// NOTE: I removed the single short pulse because camera is now configured for "TTL High" instead of "Rising-Edge"
//  delay(1); 
//  digitalWrite(camera, LOW);
//  residual=campretime-1;
  residual=campretime;
  if (residual > 0) {
    delay(residual);          // wait 
  }
  
  // starting a trial
  trialtime = millis();
  
  if (cs > 0){
     switch (csch) {
       case 1:
           digitalWrite(led, HIGH);   // turn the LED on (HIGH is the voltage level)
           break; 
       case 2:
           digitalWrite(whisker, HIGH);   // turn the LED on (HIGH is the voltage level)
           break; 
       case 5:
           tone(tonech, tonefreq5, cs);   // turn the LED on (HIGH is the voltage level)
           break; 
       case 6:
           tone(tonech, tonefreq5, cs);   // turn the LED on (HIGH is the voltage level)
           break; 
     }
  }
  delay(ISI);                // wait for isi
  
  if (us > 0){
     digitalWrite(puff, HIGH);   // turn the PUFF on (HIGH is the voltage level)
     delay(us);                  // wait for us
     digitalWrite(puff, LOW);    // turn the PUFF off (HIGH is the voltage level)
  }
  
  residual=cs-ISI-us;
  if (residual > 0) {
    delay(residual);          // wait 
  }
  
  if (cs > 0){
     switch (csch) {
       case 1:
           digitalWrite(led, LOW);   // turn the LED on (HIGH is the voltage level)
           break; 
       case 2:
           digitalWrite(whisker, LOW);   // turn the LED on (HIGH is the voltage level)
           break; 
     }
  }
  
  now = millis();
  if (now - trialtime < camposttime) {
    delay(int(now-trialtime));
  }
  
  // Camera will only collect the number of frames that we specify so we only have to reset TTL
  // to low before starting the next trial. The hitch is that if we set it low too early we won't
  // collect all of the frames that we asked for and the camera will get stuck; hence the extra delay. 
  delay(10); // Delay a little while longer just to be safe - so the camera doesn't get stuck with more frames to acquire
  digitalWrite(camera, LOW);  
}





/*
 Tone generator for Arduino Due
 v1  use timer, and toggle any digital pin in ISR
   funky duration from arduino version
   TODO use FindMckDivisor?
   timer selected will preclude using associated pins for PWM etc.
    could also do timer/pwm hardware toggle where caller controls duration
*/


// timers TC0 TC1 TC2   channels 0-2 ids 0-2  3-5  6-8     AB 0 1
// use TC1 channel 0 
#define TONE_TIMER TC1
#define TONE_CHNL 0
#define TONE_IRQ TC3_IRQn

// TIMER_CLOCK4   84MHz/128 with 16 bit counter give 10 Hz to 656KHz
//  piano 27Hz to 4KHz

static uint8_t pinEnabled[PINS_COUNT];
static uint8_t TCChanEnabled = 0;
static boolean pin_state = false ;
static Tc *chTC = TONE_TIMER;
static uint32_t chNo = TONE_CHNL;

volatile static int32_t toggle_count;
static uint32_t tone_pin;

// frequency (in hertz) and duration (in milliseconds).

void tone(uint32_t ulPin, uint32_t frequency, int32_t duration)
{
		const uint32_t rc = VARIANT_MCK / 256 / frequency; 
		tone_pin = ulPin;
		toggle_count = 0;  // strange  wipe out previous duration
		if (duration > 0 ) toggle_count = 2 * frequency * duration / 1000;
		 else toggle_count = -1;

		if (!TCChanEnabled) {
 			pmc_set_writeprotect(false);
			pmc_enable_periph_clk((uint32_t)TONE_IRQ);
			TC_Configure(chTC, chNo,
				TC_CMR_TCCLKS_TIMER_CLOCK4 |
				TC_CMR_WAVE |         // Waveform mode
				TC_CMR_WAVSEL_UP_RC ); // Counter running up and reset when equals to RC
	
			chTC->TC_CHANNEL[chNo].TC_IER=TC_IER_CPCS;  // RC compare interrupt
			chTC->TC_CHANNEL[chNo].TC_IDR=~TC_IER_CPCS;
			 NVIC_EnableIRQ(TONE_IRQ);
                         TCChanEnabled = 1;
		}
		if (!pinEnabled[ulPin]) {
			pinMode(ulPin, OUTPUT);
			pinEnabled[ulPin] = 1;
		}
		TC_Stop(chTC, chNo);
                TC_SetRC(chTC, chNo, rc);    // set frequency
		TC_Start(chTC, chNo);
}

void noTone(uint32_t ulPin)
{
	TC_Stop(chTC, chNo);  // stop timer
	digitalWrite(ulPin,LOW);  // no signal on pin
}

// timer ISR  TC1 ch 0
void TC3_Handler ( void ) {
	TC_GetStatus(TC1, 0);
	if (toggle_count != 0){
		// toggle pin  TODO  better
		digitalWrite(tone_pin,pin_state= !pin_state);
		if (toggle_count > 0) toggle_count--;
	} else {
		noTone(tone_pin);
	}
}



