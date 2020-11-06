#include "ps5000aApi.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

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
	int16_t handle;
	//MODEL_TYPE				model;
	int8_t						modelString[8];
	int8_t						serial[10];
	int16_t						complete;
	int16_t						openStatus;
	int16_t						openProgress;
	PS5000A_RANGE			firstRange;
	PS5000A_RANGE			lastRange;
	int16_t						channelCount;
	int16_t						maxADCValue;
	//SIGGEN_TYPE				sigGen;
	int16_t						hasHardwareETS;
	uint16_t					awgBufferSize;
	//CHANNEL_SETTINGS	channelSettings[PS5000A_MAX_CHANNELS];
	PS5000A_DEVICE_RESOLUTION	resolution;
	int16_t						digitalPortCount;
}UNIT;

typedef struct tBufferInfo
{
	UNIT* unit;
	int16_t** devBuffer;
	int16_t** appBuffer;

} BUFFER_INFO;

// Global variables

int16_t			g_autoStopped;
int16_t   	g_ready = FALSE;
uint64_t 		g_times[PS5000A_MAX_CHANNELS];
int16_t     g_timeUnit;
int32_t     g_sampleCount;
uint32_t		g_startIndex;
int16_t			g_trig = 0;
uint32_t		g_trigAt = 0;
int16_t			g_overflow = 0;



/****************************************************************************
* adc_to_mv
*
* Convert an 16-bit ADC count into millivolts
****************************************************************************/
int32_t adc_to_mv(int32_t raw, int32_t rangeIndex, UNIT* unit)
{
	return (raw * inputRanges[rangeIndex]) / unit->maxADCValue;
}


/****************************************************************************
* callbackStreaming
* Used by ps5000a data streaming collection calls, on receipt of data.
* Used to set global flags etc checked by user routines
****************************************************************************/
void PREF4 callBackStreaming(int16_t handle,
	int32_t noOfSamples,
	uint32_t startIndex,
	int16_t overflow,
	uint32_t triggerAt,
	int16_t triggered,
	int16_t autoStop,
	void* pParameter)
{
	int32_t channel;
	BUFFER_INFO* bufferInfo = NULL;

	if (pParameter != NULL)
	{
		bufferInfo = (BUFFER_INFO*)pParameter;
	}
	else
	{
		printf("Buffer info parameter not copied....");
	}

	// used for streaming
	g_sampleCount = noOfSamples;
	g_startIndex = startIndex;
	g_autoStopped = autoStop;

	// flag to say done reading data
	g_ready = TRUE;

	// flags to show if & where a trigger has occurred
	g_trig = triggered;
	g_trigAt = triggerAt;

	g_overflow = overflow;

	if (bufferInfo != NULL && noOfSamples)
	{
		if (bufferInfo->appBuffer && bufferInfo->devBuffer)
		{
			memcpy_s(bufferInfo->appBuffer[startIndex], noOfSamples * sizeof(int16_t),
				bufferInfo->devBuffer[startIndex], noOfSamples * sizeof(int16_t));
		}
	}
}

int main(void)
{
	PICO_STATUS status;
	UNIT scope;
	scope.resolution = PS5000A_DR_16BIT;
	// device batch/serial: GU037/0040
	int8_t serial[12] = "GU037/0040\0"; // this will need to be inlcuded in a setup wizard if changing harware. Ensures it only opens the relevant picoscope....
	
	
	
	status = ps5000aOpenUnit(&scope.handle, serial, scope.resolution);
	
	if (status == PICO_OK || status == PICO_USB3_0_DEVICE_NON_USB3_0_PORT)
	{
		printf("PicoScope Connected\n");
	}
	else if (status == PICO_POWER_SUPPLY_NOT_CONNECTED)
	{
		printf("No Power Supply");
	}
	else
	{
		printf("Error code : %x\n", status);
	}
	
	// Set channels: Enable/Disable, AC/DC, voltage range, off-set

	status = ps5000aSetChannel(scope.handle, PS5000A_CHANNEL_A, 1, PS5000A_DC, PS5000A_5V, 0);
	if (status == PICO_OK)
	{
		printf("Channel A enabled\n");
	}
	else
	{
		printf("Error code : %x\n", status);
	}

	for (int i = 1; i < 4; i++)
	{
		status = ps5000aSetChannel(scope.handle, (PS5000A_CHANNEL)(PS5000A_CHANNEL_A + i), 0, PS5000A_DC, PS5000A_5V, 0);
		if (status == PICO_OK)
		{
			printf("Channel disabled\n");
		}
		else
		{
			printf("Error code : %x\n", status);
		}
	}

	// Disable triggers
	status = ps5000aSetSimpleTrigger(scope.handle , 0, (PS5000A_CHANNEL)(PS5000A_CHANNEL_A), 0, PS5000A_RISING, 0, 0);
	if (status == PICO_OK)
	{
		printf("Trigger disabled\n");
	}
	else
	{
		printf("Error code : %x\n", status);
	}

	int sampleCount = 50000; /* make sure overview buffer is large enough */

	// allocate memory and assign buffers
	int16_t* devBuffer = (int16_t*)calloc(sampleCount, sizeof(int16_t));
	status = ps5000aSetDataBuffer(scope.handle, (PS5000A_CHANNEL)0, devBuffer, sampleCount, 0, PS5000A_RATIO_MODE_NONE);
	if (status == PICO_OK)
	{
		printf("Buffer assigned\n");
	}
	else
	{
		printf("Error code : %x\n", status);
	}
	int16_t* appBuffer = (int16_t*)calloc(sampleCount, sizeof(int16_t));
	
	BUFFER_INFO bufferInfo;

	bufferInfo.unit = &scope;
	bufferInfo.devBuffer = &devBuffer;
	bufferInfo.appBuffer = &appBuffer;

	//start streaming
	uint32_t downsampleRatio = 1;
	PS5000A_TIME_UNITS timeUnits = PS5000A_US;
	uint32_t sampleInterval = 1;
	PS5000A_RATIO_MODE ratioMode = PS5000A_RATIO_MODE_NONE;
	uint32_t preTrigger = 0;
	uint32_t postTrigger = 1000000;
	int16_t autostop = 1;

	status = ps5000aRunStreaming(scope.handle, &sampleInterval, timeUnits, preTrigger, postTrigger, autostop,downsampleRatio, ratioMode, sampleCount);

	if (status != PICO_OK)
	{
		// PicoScope 5X4XA/B/D devices...+5 V PSU connected or removed or
		// PicoScope 524XD devices on non-USB 3.0 port
		if (status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED ||
			status == PICO_USB3_0_DEVICE_NON_USB3_0_PORT || status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
		{
			printf("Using wrong power source\n");
		}
		else
		{
			printf("streamDataHandler:ps5000aRunStreaming ------ 0x%08lx \n", status);
		}
	}
	else
	{
		printf("Streaming Started\n");
	}


	// Open File stream to save data
	FILE* fp = NULL;
	char streamFile[20] = "stream.txt";
	fopen_s(&fp, streamFile, "w");

	bool streaming = true;
	int totalSamples = 0;
	int triggeredAt;

	while (streaming)
	{
		if (GetAsyncKeyState(VK_RETURN))
		{
			streaming = false;
			continue;
		}
		// Streaming and Save
		/* Poll until data is received. Until then, GetStreamingLatestValues wont call the callback */
		g_ready = FALSE;

		status = ps5000aGetStreamingLatestValues(scope.handle, callBackStreaming, &bufferInfo);

		if (g_ready && g_sampleCount > 0) /* Can be ready and have no data, if autoStop has fired */
		{
			if (g_trig)
			{
				triggeredAt = totalSamples + g_trigAt;		// Calculate where the trigger occurred in the total samples collected
				printf("Trig. at index %lu total %lu", g_trigAt, triggeredAt + 1);	// show where trigger occurred
			}

			totalSamples += g_sampleCount;
			printf("\nCollected %3li samples, index = %5lu, Total: %6d samples ", g_sampleCount, g_startIndex, totalSamples);

			for (int i = g_startIndex; i < (int32_t)(g_startIndex + g_sampleCount); i++)
			{

				if (fp != NULL)
				{
					fprintf(fp,
						"Ch A  %5d",
						appBuffer[i]);// ,
						//adc_to_mv(appBuffer[i], PS5000A_5V, &scope));

					fprintf(fp, "\n");
				}
				else
				{
					printf("Cannot open the file %s for writing.\n", streamFile);
				}

			}
		}
	}
	ps5000aStop(scope.handle);
	printf("Streaming Finished");
	fclose(fp);
	
	// Clean Up
	free(devBuffer);
	free(appBuffer);
	ps5000aCloseUnit(scope.handle);
	

	return 0;
}