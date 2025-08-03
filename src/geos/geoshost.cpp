#include "dosbox.h"
#include "inout.h"
#include "control.h"
#include "mouse.h"
#include "cpu.h"
#include "timer.h"
#include "sdlmain.h"
#include "../ints/int10.h"
#include <SDL.h>

#if C_GEOSHOST



#define HIF_CHECK_API			98
#define HIF_SET_VIDEO_PARAMS	4
#define HIF_SET_EVENT_INTERRUPT 5
#define HIF_EVENT_NOTIFICATION	6
#define HIF_EVENT_ASYNC_END		7
#define HIF_GET_VIDEO_PARAMS	8
#define HIF_GET_EVENT			9

#define HIF_OK					0
#define HIF_NOT_FOUND			1

const static char G_baseboxID[] = "XOBESAB2";
static uint8_t G_baseboxIDOffset = 1;

static uint8_t G_commandOffset = 0;
static uint16_t G_commandBuffer[6];
static uint8_t G_responseOffset = 0;
static uint16_t G_responseBuffer[6];
static uint8_t G_eventInterrupt = 0;
SDL_mutex* G_eventQueueMutex = NULL;
bool G_recheckEventInterrupt = false;
bool G_protectedOpMode = false;

class EventRecord {
private:
	uint16_t m_Payload[6];
	EventRecord* m_Next;

public:
	EventRecord(uint16_t* eventRecord, EventRecord* next) {
		m_Next = next;
		uint8_t loopCount = 0;
		while (loopCount < 6) {
			m_Payload[loopCount] = eventRecord[loopCount];
			loopCount++;
		}
	}
	~EventRecord() {
	}

public:
	void SetNext(EventRecord* nextEvent) {
		m_Next = nextEvent;
	};
	EventRecord* GetNext() {
		return m_Next;
	}
	void GetRecordData(uint16_t* recordBuf) {

		uint8_t loopCount = 0;
		while (loopCount < 6) {
			recordBuf[loopCount] = m_Payload[loopCount];
			loopCount++;
		}
	}
};

EventRecord* G_eventRecords = NULL;


static void GeosHost_TickHandler(void) {

	// if in the matching operation mode: real mode or protected mode
	if (G_eventInterrupt && (G_protectedOpMode == cpu.pmode)) {
		// if event interrupt is requested
		if (G_recheckEventInterrupt) {

			SDL_mutexP(G_eventQueueMutex);
			G_recheckEventInterrupt = false;
			SDL_mutexV(G_eventQueueMutex);
			// issue software interrupt
			CPU_SW_Interrupt(G_eventInterrupt, reg_eip);
			DOSBOX_RunMachine();
		} 
	}
}

void GeosHost_GeneralNotification() {

	// Send general notification event
}

void GeosHost_AsyncCompletion() {

	// Send async completion event
}

void GeosHost_SendEvent(uint16_t* eventRecord)
{
	// Create Event Object
	EventRecord* newRecord = new EventRecord(eventRecord, NULL);
	
	// Add Event to list
	SDL_mutexP(G_eventQueueMutex);
	if (G_eventRecords == NULL) {
		G_recheckEventInterrupt = true;
	}
	newRecord->SetNext(G_eventRecords);
	G_eventRecords = newRecord;
	SDL_mutexV(G_eventQueueMutex);
}

static uint16_t read_baseboxid(io_port_t, io_width_t)
{
	uint16_t result = 0;
	if (G_responseOffset > 0) {
		G_responseOffset--;
		result = G_responseBuffer[G_responseOffset];
	}
	G_commandOffset = 0;
	return result;
}

void GeosHost_NotifyVideoChange()
{
	// send event to GEOS client
	static uint16_t eventRecord[6];
	eventRecord[0] = HIF_EVENT_NOTIFICATION;

	GeosHost_SendEvent(eventRecord);
}

static uint32_t timer_callback(uint32_t intervall, void* param)
{
	// send event to GEOS client
	static uint16_t eventRecord[6];
	eventRecord[0] = HIF_EVENT_NOTIFICATION;

	GeosHost_SendEvent(eventRecord);

	return 0;	// end the timer
}

static void write_baseboxcmd(io_port_t, io_val_t command, io_width_t)
{
	// we receive command bytes on this io port, works like a
	// interrupt but keeps the overall processor state
	// 
	// parameters are fetched from register contents as needed
	// result are fed into registers as well, depending on the command
	// potentially referenced memory is read or written

	if (G_commandOffset < (sizeof(G_commandBuffer) / sizeof(uint16_t))) {
		G_commandBuffer[G_commandOffset] = (uint16_t)command;
		G_commandOffset++;

		if (G_commandOffset == (sizeof(G_commandBuffer) / sizeof(uint16_t))) {
			if (G_commandBuffer[0] == HIF_SET_VIDEO_PARAMS) {

				MOUSEDOS_BeforeNewVideoMode();
				uint16_t newWidth  = G_commandBuffer[1];
				uint16_t newHeight = G_commandBuffer[2];
				VESA_SetBaseboxMode(newWidth, newHeight);
				LOG_INFO("GEOSHOST:Set basebox video mode");
				MOUSEDOS_AfterNewVideoMode(false);
			} else if (G_commandBuffer[0] == HIF_CHECK_API) {

				/* full response buffer */
				G_responseBuffer[0] = HIF_OK;
				G_responseBuffer[1] = (((uint16_t)G_baseboxID[1])
				                       << 8) |
				                      G_baseboxID[0];
				G_responseBuffer[2] = (((uint16_t)G_baseboxID[3])
				                       << 8) |
				                      G_baseboxID[2];
				G_responseBuffer[3] = (((uint16_t)G_baseboxID[5])
				                       << 8) |
				                      G_baseboxID[4];
				G_responseBuffer[4] = (((uint16_t)G_baseboxID[7])
				                       << 8) |
				                      G_baseboxID[6];
				G_responseBuffer[5] = 1; /* minor (compatibility)
				                            version */
				G_responseOffset = 6;

				// for testing issue an async event trigger
				//SDL_AddTimer(30000, timer_callback, NULL);
			} else if (G_commandBuffer[0] == HIF_SET_EVENT_INTERRUPT) {
				G_eventInterrupt =
				        0xA0 /* G_commandBuffer[1] & 0xFF*/;
			} else if (G_commandBuffer[0] == HIF_GET_VIDEO_PARAMS) {

				float ddpi, hdpi, vdpi;

				int displayIndex = SDL_GetWindowDisplayIndex(sdl.window);

				int width, height;
				SDL_GL_GetDrawableSize(sdl.window, &width, &height);

				G_responseBuffer[0] = HIF_OK;
				//G_responseBuffer[1] = sdl.draw_rect_px.w; /* native width */
				//G_responseBuffer[2] = sdl.draw_rect_px.h; /* native height */
				G_responseBuffer[1] = width; /* native width */ 
				G_responseBuffer[2] = height; /* native height */

				// Get the DPI of the first display (index 0)
				if (SDL_GetDisplayDPI(displayIndex, &ddpi, &hdpi, &vdpi) ==
				    0) {
					G_responseBuffer[3] = hdpi; /* horizontal dpi */
					G_responseBuffer[4] = vdpi; /* vertical dpi */
				} else {
					/* error getting the dpi */
					G_responseBuffer[3] = 0; /* horizontal dpi */
					G_responseBuffer[4] = 0; /* vertical dpi */
				}
				G_responseBuffer[5] = 0; /* unused */
				G_responseOffset    = 6;
			
			} else if (G_commandBuffer[0] == HIF_GET_EVENT) {
			
				// Add Event to list
				SDL_mutexP(G_eventQueueMutex);
				if (G_eventRecords == NULL) {
					G_responseBuffer[0] = HIF_NOT_FOUND;

				} else {

					EventRecord* thisRecord = G_eventRecords;
					thisRecord->GetRecordData(G_responseBuffer);
					G_eventRecords = thisRecord->GetNext();
					delete thisRecord;
				}
				G_responseOffset = 6;
				SDL_mutexV(G_eventQueueMutex);
			}
		}
	}
}

void geoshost_init(Section* /*sec*/) {

	IO_RegisterReadHandler(0x38FF, read_baseboxid, io_width_t::word);
	IO_RegisterWriteHandler(0x38FF, write_baseboxcmd, io_width_t::word);

	G_eventQueueMutex = SDL_CreateMutex();

	TIMER_AddTickHandler(GeosHost_TickHandler);

	LOG_INFO("GEOSHOST:Initialized");
}

void GeosHost_AddConfigSection(const ConfigPtr& conf)
{
	assert(conf);

	Section_prop* sec = conf->AddSection_prop("geoshost",
	                                          &geoshost_init);
	assert(sec);
}

#endif // C_GEOSHOST



