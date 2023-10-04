#include <time.h>
#include "setup.h"
#include "dos_inc.h"
#include "timer.h"
#include "setup.h"

class device_LPT : public DOS_Device {
public:
	device_LPT(char *name, char *fname, const char *ncmd) { 
		SetName(name); 
		strcpy (Filename, fname);
		Cmd = ncmd;
		Access = 0;
		Handle = NULL;
	};
	bool Read(uint8_t * data, uint16_t * size) { 
		return true; 
	};
	bool Write(uint8_t * data, uint16_t * size) {
		if (!Handle) {
			if (!(Handle = fopen(Filename, "a")))
				return false;
		}
		return fwrite (data, 1, *size, Handle) == *size;
	};
	bool Seek(uint32_t *pos, uint32_t type) { 
		return true; 
	};
	bool Close() { 
		if (Handle) {
			fclose (Handle);
			Handle = NULL;
			Access = GetTicks();
		}
		return true; 
	};
	void Flush (uint32_t timeout) {
		char wrk[CROSS_LEN];
		uint32_t ticks = GetTicks();
		if (!Access || Handle || ticks - Access < timeout)
			return;
		Access = 0;
		if (!Cmd) {
			LOG_MSG("Output to %s discarded due to configuration settings", GetName());
		} else {
			sprintf(wrk, Cmd, Filename);
			if (system(wrk) == -1) {
				LOG_MSG("%s: %s", GetName(), wrk);
			}
		}
		unlink (Filename);
	};
	uint16_t GetInformation(void) { 
		return 0x8000; 
	};
	~device_LPT () { 
		Flush(0); 
	};	

private:
	char		Filename[CROSS_LEN];	// file name 
	const char	*Cmd;					// command string to pass to host printing system
	uint32_t	Access = 0;				// last access time
	FILE		*Handle = NULL;			// persistent file handle
};
