#include <ESP8266WiFi.h>
extern "C" {
	#include <osapi.h>
	#include <os_type.h>
}
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include "Motion1D.h"
#include "NetworkCommand.h"
#include "HTTPCommand.h"
#include "UdpLogger.h"
#include <string>
#include "simpleswitch.h"

/* SWITCHES */
#define HOSTNAME                 "wire"
#define NPORT                    (2500)

// ==-- HW connection --==
// STEP      - GPIO5  (D1)
// DIR1      - GPIO4  (D2)
// MOTTOR EN - GPIO2  (D4)
// SERVO     - GPIO12 (D6)
// BUTTON    - GPIO14 (D5)
//========================

// PIN definition
#define step1        5
#define dir1         4
#define enableMotor  2
#define servoPin     12
#define pushButton   14

#ifdef DEBUG_ENABLED
UdpLoggerClass     UdpLogger;
#endif
DNSServer          dnsServer;
class WiFiManager {
	public:
	WiFiManager() {}
	void autoConnect(String x) {
		WiFi.softAP("WireCutterAP");
		dnsServer.start(53, "*", WiFi.softAPIP());
	}
};
#include "secrets.h"

/* Global variables */
Motion1D          *m1d;
CommandDB         CmdDB;
NetworkCommand    *NCmd;
HTTPCommand       *HCmd;
volatile int ota_in_progress  = 0;
static int current_microsteps = 16;
const  int steps_per_rev      = 200; 
const  int steps_per_mm       = 99; //104;
volatile int catCounter       = 0;
volatile int catDistance      = 0;
volatile int catDuration      = 0;

static void makeCmdInterface();

/*!
 * \brief Setup.
 * 1. Configure gpio.
 * 2. Connect to WiFi.
 * 3. Setup OTA.
 * 4. Register available commands.
 * 5. Configure TMC2208.
 * 6. Create Motion1D controller.
 */
void setup()
{
	/* Setup pins */
	Serial.begin(115200);
	pinMode(enableMotor, OUTPUT);
	pinMode(step1, OUTPUT);
	pinMode(dir1, OUTPUT);
	pinMode(pushButton, INPUT_PULLUP);
	pinMode(16, INPUT_PULLUP);
	digitalWrite(enableMotor, HIGH);

	/* Connect to WiFi */
	detect_network();
#ifdef DEBUG_ENABLED
	UdpLogger.init(12345, "wire: ");
#endif
	pdebug("WIFI::IP address: %s\n", WiFi.localIP().toString().c_str());
	/* Setup OTA */
	ArduinoOTA.setHostname(HOSTNAME);
	ArduinoOTA.onStart([]() {
		ota_in_progress = 1;
	});
	ArduinoOTA.begin();
	
	makeCmdInterface();
	
	/* Setup driver */
	digitalWrite(enableMotor, LOW);    // Enable driver in hardware
	m1d = new Motion1D(step1, dir1, enableMotor, servoPin);

	new SimpleSwitch(14, [](SimpleSwitch *s, int butonEvent) { if (butonEvent) m1d->setCutterDown(); else m1d->setCutterUp(); } );

	pdebug("Setup done :-)\n");
}
//====================================================================================

/*!
 * \brief MAIN loop.
 * 1. Handle OTA.
 * 2. Handle CMD queue.
 * 3. Handle motion loop.
 */
void loop()
{
	/* Handle OTA */
	ArduinoOTA.handle();
	if (ota_in_progress) return;

	/* Execute command from queue */
	if ( m1d->loop() ) {
		CmdDB.loop();
	} else {
		if (m1d->motionQ_is_empty()) {
			if (catCounter) {
				catCounter--;
				/* Queue cut */
				m1d->goTo(catDuration, catDistance);
				m1d->setCutterDown(1500);
				m1d->setCutterUp(800);
			}
		}
		CmdDB.loopMotion();
		CmdDB.loop();
	}
}
//====================================================================================

/*!
 * \brief Global position estimation.
 */
static int g_pos_x = 0;

/*!
 * \brief Move to revolution command (absolute move).
 */
static void stepperMoveAbsoluteRev(CommandQueueItem *c)
{
	int d        = c->m_arg0;
	int newS     = c->m_arg1;
	int newE     = c->m_arg2;
	int duration, aX, dX;
	int speed    = 6000;

	if ((c->m_arg_mask & 7) != 7) {
		c->sendError();
		return;
	}
	
	/* Limit move distance */
	if (newS < 0) newS = 0;
	if (newE < 0) newE = 0;
	
	/* Convert to microsteps */
	newS*=(steps_per_rev * current_microsteps);
	newE*=(steps_per_rev * current_microsteps);

	dX = newS - g_pos_x;
	if (dX < 0) aX = -dX; else aX = dX;

	/* Calculate duration in [ms] */
	if (aX) {
		duration = (aX * 1000)/speed;
		m1d->goTo(duration, dX);
	}
	g_pos_x = newS;

	dX = newE - g_pos_x;
	if (dX < 0) aX = -dX; else aX = dX;
	if (aX) {
		m1d->goTo(d, dX);
	}
	g_pos_x = newE;
	c->sendAck();
}
//====================================================================================

/*!
 * \brief Move relative command (in revolution).
 */
static void stepperMoveRelativeRev(CommandQueueItem *c)
{
	int d        = c->m_arg0;
	int newS     = g_pos_x + (c->m_arg1 * steps_per_rev * current_microsteps);
	int aX, dX;

	if ((c->m_arg_mask & 3) != 3) {
		c->sendError();
		return;
	}	
	dX = newS - g_pos_x;
	if (dX < 0) aX = -dX; else aX = dX;
	if (aX) {
		m1d->goTo(d, dX);
	}
	g_pos_x = newS;
	c->sendAck();
}
//====================================================================================

/*!
 * \brief Move to position command (absolute move).
 */
static void stepperMoveAbsolute(CommandQueueItem *c)
{
	int d        = c->m_arg0;
	int newS     = c->m_arg1;
	int newE     = c->m_arg2;
	int duration, aX, dX;
	int speed    = 3000;

	if ((c->m_arg_mask & 7) != 7) {
		c->sendError();
		return;
	}
	
	dX = newS - g_pos_x;
	if (dX < 0) aX = -dX; else aX = dX;

	/* Calculate duration in [ms] */
	if (aX) {
		duration = (aX * 1000)/speed;
		m1d->goTo(duration, dX);
	}
	g_pos_x = newS;

	dX = newE - g_pos_x;
	if (dX < 0) aX = -dX; else aX = dX;
	if (aX) {
		m1d->goTo(d, dX);
	}
	g_pos_x = newE;
	c->sendAck();
}
//====================================================================================

/*!
 * \brief Move relative command (in microsteps).
 */
static void stepperMoveRelative(CommandQueueItem *c)
{
	int d        = c->m_arg0;
	int newS     = g_pos_x + c->m_arg1;
	int aX, dX;

	if ((c->m_arg_mask & 3) != 3) {
		c->sendError();
		return;
	}
	
	dX = newS - g_pos_x;
	if (dX < 0) aX = -dX; else aX = dX;
	if (aX) {
		m1d->goTo(d, dX);
	}
	g_pos_x = newS;
	c->sendAck();
}
//====================================================================================

/*!
 * \brief Move uncondicional - do not check limits (in microsteps).
 */
static void stepperMoveUncondicional(CommandQueueItem *c)
{
	int d        = c->m_arg0;
	int newS     = g_pos_x + c->m_arg1;
	int aX, dX;

	if ((c->m_arg_mask & 3) != 3) {
		c->sendError();
		return;
	}
	dX = newS - g_pos_x;
	if (dX < 0) aX = -dX; else aX = dX;
	if (aX) {
		m1d->goTo(d, dX);
	}
	g_pos_x = newS;
	c->sendAck();
}
//====================================================================================

/*!
 * \brief STOP movement.
 */
static void stepperMoveStop(CommandQueueItem *c)
{
	m1d->stop();
	g_pos_x = x_pos;
	catCounter = 0;
	c->sendAck();
}
//====================================================================================

/*!
 * \brief Set zero.
 */
void cmdG90(CommandQueueItem *c)
{
	m1d->stop();
	g_pos_x  = 0;
	x_pos    = 0;
	x_target = 0;
	c->sendAck();
}
//====================================================================================

/*!
 * \brief CmdHome (detect home).
 */
void cmdHome(CommandQueueItem *c)
{
	/* GoTo 0 */
	int newS     = 0;
	int duration, aX, dX;
	int speed    = 3000;

	dX = newS - g_pos_x;
	if (dX < 0) aX = -dX; else aX = dX;
	/* Calculate duration in [ms] */
	if (aX) {
		duration = (aX * 1000)/speed;
		m1d->goTo(duration, dX);
	}
	g_pos_x = newS;
	c->sendAck();
}
//====================================================================================

/*!
 * \brief Enable/Disable mottors command..
 */
static void enableMotors(CommandQueueItem *c)
{
	int value = -1, cmd = -1;

	if (c->m_arg_mask & 1) cmd = c->m_arg0;
	if (c->m_arg_mask & 2) value = c->m_arg1;

	if ((cmd != -1) && (value == -1)) {
		switch (cmd) {
			case 0: {m1d->motorsOff();c->sendAck();}break;
			case 1: {m1d->motorsOn();c->sendAck();}break;
			default: c->sendError(); break;
		}
	}
	if ((cmd != -1) && (value != -1)) {
		switch (value) {
			case 0: {m1d->motorsOff();c->sendAck();}break;
			case 1: {m1d->motorsOn();c->sendAck();}break;
			default: c->sendError(); break;
		}
	}
}
//====================================================================================

static void unrecognized(const char *command, Command *c) {c->print("!8 Err: Unknown command\r\n");}

/*!
 * \brief Fill commands database.
 */
static void makeCmdInterface()
{
	CmdDB.addCommand("v",[](CommandQueueItem *c) {
		c->print("WireCutter-Firmware V1.0\r\n");
	});
	CmdDB.addCommand("EM" ,enableMotors);
	/* Motion commands */
	CmdDB.addCommand("M"  ,stepperMoveAbsoluteRev, true);
	CmdDB.addCommand("MR" ,stepperMoveRelativeRev, true);
	CmdDB.addCommand("MH", cmdHome, true);
	CmdDB.addCommand("GT" ,stepperMoveAbsolute, true);
	CmdDB.addCommand("GTR",stepperMoveRelative, true);
	CmdDB.addCommand("GTH",cmdHome, true);
	CmdDB.addCommand("UM" ,stepperMoveUncondicional, true);
	CmdDB.addCommand("STP",stepperMoveStop);
	CmdDB.addCommand("TC",[](CommandQueueItem *c) { 
		m1d->toggleCutter();
		c->sendAck(); 
	});
	CmdDB.addCommand("CU",[](CommandQueueItem *c) { 
		m1d->setCutterUp();
		c->sendAck(); 
	});
	CmdDB.addCommand("CD",[](CommandQueueItem *c) { 
		m1d->setCutterDown();
		c->sendAck(); 
	});
	CmdDB.addCommand("CT",[](CommandQueueItem *c) { 
		m1d->setCutterDown(1500);
		m1d->setCutterUp(1500);
		c->sendAck(); 
	});

	CmdDB.addCommand("CUT",[](CommandQueueItem *c) { 
		catCounter = c->m_arg0;
		catDistance = c->m_arg1; 
		catDuration = c->m_arg2; 
		c->sendAck(); 
	});
	CmdDB.addCommand("CUTD",[](CommandQueueItem *c) { 
		catCounter = c->m_arg0;
		catDistance = c->m_arg1 * steps_per_mm ; /* Convert milimeters to microsteps */
		catDuration = c->m_arg1 * 20;            /* Set Duration 50mm/s */ 
		pdebug(("Cut wires " +String(catCounter) + ", "+String(c->m_arg1) + "[mm], "+String(catDistance) + "[usteps], " + String(catDuration)+ "[ms]\n\r").c_str() );
		c->sendAck(); 
	});
	/* Parameters */
	CmdDB.addCommand("G90",cmdG90, true);
	/* Status */
	CmdDB.addCommand("XX" ,[](CommandQueueItem *c){ m1d->printStat(c); });
	CmdDB.setDefaultHandler(unrecognized); // Handler for command that isn't matched (says "What?")

	NCmd = new NetworkCommand(&CmdDB, NPORT);
	HCmd = new HTTPCommand(&CmdDB);
}
//====================================================================================
