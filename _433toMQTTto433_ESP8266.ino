/*
  433toMQTTto433  - ESP8266 program for home automation 
   Act as a wifi gateway between your 433mhz nodes and a MQTT broker 
   Send and receiving command by MQTT
 
  This program enables to:
 - receive MQTT data from a topic and send RF 433Mhz signal corresponding to the received MQTT data
 - publish MQTT data to a different topic related to received 433Mhz signal
  Contributor:
  - 1technophile
  Based on:
  - MQTT library (https://github.com/knolleary)
  - RCSwitch (https://github.com/sui77/rc-switch)
  - ESP8266Wifi
  
  Project home: https://github.com/1technophile/433toMQTTto433_ESP8266
  Blog, tutorial: http://1technophile.blogspot.com/2016/09/433tomqttto433-bidirectional-esp8266.html
Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
and associated documentation files (the "Software"), to deal in the Software without restriction, 
including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, 
subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED 
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF 
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
Some usefull commands to test gateway with mosquitto:
Subscribe to the subject for data receiption from RF signal
mosquitto_sub -t home/433toMQTT
Send data by MQTT to convert it on RF signal
mosquitto_pub -t home/MQTTto433/ -m 1315153
*/
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <RCSwitch.h> // library for controling Radio frequency switch

RCSwitch mySwitch = RCSwitch();

//Do we want to see trace for debugging purposes
#define TRACE 1  // 0= trace off 1 = trace on

// Update these with values suitable for your network.
#define wifi_ssid "mywifiid"
#define wifi_password "mypwd"
#define mqtt_server "192.168.0.22"
#define mqtt_user "your_username" // not compulsory if you set it uncomment line 127 and comment line 129
#define mqtt_password "your_password" // not compulsory if you set it uncomment line 127 and comment line 129

//variables to avoid duplicates
#define time_avoid_duplicate 3000 // if you want to avoid duplicate mqtt message received set this to > 0, the value is the time in milliseconds during which we don't publish duplicates
// array to store previous received RFs codes and their timestamps
long ReceivedRF[10][2] ={{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}};

#define subjectMQTTtoX "home/commands/#"
//RF MQTT Subjects
#define subject433toMQTT "home/433toMQTT"
#define subjectMQTTto433 "home/commands/MQTTto433"

//adding this to bypass to problem of the arduino builder issue 50
void callback(char*topic, byte* payload,unsigned int length);
WiFiClient espClient;

// client parameters
PubSubClient client(mqtt_server, 1883, callback, espClient);

//MQTT last attemps reconnection number
long lastReconnectAttempt = 0;

// Callback function, when the gateway receive an MQTT value on the topics subscribed this function is called
void callback(char* topic, byte* payload, unsigned int length) {
  // In order to republish this payload, a copy must be made
  // as the orignal payload buffer will be overwritten whilst
  // constructing the PUBLISH packet.
  trc("Hey I got a callback ");
  // Allocate the correct amount of memory for the payload copy
  byte* p = (byte*)malloc(length + 1);
  // Copy the payload to the new buffer
  memcpy(p,payload,length);
  
  // Conversion to a printable string
  p[length] = '\0';
  String callbackstring = String((char *) p);
  String topicNameRec = String((char*) topic);
  
  //launch the function to treat received data
  receivingMQTT(topicNameRec,callbackstring);

  // Free the memory
  free(p);
}

void setup()
{
  //Launch serial for debugging purposes
  Serial.begin(9600);
  //Begining wifi connection
  setup_wifi();
  delay(1500);
  lastReconnectAttempt = 0;
  
  mySwitch.enableTransmit(4); // RF Transmitter is connected to Pin D2 
  mySwitch.setRepeatTransmit(20); //increase transmit repeat to avoid lost of rf sendings
  mySwitch.enableReceive(5);  // Receiver on pin D1
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  trc("Connecting to ");
  trc(wifi_ssid);

  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    trc(".");
  }
  trc("WiFi connected");
}

boolean reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    trc("Attempting MQTT connection...");
    // Attempt to connect
    // If you  want to use a username and password, uncomment next line and comment the line if (client.connect("433toMQTTto433")) {
    //if (client.connect("433toMQTTto433", mqtt_user, mqtt_password)) {
    // and set username and password at the program beginning
    if (client.connect("433toMQTTto433")) {
    // Once connected, publish an announcement...
      client.publish("outTopic","hello world");
      trc("connected");
      //Subscribing to topic(s)
      subscribing(subjectMQTTtoX);
    } else {
      trc("failed, rc=");
      trc(String(client.state()));
      trc(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  return client.connected();
}

void loop()
{
  //MQTT client connexion management
  if (!client.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      trc("client mqtt not connected, trying to connect");
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    // MQTT loop
    client.loop();
  }

  // Receive loop, if data received by RF433 send it by MQTT to subject433toMQTT
  if (mySwitch.available()) {
    // Topic on which we will send data
    trc("Receiving 433Mhz signal");
    unsigned long MQTTvalue;
    MQTTvalue=mySwitch.getReceivedValue();  
    mySwitch.resetAvailable();
    if (client.connected()) {
      if (!isAduplicate(MQTTvalue)) {// conditions to avoid duplications of RF -->MQTT
          trc("Sending 433Mhz signal to MQTT");
          trc(String(MQTTvalue));
          sendMQTT(subject433toMQTT,String(MQTTvalue));
          storeValue(MQTTvalue);
      }         
    } else {
      if (reconnect()) {
        trc("Sending 433Mhz signal to MQTT after reconnect");
        trc(String(MQTTvalue));
        sendMQTT(subject433toMQTT,String(MQTTvalue));
        storeValue(MQTTvalue);
        lastReconnectAttempt = 0;
      }
    }
  }
}

void storeValue(long MQTTvalue){
    long now = millis();
    // find oldest value of the buffer
    int o = getMin();
    trc("Minimum index: " + String(o));
    // replace it by the new one
    ReceivedRF[o][0] = MQTTvalue;
    ReceivedRF[o][1] = now;
    trc("send this code :" + String(ReceivedRF[o][0])+"/"+String(ReceivedRF[o][1]));
    trc("Col: value/timestamp");
    for (int i = 0; i < 10; i++)
    {
      trc(String(i) + ":" + String(ReceivedRF[i][0])+"/"+String(ReceivedRF[i][1]));
    }
}

int getMin()
{
  int minimum = ReceivedRF[0][1];
  int minindex=0;
  for (int i = 0; i < 10; i++)
  {
    if (ReceivedRF[i][1] < minimum) {
      minimum = ReceivedRF[i][1];
      minindex = i;
    }
  }
  return minindex;
}

boolean isAduplicate(long value){
trc("isAduplicate");
// check if the value has been already sent during the last "time_avoid_duplicate"
for (int i=0; i<10;i++){
 if (ReceivedRF[i][0] == value){
      long now = millis();
      if (now - ReceivedRF[i][1] < time_avoid_duplicate){
      trc("don't send this code :" + String(ReceivedRF[i][0])+"/"+String(ReceivedRF[i][1]));
      return true;
    }
  }
}
return false;
}

void subscribing(String topicNameRec){ // MQTT subscribing to topic
  char topicStrRec[26];
  topicNameRec.toCharArray(topicStrRec,26);
  // subscription to topic for receiving data
  boolean pubresult = client.subscribe(topicStrRec);
  if (pubresult) {
    trc("subscription OK to");
    trc(topicNameRec);
  }
}

void receivingMQTT(String topicNameRec, String callbackstring) {
  trc("Receiving data by MQTT");
  trc(topicNameRec);
  char topicOri[26] = "";
  char topicStrAck[26] = "";
  char datacallback[26] = "";
  
  // Below you send RF signal following data value received by MQTT 
    callbackstring.toCharArray(datacallback,26);
    trc(datacallback);
    long int data = atol(datacallback);
    trc("Send received data by RF 433");
    trc(String(data));
    //send received MQTT value by RF signal (example of signal sent data = 5264660)
    mySwitch.send(data, 24);
}

//send MQTT data dataStr to topic topicNameSend
void sendMQTT(String topicNameSend, String dataStr){

      char topicStrSend[26];
      topicNameSend.toCharArray(topicStrSend,26);
      char dataStrSend[200];
      dataStr.toCharArray(dataStrSend,200);
      boolean pubresult = client.publish(topicStrSend,dataStrSend);
      trc("sending ");
      trc(dataStr);
      trc("to ");
      trc(topicNameSend);
}

//trace
void trc(String msg){
  if (TRACE) {
  Serial.println(msg);
  }
}
