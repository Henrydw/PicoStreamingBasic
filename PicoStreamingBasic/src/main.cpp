#include "ps5000aApi.h"
#include <conio.h>
#include <windows.h>
#include "PicoWrapper.h"
#include <stdio.h>
//For Timestamp
#include <iostream>
#include <ctime>
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
	BUFFER_INFO * bufferInfo = NULL;

	if (pParameter != NULL)
	{
		bufferInfo = (BUFFER_INFO *)pParameter;
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
			memcpy_s(&bufferInfo->appBuffer[startIndex], noOfSamples * sizeof(int16_t),
				     &bufferInfo->devBuffer[startIndex], noOfSamples * sizeof(int16_t));
		}
	}
}


int main(void)
{
	PICO_STATUS status;
	UNIT scope;
	//device batch/serial: GU037/0040
	scope.resolution = PS5000A_DR_16BIT;
	int8_t serial[12] = "GU037/0040\0"; // this will need to be inlcuded in a setup wizard if changing harware. Ensures it only opens the relevant picoscope
	
	int sampleCount = 50000; /* make sure overview buffer is large enough */

	status = ps5000aOpenUnit(&scope.handle, serial, scope.resolution);
	// Powersupply warnings
	// < USB 3.0 warnings
	
	// Set channels: Enable/Disable, AC/DC, voltage range, off-set
	status = ps5000aSetChannel(scope.handle, PS5000A_CHANNEL_A, 1, PS5000A_DC, PS5000A_5V, 0);
	status = ps5000aSetChannel(scope.handle, (PS5000A_CHANNEL)(PS5000A_CHANNEL_B), 0, PS5000A_DC, PS5000A_5V, 0);
	status = ps5000aSetChannel(scope.handle, (PS5000A_CHANNEL)(PS5000A_CHANNEL_C), 0, PS5000A_DC, PS5000A_5V, 0);
	status = ps5000aSetChannel(scope.handle, (PS5000A_CHANNEL)(PS5000A_CHANNEL_D), 0, PS5000A_DC, PS5000A_5V, 0);

	status = ps5000aSetDigitalPort(scope.handle, (PS5000A_CHANNEL)(PS5000A_DIGITAL_PORT0), 0, 0);
	status = ps5000aSetDigitalPort(scope.handle, (PS5000A_CHANNEL)(PS5000A_DIGITAL_PORT1), 0, 0);


	//Set timebase
	uint32_t timebase = 625;// approximately 200kHz
	int32_t noSamples = 100000;
	int32_t timeInterval;
	int32_t maxSamples;

	status = ps5000aGetTimebase(scope.handle, timebase, noSamples, &timeInterval, &maxSamples, 0);

	printf("Time interval:  %d, Max samples: %d", timeInterval, maxSamples);


	// Disable triggers
	status = ps5000aSetSimpleTrigger(scope.handle , 0, (PS5000A_CHANNEL)(PS5000A_CHANNEL_A), 0, PS5000A_RISING, 0, 0);

	// allocate memory and assign buffers
	int16_t* devBuffer = (int16_t*) calloc(sampleCount, sizeof(int16_t));
	status = ps5000aSetDataBuffer(scope.handle, PS5000A_CHANNEL_A, devBuffer, sampleCount, 0, PS5000A_RATIO_MODE_NONE);

	int16_t* appBuffer = (int16_t*)calloc(sampleCount, sizeof(int16_t));
	
	BUFFER_INFO bufferInfo;

	bufferInfo.unit = &scope;
	bufferInfo.devBuffer = devBuffer;
	bufferInfo.appBuffer = appBuffer;

	//start streaming
	uint32_t downsampleRatio = 1;
	PS5000A_TIME_UNITS timeUnits = PS5000A_US;
	uint32_t sampleInterval = 1;
	PS5000A_RATIO_MODE ratioMode = PS5000A_RATIO_MODE_NONE;
	uint32_t preTrigger = 0;
	uint32_t postTrigger = 1000;
	int16_t autostop = 0;

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

	g_autoStopped = FALSE;

	time_t rawtime;
	struct tm timeinfo;
	char buffer[80];

	time(&rawtime);
	localtime_s(&timeinfo ,&rawtime);

	strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S", &timeinfo);
	std::string str(buffer);

	std::cout << str;

	// Open File stream to save data
	FILE* fp = NULL;
	char streamFile[20] = "stream.txt";
	fopen_s(&fp, streamFile, "w");

	bool streaming = true;
	int totalSamples = 0;
	int triggeredAt;

	while (!_kbhit() && !g_autoStopped)
	{

		// Streaming and Save
		/* Poll until data is received. Until then, GetStreamingLatestValues wont call the callback */
		g_ready = FALSE;

		status = ps5000aGetStreamingLatestValues(scope.handle, callBackStreaming, &bufferInfo);

		printf("Pico Stream Lastest status: %d",status);

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
					// do this for all enabled channels (reading, + voltage in mV
					fprintf(fp,
						"A,  %5d, %+5d",
						appBuffer[i],
						adc_to_mv(appBuffer[i], PS5000A_5V, &scope));

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