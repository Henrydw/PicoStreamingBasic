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
		for (channel = 0; channel < bufferInfo->unit->channelCount; channel++)
		{
			if (bufferInfo->unit->channelSettings[channel].enabled)
			{
				if (bufferInfo->appBuffer && bufferInfo->devBuffer)
				{
					memcpy_s(&bufferInfo->appBuffer[channel][startIndex], noOfSamples * sizeof(int16_t),
						&bufferInfo->devBuffer[channel][startIndex], noOfSamples * sizeof(int16_t));
				}
			}
		}

		for (int port = 0; port < bufferInfo->unit->digitalPortCount; port++)
		{
			if (bufferInfo->unit->digitalPortSettings[port].enabled)
			{
				if (bufferInfo->appMSOBuffer && bufferInfo->devMSOBuffer)
				{
					memcpy_s(&bufferInfo->appMSOBuffer[port][startIndex], noOfSamples * sizeof(int16_t),
						&bufferInfo->devMSOBuffer[port][startIndex], noOfSamples * sizeof(int16_t));
				}
			}
		}
	}
}

int main(void)
{

	int8_t devChars[] =
		"1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#";

	int8_t serial[12] = "GU037/0040\0"; // this will need to be inlcuded in a setup wizard if changing harware. Ensures it only opens the relevant picoscope
	
	PICO_STATUS status;
	
	UNIT scope;

	//device batch/serial: GU037/0040
	scope.resolution = PS5000A_DR_14BIT;

	scope.streamBufferSize = 250000; /* make sure overview buffer is large enough */


	status = ps5000aOpenUnit(&scope.handle, serial, scope.resolution);
	status = ps5000aMaximumValue(scope.handle, &scope.maxADCValue);

	status = ps5000aCurrentPowerSource(scope.handle);

	// Filling UNIT Data structure

	scope.channelCount = 4;

	for (int i = 0; i < scope.channelCount; i++)
	{
		// Do not enable channels C and D if power supply not connected for PicoScope 544XA/B devices
		if (scope.channelCount == QUAD_SCOPE && status == PICO_POWER_SUPPLY_NOT_CONNECTED && i >= DUAL_SCOPE)
		{
			scope.channelSettings[i].enabled = FALSE;
		}
		else
		{
			scope.channelSettings[i].enabled = TRUE;
		}

		scope.channelSettings[i].coupling = PS5000A_DC;
		scope.channelSettings[i].range = PS5000A_5V;
		scope.channelSettings[i].analogueOffset = 0.0f;
	}

	scope.digitalPortCount = 2;
	
	for (int i = 0; i < MAX_MSO_CHANNELS; i++)
	{
		if (i <= scope.digitalPortCount - 1 )
		{
			scope.digitalPortSettings[i].enabled = TRUE;
			scope.digitalPortSettings[i].logicLevel = (int16_t)((1.5 / 5) * scope.maxADCValue);// (1/5)*32767 = 6553 (1V threshold) 
		}
		else
		{
			scope.digitalPortSettings[i].enabled = FALSE;
			scope.digitalPortSettings[i].logicLevel = (int16_t)((1.5 / 5) * scope.maxADCValue);// (1/5)*32767 = 6553 (1V threshold) 
		}
	}

	// Writing information UNIT to device

	// Write the Analogue channel Information
	for (int i = 0; i < scope.channelCount; i++)
	{
		if (scope.channelSettings[i].enabled)
		{
			status = ps5000aSetChannel(scope.handle,
				(PS5000A_CHANNEL)i,
				scope.channelSettings[i].enabled,
				scope.channelSettings[i].coupling, 
				scope.channelSettings[i].range,
				scope.channelSettings[i].analogueOffset);
		}
	}


	// Write digital channel settings
	for (int i = 0; i < MAX_MSO_CHANNELS; i++)
	{
		status = ps5000aSetDigitalPort(scope.handle,
			(PS5000A_CHANNEL)(PS5000A_DIGITAL_PORT0 + i),
			scope.digitalPortSettings[i].enabled,
			scope.digitalPortSettings[i].logicLevel);
	}

	//Set timebase
	uint32_t timebase = 315;// approximately 200kHz
	int32_t noSamples = scope.streamBufferSize;
	int32_t timeInterval;
	int32_t maxSamples;

	status = ps5000aGetTimebase(scope.handle, timebase, noSamples, &timeInterval, &maxSamples, 0);

	printf("Time interval:  %d, Max samples: %d\n", timeInterval, maxSamples);


	// Disable triggers
	status = ps5000aSetSimpleTrigger(scope.handle , 0, (PS5000A_CHANNEL)(PS5000A_CHANNEL_A), 0, PS5000A_RISING, 0, 0);



	// ------------------------------------------------- allocate buffers -------------------------------------------------

	// ---------------- Create buffer pointers ----------------

	// buffer pointer pointers
	BUFFER_INFO bufferInfo;

	// buffer pointer arrays
	int16_t* devBuffers[PS5000A_MAX_CHANNELS];
	int16_t* appBuffers[PS5000A_MAX_CHANNELS];
	
	int16_t* devMSOBuffers[MAX_MSO_CHANNELS];
	int16_t* appMSOBuffers[MAX_MSO_CHANNELS];

	// ---------------- allocate analogue buffers ----------------

	for (int i = 0; i < scope.channelCount; i++)
	{
		if (scope.channelSettings[i].enabled)
		{
			devBuffers[i] = (int16_t*)calloc(scope.streamBufferSize, sizeof(int16_t));

			status = ps5000aSetDataBuffer(scope.handle, (PS5000A_CHANNEL)i, devBuffers[i], scope.streamBufferSize, 0, PS5000A_RATIO_MODE_NONE);

			appBuffers[i] = (int16_t*)calloc(scope.streamBufferSize, sizeof(int16_t));

			printf(status ? "StreamDataHandler:ps5000aSetDataBuffers(channel %ld) ------ 0x%08lx \n" : "", i, status);
		}
	}

	// ---------------- allocate MSO buffers ----------------

	for (int i = 0; i < scope.digitalPortCount; i++)
	{
		devMSOBuffers[i] = (int16_t*)calloc(scope.streamBufferSize, sizeof(int16_t));

		status = ps5000aSetDataBuffer(scope.handle,
			(PS5000A_CHANNEL)(PS5000A_DIGITAL_PORT0 + i),
			devMSOBuffers[i],
			scope.streamBufferSize,
			0,
			PS5000A_RATIO_MODE_NONE);

		appMSOBuffers[i] = (int16_t*)calloc(scope.streamBufferSize, sizeof(int16_t));

		printf(status ? "StreamDataHandler:ps5000aSetDataBuffers(Digital Ports %ld) ------ 0x%08lx \n" : "", i, status);
	}

	
	bufferInfo.unit = &scope;
	bufferInfo.devBuffer = devBuffers;
	bufferInfo.appBuffer = appBuffers;
	bufferInfo.devMSOBuffer = devMSOBuffers;
	bufferInfo.appMSOBuffer = appMSOBuffers;


	// ------------------------------------------------- start streaming -------------------------------------------------
	uint32_t downsampleRatio = 1;
	PS5000A_TIME_UNITS timeUnits = PS5000A_NS;
	uint32_t sampleInterval = 5000;
	PS5000A_RATIO_MODE ratioMode = PS5000A_RATIO_MODE_NONE;
	uint32_t preTrigger = 0;
	uint32_t postTrigger = 1000;
	int16_t autostop = 0;

	status = ps5000aRunStreaming(scope.handle, &sampleInterval, timeUnits, preTrigger, postTrigger, autostop,downsampleRatio, ratioMode, scope.streamBufferSize);

	printf("\n\nSample interval:  %d\n\n", sampleInterval);

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

	// Get timestamp of steam start

	time_t rawtime;
	struct tm timeinfo;
	char buffer[80];

	time(&rawtime);
	localtime_s(&timeinfo ,&rawtime);

	strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S", &timeinfo);
	std::string startTime(buffer);

	std::cout << startTime << std::endl;

	// Open File stream to save data
	FILE* fp = NULL;
	char streamFile[40] = "Layer_1.csv";
	fopen_s(&fp, streamFile, "w");

	bool streaming = true;
	int totalSamples = 0;
	int triggeredAt;

	// Write the File Meta information
	fprintf(fp,
		"Timestamp,  %s, SampleInterval, %d, VoltageRange, 5, BitDepth, 14\n",
		buffer,
		timeInterval,
		scope.resolution);

	// Write the Channel headings
	for (int j = 0; j < scope.channelCount; j++)
	{
		if (scope.channelSettings[j].enabled)
		{
			fprintf(fp,
				"%c_raw, %c_voltage,",
				devChars[10 + j],
				devChars[10 + j]);

		}
	}
	fprintf(fp,"D0");

	fprintf(fp, "\n");


	uint16_t digiStop = 0;

	int16_t bit;

	uint16_t bitValue;
	uint16_t digiValue;

	// ---------------- Main Streaming App loop  ----------------


	while (!_kbhit() && !g_autoStopped && !digiStop)
	{

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
			printf("Collected %3li samples, index = %5lu, Total: %6d samples \n", g_sampleCount, g_startIndex, totalSamples);

			for (int i = g_startIndex; i < (int32_t)(g_startIndex + g_sampleCount); i++)
			{
				if (fp != NULL)
				{
					for (int j = 0; j < scope.channelCount; j++)
					{
						if (scope.channelSettings[j].enabled)
						{
							// do this for all enabled channels (reading, + voltage in mV
							fprintf(fp,
								"%5d, %+5d,",
								appBuffers[j][i],
								adc_to_mv(appBuffers[j][i], scope.channelSettings[j].range,0x7FFF));
						}
					}

					digiValue = 0x00ff & appMSOBuffers[1][i];	// Mask Port 1 values to get lower 8 bits
					digiValue <<= 8;												// Shift by 8 bits to place in upper 8 bits of 16-bit word
					digiValue |= appMSOBuffers[0][i];					// Mask Port 0 values to get lower 8 bits and apply bitwise inclusive OR to combine with Port 1 values

					// get D0 from the assembled MSO ports
					bit = (0x8000 >> 15) & digiValue ? 1 : 0;

					//fprintf(fp, "%d, ",bit);

					if (bit)
					{
						digiStop = TRUE;
					}

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
	for (int i = 0; i < scope.channelCount; i++)
	{
		if (scope.channelSettings[i].enabled)
		{
			free(devBuffers[i]);
			free(appBuffers[i]);
			status =  ps5000aSetDataBuffer(scope.handle, (PS5000A_CHANNEL)i, 0, 0, 0, PS5000A_RATIO_MODE_NONE);
		}
	}

	for (int i = 0; i < scope.digitalPortCount; i++)
	{
		free(devMSOBuffers[i]);
		free(appMSOBuffers[i]);
		status = ps5000aSetDataBuffer(scope.handle, (PS5000A_CHANNEL)(PS5000A_DIGITAL_PORT0 + i), 0, 0, 0, PS5000A_RATIO_MODE_NONE);

		printf(status ? "StreamDataHandler:ps5000aSetDataBuffers(Digital Ports %ld) ------ 0x%08lx \n" : "", i, status);
	}

	ps5000aCloseUnit(scope.handle);
	

	return 0;
}