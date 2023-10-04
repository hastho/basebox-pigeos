#include "control.h"
#include "dosbox.h"
#include "cross.h"
#include "timer.h"
#include "setup.h"
#include "../dos/dev_lpt.h"

#define MAX_PRDEV	4

#define LPTXDBG

static Bit32u Timeout;
static device_LPT *devLPT[MAX_PRDEV];

static void Spooler (void) {
	int i;
	for (i=0; i < MAX_PRDEV; i++) 
		devLPT[i]->Flush(Timeout);
}

class PRINTERDEV:public Module_base {
public:
	PRINTERDEV (Section * configuration):Module_base (configuration) {
		char name[5], tmpdir[CROSS_LEN], wrk[CROSS_LEN];
		const char *cmd, *path;
		int i;

		for (i=0;i < MAX_PRDEV; i++)
			devLPT[i] = NULL;

		Section_prop * section=static_cast<Section_prop *>(configuration);
#ifdef LPTXDBG
		printf("-- Init printer\n");
#endif
		Timeout = section->Get_int("print_timeout");
#ifdef LPTXDBG
		printf("-- Timeout set to <%u>\n", Timeout);
#endif
		if (!Timeout)
			return;
		TIMER_AddTickHandler(&Spooler);
#ifdef LPTXDBG
		printf("-- Spooler started\n");
#endif		

		path = section->Get_string("tmpdir");
		if (!strlen(path))
			path = ".";
		strcpy (tmpdir, path);
#if defined (WIN32)
		if (tmpdir[strlen(tmpdir)-1] != '\\')
			strcat(tmpdir, "\\");
#else
		if (tmpdir[strlen(tmpdir)-1] != '/')
			strcat(tmpdir, "/");
#endif
		
#ifdef LPTXDBG
		printf("-- Working directory path <%s>\n", tmpdir);
#endif
		for (i=0;i < MAX_PRDEV; i++) {
			sprintf (name, "LPT%d", i+1);
			cmd = section->Get_string(name);
			if (!strcmp(cmd, "disabled"))
				cmd = NULL;
			sprintf(wrk, "%sdev%s.prn", tmpdir, name);
#ifdef LPTXDBG
			printf("-- Device file for <%s> is <%s> = <%s>\n", name, wrk, cmd);
#endif
			devLPT[i] = new device_LPT(name, wrk, cmd);
			DOS_AddDevice(devLPT[i]);		
		}
#ifdef LPTXDBG
		printf("-- Printer initialization ok\n");
#endif
	}

	~PRINTERDEV () {
		int i;
		char name[5];
		TIMER_DelTickHandler(&Spooler);
		for (i=0;i < MAX_PRDEV; i++) {
			if (devLPT[i])
				DOS_DelDevice(devLPT[i]);
		}
	}
};

static PRINTERDEV *PrinterBaseClass;

void PRINTER_Shutdown(Section* sec) {
	delete PrinterBaseClass;
	PrinterBaseClass = NULL;
}

void PRINTER_Init(Section* sec) {
	if (PrinterBaseClass) delete PrinterBaseClass;
	PrinterBaseClass = new PRINTERDEV (sec);
	sec->AddDestroyFunction(&PRINTER_Shutdown);
}
