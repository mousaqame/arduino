// The original sketch, kept exactly as written. This is the restore point —
// flashing it puts the board back to simple threshold behaviour with no
// dashboard, no warm-up, and no calibration.

int alarm = 8;

int smokeLevel = A5;

int threVal = 250;


void setup() {

  // put your setup code here, to run once:

  pinMode(alarm,OUTPUT);

  pinMode(smokeLevel,INPUT);

  Serial.begin(9600);

}


void loop() {



  int sensorVal = analogRead(smokeLevel);

  Serial.println(sensorVal);


  if(sensorVal > threVal){

    tone(alarm,2000,200);

    }



  else{

    noTone(alarm);

    }



  }
