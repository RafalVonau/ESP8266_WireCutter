/*
 * One axis (1D) motion class for ESP8266.
 *
 * Author: Rafal Vonau <rafal.vonau@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
extern "C" {
	#include <osapi.h>
	#include <os_type.h>
}
#include "Motion1D.h"
#include "AccelStepper.h" // nice lib from http://www.airspayce.com/mikem/arduino/AccelStepper/
#include <Servo.h>
#include "ramp.h"

Servo             penservo;

#ifndef USE_ACCEL_STEPPER

#include "esp8266_gpio_direct.h"
#include "core_esp8266_waveform.h"

#define MIN_PERIOD        (4000)

static volatile int        int_active       = 0;
static volatile int        in_motion        = 0;
static volatile uint32_t   last_time        = 0;

/* X */
static uint16_t            x_gpio_mask      = 0;
static volatile uint32_t   x_hperiod        = 0;
volatile int               x_target         = 0;
static volatile uint32_t   x_time           = 0;
volatile int               x_pos            = 0;
static volatile int        x_pulse          = 0;

#ifdef USE_RAMP
volatile int               x_pos_start      = 0;   /*!< Motion start position.                            */
volatile int               x_pos_middle     = 0;   /*!< Motion middle point.                              */
volatile int               x_ramp_len       = 0;   /*!< Motion ramp length.                               */
volatile int               x_ramp_pos       = 0;   /*!< Current ramp table index.                         */
volatile int               x_ramp_iter      = 0;   /*!< Current iterations.                               */
volatile int               x_ramp_phase     = 0;   /*!< Current ramp phase.                               */
volatile uint32_t          x_target_hperiod = 0;   /*!< Target half period.                               */
volatile int               x_dir            = 0;   /*!< Motion direction.                                 */
#endif


static uint32_t motion_intr_handler(void);

//#pragma GCC optimize ("Os")

static inline ICACHE_RAM_ATTR uint32_t GetCycleCount()
{
	uint32_t ccount;
	__asm__ __volatile__("esync; rsr %0,ccount":"=a"(ccount));
	return ccount;
}
//===========================================================================================

void Motion1D::printStat(CommandQueueItem *c)
{
	c->print("now="+String(GetCycleCount()) + "\r\nint_active="+String(int_active)+"\r\nin_motion="+String(in_motion)+"\r\n" + \
		"x_time="+String(x_time) + "\r\n" \
		"x_pulse="+String(x_pulse) + "\r\n" \
		"x_pos="+String(x_pos)+",target = " + String(x_target) + "\r\nOK\r\n");
}
//===========================================================================================

#else
void Motion2D::printStat(CommandQueueItem *c)
{
}
//===========================================================================================
#endif

/*!
 * \breif Constructor.
 */
Motion1D::Motion1D(int step1, int dir1, int en_pin, int servoPin)
{
	m_cutterUpPos = 0;
	m_cutterDownPos = 150;
#ifdef USE_ACCEL_STEPPER
	m_xMotor = new AccelStepper(1, step1, dir1);
	m_xMotor->setMaxSpeed(2000.0);
	m_xMotor->setAcceleration(10000.0);
#else
	pinMode(step1, OUTPUT);
	pinMode(dir1, OUTPUT);
	digitalWrite(step1, LOW);
	m_x_dir         = dir1;
	x_gpio_mask     = (1 << step1);
	/* Disable timer */
	setTimer1Callback(NULL);
	int_active      = 0;
	in_motion       = 0;
#endif
	m_en_pin        = en_pin;
	m_motorsEnabled = 0;
	pinMode(en_pin, OUTPUT);
	motorsOff();
	pinMode(servoPin,OUTPUT);
	penservo.attach(servoPin, 500, 2500);
	setCutterUpReal();
#ifdef MOTION_QUEUE_SIZE
	m_motionQWr   = 0;
	m_motionQRd   = 0;
#endif
}
//====================================================================================

/*!
 * \brief Disable Motors.
 */
void Motion1D::motorsOff()
{
	digitalWrite(m_en_pin, HIGH);
	m_motorsEnabled = 0;
}
//====================================================================================

/*!
 * \brief Enable Motors.
 */
void Motion1D::motorsOn()
{
	digitalWrite(m_en_pin, LOW) ;
	m_motorsEnabled = 1;
}
//====================================================================================

/*!
 * \brief Toggle Motors.
 */
void Motion1D::toggleMotors()
{
	if (m_motorsEnabled) {
		motorsOff();
	} else {
		motorsOn();
	}
}
//====================================================================================

void Motion1D::setCutterUpReal(int d)
{
	penservo.write(m_cutterUpPos);
	m_cutterState = m_cutterUpPos;
	if (d) delay(d);
}
//====================================================================================

void Motion1D::setCutterDownReal(int d)
{
	penservo.write(m_cutterDownPos);
	m_cutterState = m_cutterDownPos;
	if (d) delay(d);
}
//====================================================================================

void Motion1D::toggleCutterReal(int d)
{
	if (m_cutterState == m_cutterUpPos) {
		setCutterDown(d);
	} else {
		setCutterUp(d);
	}
}
//====================================================================================


/*!
 * \brief Prepare and start move.
 */
void Motion1D::goToReal(uint16_t duration, int xSteps)
{
#ifdef USE_ACCEL_STEPPER
	if (!m_motorsEnabled) {motorsOn();}
	//set Coordinates and Speed
	m_xMotor->move(xSteps);
	if (xSteps < 0) xSteps = -xSteps;
	m_xMotor->setSpeed( (xSteps * 1000) / duration );
#else
	uint64_t tmp;
	if (in_motion) { Serial.print("ERROR\n"); return; }
	if (!m_motorsEnabled) { motorsOn(); }
	setTimer1Callback(NULL);
	int_active      = 0;
	in_motion       = 0;
	x_pulse         = 0;
	if (duration == 0) duration = 100;
	/* Set target */
	x_target += xSteps;
	/* Set direction pin */
	if (x_target > x_pos) digitalWrite(m_x_dir, HIGH); else digitalWrite(m_x_dir, LOW);
#ifdef USE_RAMP
	x_pos_start = x_pos;
	if (x_target > x_pos) {
		x_dir = 1;
		x_pos_middle = x_pos - 2 + ((x_target - x_pos)>>1);
	} else {
		x_dir = 0;
		x_pos_middle = x_pos + 2 - ((x_pos-x_target)>>1);
	}
	x_ramp_pos   = 0;
	x_ramp_iter  = 0;
	x_ramp_phase = 0;
#endif	
	/* ABS */
	if (xSteps < 0) xSteps = -xSteps;
	/* Set period (timer1 clock  = 80MHz) */
	if (xSteps) {
		tmp = duration;
		tmp *= 80000;
		tmp /= xSteps;
		tmp--;
	} else {
		tmp = 160000;
	}
	if (tmp < MIN_PERIOD) tmp = MIN_PERIOD;
#ifdef USE_RAMP
	if (tmp < RMAXIMUM_PERIOD) tmp = RMAXIMUM_PERIOD;

	if (tmp < RSTART_STOP_PERIOD) {
		x_target_hperiod = RMAXIMUM_HPERIOD;
		x_ramp_phase     = 1;
		tmp = RSTART_STOP_PERIOD;
	}
#endif
	/* Calculate half period */
	x_hperiod = ((tmp >> 1)&0xffffffff);

	/* Start timer1 */
	in_motion  = 1;
	int_active = 1;
	Serial.printf("GoTo %d, hperiod = %d, duration = %d, xsteps = %d\n\r",x_target, x_hperiod, duration, xSteps);
	x_time = (GetCycleCount() + microsecondsToClockCycles(500));
	setTimer1Callback(motion_intr_handler);
#endif
}
//====================================================================================

void Motion1D::stop()
{
#ifdef MOTION_QUEUE_SIZE
	/* Flush the motion Queue */
	m_motionQWr   = 0;
	m_motionQRd   = 0;
#endif
	/* Soft stop */
	x_target = x_pos;
}
//====================================================================================


/*!
 * \brief Executed in main loop.
 */
boolean Motion1D::loop()
{
#ifdef USE_ACCEL_STEPPER
#ifdef MOTION_QUEUE_SIZE
	if ( m_xMotor->distanceToGo() ) {
		m_xMotor->runSpeedToPosition();
	} else {
		motionQ_pull();
	}
	return motionQ_is_full();
#else
	while ( m_xMotor->distanceToGo() ) {
		m_xMotor->runSpeedToPosition();
		yield();
	}
	return 0;
#endif
#else
#ifdef MOTION_QUEUE_SIZE
	if (in_motion) {
		if (int_active == 0) {
			setTimer1Callback(NULL);
			in_motion = 0;
			motionQ_pull();
		}
	} else {
		motionQ_pull();
	}
	return motionQ_is_full();
#else
	if (in_motion) {
		if (int_active == 0) {
			setTimer1Callback(NULL);
			in_motion = 0;
		}
	}
	return in_motion;
#endif
#endif
}
//====================================================================================


#ifndef USE_ACCEL_STEPPER

// Speed critical bits
#pragma GCC optimize ("O2")
// Normally would not want two copies like this, but due to different
// optimization levels the inline attribute gets lost if we try the
// other version.

static inline ICACHE_RAM_ATTR uint32_t GetCycleCountIRQ()
{
	uint32_t ccount;
	__asm__ __volatile__("rsr %0,ccount":"=a"(ccount));
	return ccount;
}
//===========================================================================================

static uint32_t ICACHE_RAM_ATTR motion_intr_handler(void)
{
	if (int_active == 0) return 10000;
	uint32_t d0 = 0, now = GetCycleCountIRQ();
	int32_t expiryToGo;

	/* Process move */
	expiryToGo = (x_time - now);
	if (expiryToGo <= 0) {
		if (x_pulse) {
			asm volatile ("" : : : "memory");
			gpio_r->out_w1tc = (uint32_t)(x_gpio_mask);
			x_pulse = 0;
			if (x_pos == x_target) {
				/* Disable timer */
				x_time    = 0;
				x_hperiod = 0;
			}
#ifdef USE_RAMP
			/* Tabled ramp implementation */
			else if (x_ramp_phase) {
				if (x_ramp_phase == 1) {
					/* Ramp UP */
					if (x_hperiod > x_target_hperiod) {
						if (x_ramp_iter == 0) {
							x_hperiod--;
							x_ramp_iter = ramp[x_ramp_pos++];
							/* Recalculate middle point */
							if (x_hperiod == x_target_hperiod) {
								if (x_dir == 1) {
									x_ramp_len   = x_pos - x_pos_start;
									x_pos_middle = x_target - x_ramp_len;
								} else {
									x_ramp_len = x_pos_start - x_pos;
									x_pos_middle = x_target + x_ramp_len;
								}
							}
						} else {
							x_ramp_iter--;
						}
					}
					/* Check middle point */
					if (x_dir == 1) {
						if (x_pos > x_pos_middle) {x_ramp_phase = 2;}
					} else {
						if (x_pos < x_pos_middle) {x_ramp_phase = 2;}
					}
				} else {
					/* Ramp DOWN */
					if (x_hperiod < RSTART_STOP_HPERIOD) {
						if (x_ramp_iter == 0) {
							x_hperiod++;
							x_ramp_iter = ramp[x_ramp_pos--];
						} else {
							x_ramp_iter--;
						}
					}
				}
			}
#endif
			x_time += x_hperiod;
		} else if (x_pos != x_target) {
			asm volatile ("" : : : "memory");
			gpio_r->out_w1ts = (uint32_t)(x_gpio_mask);
			x_time += x_hperiod;
			x_pulse = 1;
			if (x_pos > x_target) x_pos--; else x_pos++;
		} else {
			x_time = 0;
		}
	}

	/* calculate next event time */
	if (x_time == 0) {
		/* We are done :-) */
		int_active = 0;
		last_time  = now;
		return 10000;
	} else {
		d0 = x_time - now;
	}
	return d0;
}
//===========================================================================================

#endif
