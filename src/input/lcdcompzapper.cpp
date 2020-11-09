/* FCE Ultra - NES/Famicom Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "share.h"

static uint32 lcdCompZapperStrobe[2];
static uint32 lcdCompZapperData[2];

const uint8 CHEATZAP_LIGHT_ON = 0;
const uint8 CHEATZAP_TRIGGER_ON = 24;
const uint8 CHEATZAP_BOTH_ON = 16;
const uint8 CHEATZAP_BOTH_OFF = 8;

uint32 last = 3;
bool inProcess = false;
short state = 0;
int count = 0;

static uint8 ReadLCDCompZapper(int w)
{
	if (inProcess) {
		count++;

		if (state == 0) {
			if (count == 45) {
				state = 1;
				count = 0;
			}

			return 24;
		}

		if (state == 1) {
			if (count == 2750) {
				state = 2;
				count = 0;
			}

			return 8;
		}

		if (state == 2) {
			state = 3;
			count = 0;

			return 0;
		}

		if (state == 3) {
			state = 0;
			count = 0;
			inProcess = false;

			return 8;
		}
	}

	if (lcdCompZapperData[1] != last)
	{
		printf("lcdCompZapperData[1]=%d ", lcdCompZapperData[1]);
		switch (lcdCompZapperData[1]) {
		case CHEATZAP_LIGHT_ON:
			printf("LIGHT_ON");
			break;
		case CHEATZAP_TRIGGER_ON:
			printf("CHEATZAP_TRIGGER_ON");
			break;
		case CHEATZAP_BOTH_ON:
			printf("CHEATZAP_BOTH_ON");
			break;
		case CHEATZAP_BOTH_OFF:
			printf("CHEATZAP_BOTH_OFF");
			break;
		}

		printf("\n");
		last = lcdCompZapperData[1];
	}

	if (lcdCompZapperData[w] == CHEATZAP_TRIGGER_ON)
	{
		inProcess = true;
		state = 0;
		count = 1;
		return 24;
	}

	return lcdCompZapperData[w];
	// return CHEATZAP_LIGHT_ON;
	// return CHEATZAP_TRIGGER_ON;
	// return CHEATZAP_BOTH_ON;
}

static void StrobeLCDCompZapper(int w)
{
	lcdCompZapperStrobe[w] = 0;
}

void UpdateLCDCompZapper(int w, void* data, int arg)
{
	// In the '(*(uint32*)data)' variable, bit 0 holds the trigger value and bit 1 holds the light sense value.
	// Ultimately this needs to be converted from 0000 00lt to 000t l000 where l is the light bit and t
	// is the trigger bit.
	// l must be inverted because 0: detected; 1: not detected
	lcdCompZapperData[w] = ((((*(uint32*)data) & 1) << 4) | 
		                (((*(uint32*)data) & 2 ^ 2) << 2));
}

static INPUTC LCDCompZapperCtrl = { ReadLCDCompZapper,0,StrobeLCDCompZapper,UpdateLCDCompZapper,0,0 };

INPUTC* FCEU_InitLCDCompZapper(int w)
{
	lcdCompZapperStrobe[w] = lcdCompZapperData[w] = 0;
	return(&LCDCompZapperCtrl);
}
