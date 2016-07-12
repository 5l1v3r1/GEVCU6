/*
 * CKMotorController.cpp
 *
 * Created: 7/7/2016 2:47:16 PM
 *  Author: collin
 
  * Interface to the CK Motor Controller - Handles sending of commands and reception of status frames
  *
  Copyright (c) 2016 Collin Kidder, Michael Neuweiler, Charles Galpin

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */ 

#include "CKMotorController.h"

uint32_t CK_milli;

CKMotorController::CKMotorController() : MotorController() {
    prefsHandler = new PrefHandler(CKINVERTER);

    selectedGear = NEUTRAL;
    operationState = DISABLED;
    actualState = DISABLED;
    online = 0;
	aliveCounter = 0;
    commonName = "CK Inverter Ctrl Board";
}

void CKMotorController::setup() {
    TickHandler::getInstance()->detach(this);

    Logger::info("add device: CKINVCTRL (id:%X, %X)", CKINVERTER, this);

    loadConfiguration();
    MotorController::setup(); // run the parent class version of this function

    // register ourselves as observer of 0x23x and 0x65x can frames
    CanHandler::getInstanceEV()->attach(this, 0x410, 0x7f0, false);

    running = false;
    setSelectedGear(NEUTRAL);
    setOpState(DISABLED );
    CK_milli = millis();

    TickHandler::getInstance()->attach(this, CFG_TICK_INTERVAL_MOTOR_CONTROLLER_DMOC);
}

/*
 Finally, the firmware actually processes some of the status messages from the DmocMotorController
 However, currently the alive and checksum bytes aren't checked for validity.
 To be valid a frame has to have a different alive value than the last value we saw
 and also the checksum must match the one we calculate. Right now we'll just assume
 everything has gone according to plan.
 */

void CKMotorController::handleCanFrame(CAN_FRAME *frame) {
    int RotorTemp, invTemp, StatorTemp;
    int temp;
    online = true; //if a frame got to here then it passed the filter and must have been from the DMOC

    //Logger::debug("CKInverter CAN received: %X  %X  %X  %X  %X  %X  %X  %X  %X", frame->id,frame->data.bytes[0] ,frame->data.bytes[1],frame->data.bytes[2],frame->data.bytes[3],frame->data.bytes[4],frame->data.bytes[5],frame->data.bytes[6],frame->data.bytes[7]);

    switch (frame->id) {
    case 0x410: //Debugging output 1
        activityCount++;
        break;
    case 0x411: //debugging 2
        activityCount++;
        break;

    case 0x412: //debugging 3
        activityCount++;
        break;
    }
}

void CKMotorController::handleTick() {

    MotorController::handleTick(); //kick the ball up to papa

    if (activityCount > 0)
    {
        activityCount--;
        if (activityCount > 60) activityCount = 60;
        if (activityCount > 40) //If we are receiving regular CAN messages from the controller, this will very quickly get to over 40. We'll limit
            // it to 60 so if we lose communications, within 20 ticks we will decrement below this value.
        {
            //Logger::debug("EnableIn=%i and ReverseIn = %i" ,getEnableIn(),getReverseIn());
            /*if(getEnableIn()<0)*/setOpState(ENABLE); //If we HAVE an enableinput 0-3, we'll let that handle opstate. Otherwise set it to ENABLE
            /*if(getReverseIn()<0)*/setSelectedGear(DRIVE); //If we HAVE a reverse input, we'll let that determine forward/reverse.  Otherwise set it to DRIVE
        }
    }
    else {
        setSelectedGear(NEUTRAL); //We will stay in NEUTRAL until we get at least 40 frames ahead indicating continous communications.
    }


    if(!online)  //This routine checks to see if we have received any frames from the inverter.  If so, ONLINE would be true and
    {   //we set the RUNNING light on.  If no frames are received for 2 seconds, we set running OFF.
        if ((millis()-CK_milli)>2000)
        {
            running=false; // We haven't received any frames for over 2 seconds.  Otherwise online would be true.
            CK_milli=millis();   //Reset our 2 second timer
        }
    }
    else running=true;
    online=false;//This flag will be set to 1 by received frames.

    sendPowerCmd();
}

//Commanded RPM plus state of key and gear selector
void CKMotorController::sendPowerCmd() {
    CKMotorControllerConfiguration *config = (CKMotorControllerConfiguration *)getConfiguration();
    CAN_FRAME output;
    OperationState newstate;
    output.length = 7;
    output.id = 0x232;
    output.extended = 0; //standard frame
    output.rtr = 0;

	aliveCounter++;
	
	//obviously just for debugging during development. Do not leave these next lines here for long!
	operationState = MotorController::ENABLE;
	selectedGear = MotorController::DRIVE;
	powerMode = MotorController::modeSpeed;
	actualState = MotorController::ENABLE;

	if (operationState == ENABLE && selectedGear != NEUTRAL)
	{
		if (powerMode == modeSpeed)
		{
			torqueRequested = 0;
			if (throttleRequested > 0)
			{
		        speedRequested = (((long) throttleRequested * (long) config->speedMax) / 1000);			
			}
			else speedRequested = 0;
		}	
		else if (powerMode == modeTorque)
		{
			speedRequested = 0;
			torqueRequested = (long)throttleRequested * (long)config->torqueMax / 1000l;	
		}
	}
	else
	{
		speedRequested = 0;
		torqueRequested = 0;
	}

	output.data.bytes[0] = (speedRequested & 0x00FF);
    output.data.bytes[1] = (speedRequested >> 8) & 0xFF;
	output.data.bytes[2] = (torqueRequested & 0x00FF);
	output.data.bytes[3] = (torqueRequested >> 8) & 0xFF;    
   
    if (actualState == ENABLE) {
		output.data.bytes[4] = 1;
		if (selectedGear == MotorController::DRIVE) output.data.bytes[4] += 2;
		if (selectedGear == MotorController::REVERSE) output.data.bytes[4] += 4;
    }
    else { //force neutral gear until the system is enabled.
        output.data.bytes[4] = 0;
    }

	output.data.bytes[5] = aliveCounter;

    output.data.bytes[6] = calcChecksum(output);
	
	Logger::debug("CKInverter Sent Frame: %X  %X  %X  %X  %X  %X  %X  %X  %X", output.id, output.data.bytes[0] , output.data.bytes[1], output.data.bytes[2], output.data.bytes[3], output.data.bytes[4], output.data.bytes[5], output.data.bytes[6]);

    CanHandler::getInstanceEV()->sendFrame(output);
}

//just a bog standard CRC8 calculation with custom generator byte. Good enough.
//The point isn't obfuscation - just to prove to the other end that we're not insane and sending junk
//Obfuscation doesn't work well in open source projects anyway. ;)
byte CKMotorController::calcChecksum(CAN_FRAME& thisFrame)
{
    const uint8_t generator = 0xAD;
    uint8_t crc = 0;

    for (int byt = 0; byt < 6; byt++)
    {
	    crc ^= thisFrame.data.bytes[byt];

	    for (int i = 0; i < 8; i++)
	    {
		    if ((crc & 0x80) != 0)
		    {
			    crc = (uint8_t)((crc << 1) ^ generator);
		    }
		    else
		    {
			    crc <<= 1;
		    }
	    }
    }

    return crc;
}
	    
void CKMotorController::setGear(Gears gear) {
    selectedGear = gear;
    //if the gear was just set to drive or reverse and the DMOC is not currently in enabled
    //op state then ask for it by name
    if (selectedGear != NEUTRAL) {
        operationState = ENABLE;
    }
    //should it be set to standby when selecting neutral? I don't know. Doing that prevents regen
    //when in neutral and I don't think people will like that.
}


DeviceId CKMotorController::getId() {
    return (CKINVERTER);
}

uint32_t CKMotorController::getTickInterval()
{
    return CFG_TICK_INTERVAL_MOTOR_CONTROLLER_DMOC;
}

void CKMotorController::loadConfiguration() {
    CKMotorControllerConfiguration *config = (CKMotorControllerConfiguration *)getConfiguration();

    if (!config) {
        config = new CKMotorControllerConfiguration();
        setConfiguration(config);
    }

    MotorController::loadConfiguration(); // call parent
}

void CKMotorController::saveConfiguration() {
    MotorController::saveConfiguration();
}
