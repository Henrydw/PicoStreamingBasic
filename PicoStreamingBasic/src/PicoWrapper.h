/*******************************************************************************
 *
 * Filename: PicoWrapper
 *
 * Description:
 *  This file contains all of the types/structs/classes used in the SDK examples but not in the SDK.
 *	THese are mainly for convenience/readability. Actauly functionality is still carried out by the SDK.
 *
 *	Supported PicoScope models:
 * (may work on others but not tested)
 *
 *		PicoScope 5442D
 * 
 * Supported platforms:
 * (may work on others but not tested)
 * 
 *		Windows x64
 *
 *	Dependancies:-
 *
 *		Ensure that the 32-/64-bit ps5000a.lib can be located
 *		Ensure that the ps5000aApi.h and PicoStatus.h files can be located
 *
 *	
 *
 ******************************************************************************/
#pragma once

#define QUAD_SCOPE		4
#define DUAL_SCOPE		2

#define MAX_PICO_DEVICES 64


uint16_t inputRanges[PS5000A_MAX_RANGES] = {
												10,
												20,
												50,
												100,
												200,
												500,
												1000,
												2000,
												5000,
												10000,
												20000,
												50000 };

typedef struct
{
	PS5000A_COUPLING coupling;
	PS5000A_RANGE range;
	int16_t enabled;
	float analogueOffset;
}CHANNEL_SETTINGS;

typedef struct
{
	int16_t enabled;
	int16_t logicLevel;
}DIGITAL_PORT_SETTINGS;

typedef struct
{
	int16_t handle;
	//MODEL_TYPE				model;
	int8_t						modelString[8];
	int8_t						serial[10];
	int16_t						complete;
	int16_t						openStatus;
	int16_t						openProgress;
	PS5000A_RANGE				firstRange;
	PS5000A_RANGE				lastRange;
	PS5000A_DEVICE_RESOLUTION	resolution;
	int16_t						maxADCValue;
	//SIGGEN_TYPE				sigGen;
	int16_t						hasHardwareETS;
	uint16_t					awgBufferSize;
	uint16_t					streamBufferSize;
	int16_t						channelCount;
	CHANNEL_SETTINGS			channelSettings[PS5000A_MAX_CHANNELS];
	int16_t						digitalPortCount;
	DIGITAL_PORT_SETTINGS		digitalPortSettings[2];
}UNIT;

typedef struct tBufferInfo
{
	UNIT* unit;
	int16_t** devBuffer;
	int16_t** appBuffer;

} BUFFER_INFO;


/****************************************************************************
* adc_to_mv
*
* Convert an 16-bit ADC count into millivolts
****************************************************************************/
int32_t adc_to_mv(int32_t raw, uint32_t rangeIndex, int16_t maxADCValue)
{
	return (raw * inputRanges[rangeIndex]) / maxADCValue;
}


