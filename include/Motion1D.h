/*
 * One axis (1D) motion class for ESP8266.
 *
 * Author: Rafal Vonau <rafal.vonau@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#ifndef __MOTION_1D__
#define __MOTION_1D__


#include "Arduino.h"
#include <stdint.h>
#include <c_types.h>
#include <eagle_soc.h>
#include <ets_sys.h>
#include "osapi.h"
#include "Command.h"

/*! Use AccelStepper lib or simple but fast (no ramp implementation) timer1 based solution */
//#define USE_ACCEL_STEPPER

#ifdef USE_ACCEL_STEPPER
#include "AccelStepper.h" // nice lib from http://www.airspayce.com/mikem/arduino/AccelStepper/
#endif

#define MOTION_QUEUE_SIZE (64)

#ifdef MOTION_QUEUE_SIZE
#define MOTION_QUEUE_MASK (MOTION_QUEUE_SIZE-1)
typedef struct motion_queue_s {
	uint16_t cmd;
	uint16_t duration;
	int x;
} motion_queue_t;
#endif


extern volatile int x_target;
extern volatile int x_pos;


class Motion1D
{
public:
	Motion1D(int step1, int dir1, int en_pin, int servoPin);

	void motorsOff();
	void motorsOn();
	void toggleMotors();
	
	void stop();

	boolean loop();

	void setCutterUpReal(int d = 0);
	void setCutterDownReal(int d = 0);
	void toggleCutterReal(int d = 0);
	void goToReal(uint16_t duration, int xSteps);


#ifdef MOTION_QUEUE_SIZE
	void setCutterUp(int d = 0)   {motionQ_push(2, d, 0);}
	void setCutterDown(int d = 0) {motionQ_push(3, d, 0);}
	void toggleCutter(int d = 0)  {motionQ_push(4, d, 0);}
	void goTo(uint16_t duration, int xSteps) {motionQ_push(1, duration, xSteps);}

	void motionQ_push(uint16_t cmd, uint16_t duration, int x) {
		int pos = m_motionQWr;
		motion_queue_t *v = &m_motionQ[pos];
		v->cmd = cmd;
		v->duration = duration;
		v->x = x;
		pos++;
		pos &= MOTION_QUEUE_MASK;
		m_motionQWr = pos;
	}
	
	bool motionQ_is_full() {
		int pos = m_motionQWr;
		pos++;
		pos &= MOTION_QUEUE_MASK;
		if (pos == m_motionQRd) return true;
		pos++;
		pos &= MOTION_QUEUE_MASK;
		if (pos == m_motionQRd) return true;
		pos++;
		pos &= MOTION_QUEUE_MASK;
		if (pos == m_motionQRd) return true;
		return false;
	}

	bool motionQ_is_empty() {
		if (m_motionQWr == m_motionQRd) return true;
		return false;
	}


	void motionQ_pull() {
		if (m_motionQWr != m_motionQRd) {
			int pos = m_motionQRd;
			motion_queue_t *v = &m_motionQ[pos];
			switch (v->cmd) {
				case 1: goToReal(v->duration, v->x); break;
				case 2: setCutterUpReal(v->duration); break;
				case 3: setCutterDownReal(v->duration); break;
				case 4: toggleCutterReal(v->duration); break;
				default: break;
			}
			pos++;
			pos &= MOTION_QUEUE_MASK;
			m_motionQRd = pos;
		}
	}
#else
	void setCutterUp(int d = 0) {setCutterUpReal(d);}
	void setCutterDown(int d = 0) {setCutterDownReal(d);}
	void toggleCutter(int d = 0) {toggleCutterReal(d);}
	void goTo(uint16_t duration, int xSteps) {goToReal(duration, xSteps);}
#endif
	void printStat(CommandQueueItem *c);
public:
#ifdef USE_ACCEL_STEPPER
	AccelStepper *m_xMotor;
#else
	int           m_x_dir;
#endif
	boolean       m_motorsEnabled;
	int           m_en_pin;
	/* Servo */
	int           m_cutterState;
	int           m_cutterUpPos;
	int           m_cutterDownPos;
#ifdef MOTION_QUEUE_SIZE
	motion_queue_t m_motionQ[MOTION_QUEUE_SIZE];
	int            m_motionQWr;
	int            m_motionQRd;
#endif
};

#endif