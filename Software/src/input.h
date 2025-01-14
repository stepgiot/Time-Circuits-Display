/*
 * -------------------------------------------------------------------
 * CircuitSetup.us Time Circuits Display
 * (C) 2022-2023 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Time-Circuits-Display-A10001986
 *
 * Keypad_I2C Class, TCButton Class: I2C-Keypad and Button handling
 *
 * Keypad part inspired by "Keypad" library by M. Stanley & A. Brevig
 * Fractions of this code are customized, minimized derivates of parts 
 * of OneButton library by Matthias Hertel.
 * -------------------------------------------------------------------
 * License of Keypad_I2C class: MIT
 * 
 * Permission is hereby granted, free of charge, to any person 
 * obtaining a copy of this software and associated documentation 
 * files (the "Software"), to deal in the Software without restriction, 
 * including without limitation the rights to use, copy, modify, 
 * merge, publish, distribute, sublicense, and/or sell copies of the 
 * Software, and to permit persons to whom the Software is furnished to 
 * do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY 
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * -------------------------------------------------------------------
 * Original OneButton library 
 * Copyright (c) 2005-2014 by Matthias Hertel, http://www.mathertel.de/
 * 
 * License of original OneButton library and this program as regards
 * the TCButton class:
 * Software License Agreement (BSD License)
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met:
 * - Redistributions of source code must retain the above copyright 
 *   notice, this list of conditions and the following disclaimer. 
 * - Redistributions in binary form must reproduce the above copyright 
 *   notice, this list of conditions and the following disclaimer in 
 *   the documentation and/or other materials provided with the 
 *   distribution. 
 * - Neither the name of the copyright owners nor the names of its 
 *   contributors may be used to endorse or promote products derived 
 *   from this software without specific prior written permission. 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN 
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _TCINPUT_H
#define _TCINPUT_H

#include <Wire.h>

/*
 * Keypad_i2c class
 */

typedef enum {
    TCKS_IDLE,
    TCKS_PRESSED,
    TCKS_HOLD,
    TCKS_RELEASED
} KeyState;


struct KeyStruct {
    int       kCode;
    KeyState  kState;
    char      kChar;
    bool      stateChanged;
    unsigned long startTime;
};

class Keypad_I2C {

    public:

        Keypad_I2C(char *userKeymap, const uint8_t *row, const uint8_t *col, 
                   uint8_t numRows, uint8_t numCols,
                   int address, TwoWire *awire = &Wire);

        void begin();

        // Setter for custom delay function
        void setCustomDelayFunc(void (*myDelay)(unsigned int));

        void setScanInterval(unsigned int interval);
        void setHoldTime(unsigned int holdTime);

        void addEventListener(void (*listener)(char, KeyState));

        bool scanKeypad();

    private:

        bool scanKeys();
        void advanceState(bool kstate);
        void transitionTo(KeyState nextState);

        void (*_keypadEventListener)(char, KeyState);

        void pin_write(uint8_t pinNum, bool level);
        void port_write(uint8_t i2cportval);

        unsigned int  _scanInterval;
        unsigned int  _holdTime;
        const uint8_t *_rowPins;
        const uint8_t *_columnPins;
        uint8_t       _rows;
        uint8_t       _columns;
        char          *_keymap;
        int           _i2caddr;

        unsigned long _scanTime = 0;        
        uint16_t      _rowMask;

        uint8_t       _pinState;  // shadow for output pins

        KeyStruct     _key;

        TwoWire       *_wire;

        // Ptr to custom delay function
        void (*_customDelayFunc)(unsigned int) = NULL;
};

/*
 * TCButton class
 */

typedef enum {
    TCBS_IDLE,
    TCBS_PRESSED,
    TCBS_RELEASED,
    TCBS_LONGPRESS,
    TCBS_LONGPRESSEND
} ButtonState;

class TCButton {
  
    public:
        TCButton(const int pin, const boolean activeLow = true, const bool pullupActive = true);
      
        void setDebounceTicks(const int ticks);
        void setPressTicks(const int ticks);
        void setLongPressTicks(const int ticks);
      
        void attachPress(void (*newFunction)(void));
        void attachLongPressStart(void (*newFunction)(void));
        void attachLongPressStop(void (*newFunction)(void));

        void scan(void);

    private:

        void reset(void);
        void transitionTo(ButtonState nextState);

        void (*_pressFunc)(void) = NULL;
        void (*_longPressStartFunc)(void) = NULL;
        void (*_longPressStopFunc)(void) = NULL;

        int _pin;
        
        unsigned int _debounceTicks = 50;
        unsigned int _pressTicks = 400;
        unsigned int _longPressTicks = 800;
      
        int _buttonPressed;
      
        ButtonState _state     = TCBS_IDLE;
        ButtonState _lastState = TCBS_IDLE;
      
        unsigned long _startTime;
};

#endif
