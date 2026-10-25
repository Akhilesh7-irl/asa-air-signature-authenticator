#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

// Air-Signature Authenticator — ESP32 + MPU6050, no external firmware libraries.
namespace C { const uint8_t I2C_SDA=21,I2C_SCL=22,GREEN=25,RED=26,BUZZER=27,ADDR=0x68;
const uint32_t SAMPLE=10,TELEMETRY=20,DEBOUNCE=150,PAUSE=300,SILENCE=2000,RESULT_HOLD=1000,COOLDOWN=10000;
const uint8_t MAX_TOKENS=20,MAX_ERRORS=1,MAX_FAILS=3,REPS=3;
const float DEFAULT_T=0.25f,DEFAULT_GYRO_T=35.0f,THRESHOLD_FRAC=0.60f,STILL_BAND=0.10f,GYRO_STILL=12.0f; }
enum Token:uint8_t{NONE,U,D,L,R,P,GXp,GXn,GYp,GYn,GZp,GZn}; enum Mode:uint8_t{BOOT,ENROLL,VERIFY}; enum MState:uint8_t{M_IDLE,M_ACTIVE,M_ACCEPT,M_REJECT}; enum LState:uint8_t{L_NORMAL,L_WARN_1,L_WARN_2,L_LOCKED};
struct Sample{float ax=0,ay=0,az=0,gx=0,gy=0,gz=0,mag=0;}; struct Gesture{Token t[C::MAX_TOKENS]{};uint8_t n=0;float tx=C::DEFAULT_T,ty=C::DEFAULT_T,tgx=C::DEFAULT_GYRO_T,tgy=C::DEFAULT_GYRO_T,tgz=C::DEFAULT_GYRO_T;bool valid=false;};
const char* tn(Token t){switch(t){case U:return"U";case D:return"D";case L:return"L";case R:return"R";case P:return"P";case GXp:return"GXp";case GXn:return"GXn";case GYp:return"GYp";case GYn:return"GYn";case GZp:return"GZp";case GZn:return"GZn";default:return"NONE";}}
const char* mn(MState s){switch(s){case M_ACTIVE:return"M_ACTIVE";case M_ACCEPT:return"M_ACCEPT";case M_REJECT:return"M_REJECT";default:return"M_IDLE";}}
const char* ln(LState s){switch(s){case L_WARN_1:return"L_WARN_1";case L_WARN_2:return"L_WARN_2";case L_LOCKED:return"L_LOCKED";default:return"L_NORMAL";}}
void seq(const Token*a,uint8_t n){for(uint8_t i=0;i<n;i++){if(i)Serial.print('-');Serial.print(tn(a[i]));}}
bool ready=false; Gesture enrolled; Mode mode=BOOT; MState ms=M_IDLE; LState ls=L_NORMAL;
uint8_t fails=0,idx=0,errors=0,genN=0,capN=0,repN=0; Token generated[C::MAX_TOKENS]{},cap[C::MAX_TOKENS]{},reps[C::REPS][C::MAX_TOKENS]{};
float peakX=0,peakY=0,peakGX=0,peakGY=0,peakGZ=0,peaksX[C::REPS]{},peaksY[C::REPS]{},peaksGX[C::REPS]{},peaksGY[C::REPS]{},peaksGZ[C::REPS]{},thX=C::DEFAULT_T,thY=C::DEFAULT_T,thGX=C::DEFAULT_GYRO_T,thGY=C::DEFAULT_GYRO_T,thGZ=C::DEFAULT_GYRO_T;
uint32_t lastSample=0,lastTelem=0,lastToken=0,stillAt=0,lastInput=0,resultAt=0,lockedAt=0,lastLockedMsg=0,signalAt=0;
bool pauseSent=false,signalOn=false,signalOk=false,telemetryOn=true; Sample sample;
bool readRegs(uint8_t r,uint8_t*d,size_t n){Wire.beginTransmission(C::ADDR);Wire.write(r);if(Wire.endTransmission(false))return false;size_t k=Wire.requestFrom((int)C::ADDR,(int)n,(int)true);if(k!=n)return false;for(size_t i=0;i<n;i++)d[i]=Wire.read();return true;}
bool writeReg(uint8_t r,uint8_t v){Wire.beginTransmission(C::ADDR);Wire.write(r);Wire.write(v);return Wire.endTransmission()==0;}
bool readSensor(Sample&s){uint8_t b[14];if(!ready||!readRegs(0x3B,b,14))return false;auto w=[&](int i){return(int16_t)(((uint16_t)b[i]<<8)|b[i+1]);};s.ax=w(0)/16384.f;s.ay=w(2)/16384.f;s.az=w(4)/16384.f;s.gx=w(8)/131.f;s.gy=w(10)/131.f;s.gz=w(12)/131.f;s.mag=sqrtf(s.ax*s.ax+s.ay*s.ay+s.az*s.az);return true;}
void deviceState(){Serial.printf("{\"e\":\"device_state\",\"ready\":%s,\"enrolled\":%s}\n",ready?"true":"false",enrolled.valid?"true":"false");}
void modeEvent(){Serial.printf("{\"e\":\"mode\",\"m\":\"%s\"}\n",mode==ENROLL?"ENROLL":mode==VERIFY?"VERIFY":"BOOT");}
void lockEvent(){Serial.printf("{\"e\":\"lockout\",\"s\":\"%s\",\"fails\":%u}\n",ln(ls),fails);}
void matchEvent(){Serial.printf("{\"e\":\"matcher\",\"s\":\"%s\",\"i\":%u,\"err\":%u}\n",mn(ms),idx,errors);}
void resultEvent(bool ok){Serial.print("{\"e\":\"result\",\"r\":\"");Serial.print(ok?"ACCEPT":"REJECT");Serial.print("\",\"gen\":\"");seq(generated,genN);Serial.print("\",\"exp\":\"");seq(enrolled.t,enrolled.n);Serial.printf("\",\"err\":%u}\n",errors);}
void setMode(Mode m){mode=m;capN=0;peakX=peakY=peakGX=peakGY=peakGZ=0;lastToken=0;stillAt=0;pauseSent=false;modeEvent();}
void startVerify(uint32_t now){ms=M_IDLE;idx=errors=genN=0;lastInput=now;}
void beginEnroll(){repN=0;capN=0;memset(reps,0,sizeof(reps));memset(peaksX,0,sizeof(peaksX));memset(peaksY,0,sizeof(peaksY));memset(peaksGX,0,sizeof(peaksGX));memset(peaksGY,0,sizeof(peaksGY));memset(peaksGZ,0,sizeof(peaksGZ));enrolled.valid=false;thX=thY=C::DEFAULT_T;thGX=thGY=thGZ=C::DEFAULT_GYRO_T;setMode(ENROLL);Serial.println("Enroll started: perform the same gesture three times.");}
void setSignal(bool ok,uint32_t now){signalOk=ok;signalAt=now;signalOn=true;}
Token quantize(const Sample&s,uint32_t now){
  peakX=max(peakX,fabsf(s.ax));peakY=max(peakY,fabsf(s.ay));peakGX=max(peakGX,fabsf(s.gx));peakGY=max(peakGY,fabsf(s.gy));peakGZ=max(peakGZ,fabsf(s.gz));
  bool still=fabsf(s.mag-1.f)<=C::STILL_BAND&&fabsf(s.gx)<C::GYRO_STILL&&fabsf(s.gy)<C::GYRO_STILL&&fabsf(s.gz)<C::GYRO_STILL;bool hasGesture=mode==ENROLL?capN>0:(mode==VERIFY&&genN>0);
  if(still){if(!stillAt)stillAt=now;if(hasGesture&&!pauseSent&&now-stillAt>=C::PAUSE&&(!lastToken||now-lastToken>=C::DEBOUNCE)){pauseSent=true;lastToken=now;return P;}}
  else{stillAt=0;pauseSent=false;}
  if(lastToken&&now-lastToken<C::DEBOUNCE)return NONE;
  float score[5]={fabsf(s.ax)/thX,fabsf(s.ay)/thY,fabsf(s.gx)/thGX,fabsf(s.gy)/thGY,fabsf(s.gz)/thGZ};uint8_t best=0;for(uint8_t i=1;i<5;i++)if(score[i]>score[best])best=i;if(score[best]<1.f)return NONE;
  lastToken=now;stillAt=0;pauseSent=false;
  if(best==0)return s.ax>=0?R:L;if(best==1)return s.ay>=0?U:D;
  if(best==2)return s.gx>=0?GXp:GXn;if(best==3)return s.gy>=0?GYp:GYn;return s.gz>=0?GZp:GZn;
}
void finishVerify(uint32_t now){
  if(mode!=VERIFY||(!genN&&ms==M_IDLE)){Serial.println("No attempt received.");setMode(BOOT);return;}
  bool ok=idx==enrolled.n&&errors<=C::MAX_ERRORS;ms=ok?M_ACCEPT:M_REJECT;resultAt=now;matchEvent();resultEvent(ok);setSignal(ok,now);
  if(ok){fails=0;ls=L_NORMAL;}else{if(fails<C::MAX_FAILS)fails++;if(fails>=C::MAX_FAILS){ls=L_LOCKED;lockedAt=now;}else ls=fails==1?L_WARN_1:L_WARN_2;}
  lockEvent();setMode(BOOT);
}
void enrollmentError(const char* message){Serial.printf("{\"e\":\"enroll_error\",\"message\":\"%s\"}\n",message);repN=0;capN=0;enrolled.valid=false;setMode(BOOT);}
void finishEnroll(){
  if(mode!=ENROLL||capN==0){if(mode==ENROLL)Serial.println("No tokens captured; try a clearer gesture.");return;}
  uint8_t first=0;while(first<C::MAX_TOKENS&&reps[0][first]!=NONE)first++;
  if(repN&&capN!=first){enrollmentError("Repetitions had different token counts. Start over.");return;}
  for(uint8_t i=0;i<capN;i++)reps[repN][i]=cap[i];peaksX[repN]=peakX;peaksY[repN]=peakY;peaksGX[repN]=peakGX;peaksGY[repN]=peakGY;peaksGZ[repN]=peakGZ;repN++;
  Serial.printf("{\"e\":\"enroll\",\"rep\":%u,\"of\":%u,\"seq\":\"",repN,C::REPS);seq(cap,capN);Serial.println("\"}");
  capN=0;peakX=peakY=peakGX=peakGY=peakGZ=0;if(repN<C::REPS){Serial.println("Repeat the same gesture.");return;}
  uint8_t n=0;while(n<C::MAX_TOKENS&&reps[0][n]!=NONE)n++;
  for(uint8_t i=0;i<n;i++){uint8_t c[12]={};for(uint8_t r=0;r<C::REPS;r++)c[reps[r][i]]++;uint8_t best=0;for(uint8_t j=1;j<12;j++)if(c[j]>c[best])best=j;if(c[best]<2){enrollmentError("The three repetitions did not agree. Start over with a consistent gesture.");return;}enrolled.t[i]=(Token)best;}
  float x=0,y=0,gx=0,gy=0,gz=0;for(uint8_t r=0;r<C::REPS;r++){x+=peaksX[r];y+=peaksY[r];gx+=peaksGX[r];gy+=peaksGY[r];gz+=peaksGZ[r];}enrolled.n=n;enrolled.tx=constrain(x/C::REPS*C::THRESHOLD_FRAC,.12f,1.2f);enrolled.ty=constrain(y/C::REPS*C::THRESHOLD_FRAC,.12f,1.2f);enrolled.tgx=constrain(gx/C::REPS*C::THRESHOLD_FRAC,18.f,240.f);enrolled.tgy=constrain(gy/C::REPS*C::THRESHOLD_FRAC,18.f,300.f);enrolled.tgz=constrain(gz/C::REPS*C::THRESHOLD_FRAC,18.f,300.f);enrolled.valid=true;thX=enrolled.tx;thY=enrolled.ty;thGX=enrolled.tgx;thGY=enrolled.tgy;thGZ=enrolled.tgz;
  Serial.print("{\"e\":\"enrolled\",\"seq\":\"");seq(enrolled.t,enrolled.n);Serial.printf("\",\"thx\":%.3f,\"thy\":%.3f,\"thgx\":%.1f,\"thgy\":%.1f,\"thgz\":%.1f}\n",thX,thY,thGX,thGY,thGZ);repN=0;setMode(BOOT);
}
void command(char c,uint32_t now){if(c=='T'){telemetryOn=!telemetryOn;Serial.printf("Telemetry %s.\n",telemetryOn?"on":"off");}else if(c=='E'){beginEnroll();}else if(c=='V'){if(!enrolled.valid){Serial.println("Enroll a gesture first.");return;}if(ls==L_LOCKED){Serial.println("Locked; wait for cooldown or reset.");return;}setMode(VERIFY);startVerify(now);Serial.println("Verification ready. Perform gesture.");}else if(c=='R'){fails=0;ls=L_NORMAL;lockEvent();Serial.println("Lockout reset.");}else if(c=='Q'){deviceState();if(enrolled.valid){Serial.print("{\"e\":\"enrolled\",\"seq\":\"");seq(enrolled.t,enrolled.n);Serial.println("\"}");}}}
void commands(uint32_t now){while(Serial.available()){char c=(char)Serial.read();if(c=='E'||c=='V'||c=='R'||c=='T'||c=='Q')command(c,now);}}
void io(uint32_t now){bool f=signalOn&&now-signalAt<180;digitalWrite(C::GREEN,f&&signalOk?HIGH:LOW);digitalWrite(C::RED,ls==L_LOCKED||(f&&!signalOk)?HIGH:LOW);digitalWrite(C::BUZZER,f&&!signalOk&&now-signalAt<120?HIGH:LOW);if(signalOn&&!f)signalOn=false;}
void setup(){Serial.begin(115200);Wire.begin(C::I2C_SDA,C::I2C_SCL);Wire.setClock(400000);pinMode(C::GREEN,OUTPUT);pinMode(C::RED,OUTPUT);pinMode(C::BUZZER,OUTPUT);uint8_t who=0;ready=readRegs(0x75,&who,1)&&(who==0x68||who==0x69);if(ready)ready=writeReg(0x6B,0)&&writeReg(0x1C,0)&&writeReg(0x1B,0)&&writeReg(0x1A,3);Serial.println("{\"e\":\"boot\",\"ver\":1}");Serial.println(ready?"MPU6050 ready.":"MPU6050 not found: check wiring and I2C address.");deviceState();modeEvent();lockEvent();}
void loop(){
 uint32_t now=millis();commands(now);
 if(now-lastSample>=C::SAMPLE){lastSample=now;if(readSensor(sample)){Token t=quantize(sample,now);
  if(mode==ENROLL&&t!=NONE){if(capN<C::MAX_TOKENS)cap[capN++]=t;Serial.printf("{\"e\":\"token\",\"t\":\"%s\"}\n",tn(t));}
  else if(mode==VERIFY&&ls!=L_LOCKED&&t!=NONE){if(ms==M_IDLE)ms=M_ACTIVE;if(genN<C::MAX_TOKENS)generated[genN++]=t;lastInput=now;if(idx<enrolled.n){if(t!=enrolled.t[idx])errors++;idx++;}else errors=C::MAX_ERRORS+1;matchEvent();Serial.printf("{\"e\":\"token\",\"t\":\"%s\"}\n",tn(t));if(errors>C::MAX_ERRORS){ms=M_REJECT;matchEvent();resultEvent(false);setSignal(false,now);if(fails<C::MAX_FAILS)fails++;if(fails>=C::MAX_FAILS){ls=L_LOCKED;lockedAt=now;}else ls=fails==1?L_WARN_1:L_WARN_2;lockEvent();setMode(BOOT);}}
 }}
 if(now-lastTelem>=C::TELEMETRY){lastTelem=now;if(ready&&telemetryOn)Serial.printf("{\"e\":\"sample\",\"t\":%lu,\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,\"gx\":%.1f,\"gy\":%.1f,\"gz\":%.1f,\"mag\":%.3f}\n",(unsigned long)now,sample.ax,sample.ay,sample.az,sample.gx,sample.gy,sample.gz,sample.mag);}
 if(mode==ENROLL&&capN&&now-lastToken>=C::SILENCE)finishEnroll();
 if(mode==VERIFY&&ms==M_ACTIVE&&now-lastInput>=C::SILENCE)finishVerify(now);
 if(ls==L_LOCKED){if(now-lastLockedMsg>=1000){lastLockedMsg=now;uint32_t rem=C::COOLDOWN-(now-lockedAt);Serial.printf("{\"e\":\"locked\",\"remaining\":%lu}\n",(unsigned long)((rem+999)/1000));}if(now-lockedAt>=C::COOLDOWN){fails=0;ls=L_NORMAL;lockEvent();}}
 io(now);
}