/* see main.c for definition */

#ifdef __linux__
#include <sys/stat.h>
#endif

#ifdef __DJGPP__
#include <dirent.h>
#endif

/*
Disk errors
*/
#define errWRITEPROT 1
#define errSELECT 2

#define RW	(roVector & (1 << (_RamRead(0x0004) & 0x0f)))

static void _error(uint8 error)
{
	_puts("\r\nBdos Error on ");
	_putcon('A' + LOW_REGISTER(DE));
	_puts(" : ");
	switch (error) {
	case errWRITEPROT:
		_puts("R/O");
		break;
	case errSELECT:
		_puts("Select");
		break;
	default:
		_puts("\r\nCP/M ERR");
		break;
	}
	_getch();
	_puts("\r\n");
	Status = 2;
}

int _SelectDisk(uint8 dr)
{
	uint8 result;
	uint8 disk[2] = "A";

	if (dr) {
		disk[0] += (dr - 1);
	} else {
		disk[0] += (_RamRead(0x0004) & 0x0f);
	}

	result = _sys_select(disk);
	if (result)
		loginVector = loginVector | (1 << (disk[0] - 'A'));
	return(result);
}

long _FileSize(uint16 fcbaddr)
{
	CPM_FCB* F = (CPM_FCB*)&RAM[fcbaddr];
	long l = -1;

	if (_SelectDisk(F->dr)) {
		_GetFile(fcbaddr, &filename[0]);
		l = _sys_filesize(filename);
	}
	return(l);
}

uint8 _OpenFile(uint16 fcbaddr)
{
	CPM_FCB* F = (CPM_FCB*)&RAM[fcbaddr];
	uint8 result = 0xff;
	long l;
	int32 reqext;	// Required extention to open
	int32 b, i;

	if (_SelectDisk(F->dr)) {
		_GetFile(fcbaddr, &filename[0]);
		if(_sys_openfile(&filename[0])) {
			l = _FileSize(fcbaddr);

			reqext = (F->ex & 0x1f) | (F->s1 << 5);
			b = l - reqext * 16384;
			for (i = 0; i < 16; i++)
				F->al[i] = (b > i * 1024) ? i + 1 : 0;
			F->s1 = 0x00;
			F->rc = (uint8)(l/128);
			result = 0x00;
		}
	} else {
		_error(errSELECT);
	}
	return(result);
}

uint8 _CloseFile(uint16 fcbaddr)
{
	CPM_FCB* F = (CPM_FCB*)&RAM[fcbaddr];
	uint8 result = 0xff;

	if (_SelectDisk(F->dr)) {
		if (!RW) {
			_GetFile(fcbaddr, &filename[0]);
			if (fcbaddr == BatchFCB) {
				_Truncate((char *)filename, F->rc);	// Truncate $$$.SUB to F->rc CP/M records so SUBMIT.COM can work
			}
			result = 0x00;
		} else {
			_error(errWRITEPROT);
		}
	} else {
		_error(errSELECT);
	}
	return(result);
}

uint8 _MakeFile(uint16 fcbaddr)
{
	CPM_FCB* F = (CPM_FCB*)&RAM[fcbaddr];
	uint8 result = 0xff;

	if (_SelectDisk(F->dr)) {
		if (!RW) {
			_GetFile(fcbaddr, &filename[0]);
			if(_sys_makefile(&filename[0])) {
				result = 0x00;
			}
		} else {
			_error(errWRITEPROT);
		}
	} else {
		_error(errSELECT);
	}
	return(result);
}

uint8 _SearchFirst(uint16 fcbaddr, uint8 dir)
{
	CPM_FCB* F = (CPM_FCB*)&RAM[fcbaddr];
	uint8 result = 0xff;

	if (_SelectDisk(F->dr)) {
		_GetFile(fcbaddr, &filename[0]);
		result = _findfirst(dir);
	} else {
		_error(errSELECT);
	}
	return(result);
}

uint8 _SearchNext(uint16 fcbaddr, uint8 dir)
{
	CPM_FCB* F = (CPM_FCB*)&RAM[tmpFCB];
	uint8 result = 0xff;

	if (_SelectDisk(F->dr)) {
		result = _findnext(dir);
	} else {
		_error(errSELECT);
	}
	return(result);
}

uint8 _DeleteFile(uint16 fcbaddr)
{
	CPM_FCB* F = (CPM_FCB*)&RAM[fcbaddr];
	uint8 result = 0xff;
	uint8 deleted = 0xff;

	if (_SelectDisk(F->dr)) {
		if (!RW) {
			result = _SearchFirst(fcbaddr, FALSE);	// FALSE = Does not create a fake dir entry when finding the file
			while (result != 0xff)
			{
				_GetFile(tmpFCB, &filename[0]);
				if(_sys_deletefile(&filename[0]))
					deleted = 0x00;
				result = _SearchNext(fcbaddr, FALSE);	// FALSE = Does not create a fake dir entry when finding the file
			}
		} else {
			_error(errWRITEPROT);
		}
	} else {
		_error(errSELECT);
	}
	return(deleted);
}

uint8 _RenameFile(uint16 fcbaddr)
{
	CPM_FCB* F = (CPM_FCB*)&RAM[fcbaddr];
	uint8 result = 0xff;

	if (_SelectDisk(F->dr)) {
		if (!RW) {
			_RamWrite(fcbaddr + 16, _RamRead(fcbaddr));	// Prevents rename from moving files among folders
			_GetFile(fcbaddr + 16, &newname[0]);
			_GetFile(fcbaddr, &filename[0]);
			if(_sys_renamefile(&filename[0], &newname[0]))
				result = 0x00;
		} else {
			_error(errWRITEPROT);
		}
	} else {
		_error(errSELECT);
	}
	return(result);
}

uint8 _ReadSeq(uint16 fcbaddr)
{
	CPM_FCB* F = (CPM_FCB*)&RAM[fcbaddr];
	uint8 result = 0xff;
	long fpos = (F->ex * 16384) + (F->cr * 128);

	if (_SelectDisk(F->dr)) {
		_GetFile(fcbaddr, &filename[0]);
		result = _sys_readseq(&filename[0], fpos);
		if (!result) {	// Read succeeded, adjust FCB
			F->cr++;
			if (F->cr > 127) {
				F->cr = 0;
				F->ex++;
			}
			if (F->ex > 127) {
				// (todo) not sure what to do 
				result = 0xff;
			}
		}
	} else {
		_error(errSELECT);
	}
	return(result);
}

uint8 _WriteSeq(uint16 fcbaddr)
{
	CPM_FCB* F = (CPM_FCB*)&RAM[fcbaddr];
	uint8 result = 0xff;
#ifdef ARDUINO
	SdFile sd;
	int32 file;
#else
	FILE* file;
#endif

	long fpos = (F->ex * 16384) + (F->cr * 128);

	if (_SelectDisk(F->dr)) {
		if (!RW) {
			_GetFile(fcbaddr, &filename[0]);
#ifdef ARDUINO
			file = sd.open((char*)filename, O_RDWR);
#else
			file = _sys_fopen_rw(&filename[0]);
#endif
			if (file != NULL) {
#ifdef ARDUINO
				if (sd.seekSet(fpos)) {
					if (sd.write(&RAM[dmaAddr], 128)) {
#else
				if (!_sys_fseek(file, fpos, 0)) {
					if (_sys_fwrite(&RAM[dmaAddr], 1, 128, file)) {
#endif
						F->cr++;
						if (F->cr > 127) {
							F->cr = 0;
							F->ex++;
						}
						if (F->ex > 127) {
							// (todo) not sure what to do 
						}
						result = 0x00;
					}
				} else {
					result = 0x01;
				}
#ifdef ARDUINO
				sd.close();
#else
				_sys_fclose(file);
#endif
			} else {
				result = 0x10;
			}
		} else {
			_error(errWRITEPROT);
		}
	} else {
		_error(errSELECT);
	}
	return(result);
}

uint8 _ReadRand(uint16 fcbaddr)
{
	CPM_FCB* F = (CPM_FCB*)&RAM[fcbaddr];
	uint8 result = 0xff;
#ifdef ARDUINO
	SdFile sd;
	int32 file;
#else
	FILE* file;
#endif
	uint8 bytesread;
	int32 record = F->r0 | (F->r1 << 8);
	long fpos = record * 128;

	if (_SelectDisk(F->dr)) {
		_GetFile(fcbaddr, &filename[0]);
#ifdef ARDUINO
		file = sd.open((char*)filename, O_READ);
#else
		file = _sys_fopen_r(&filename[0]);
#endif
		if (file != NULL) {
#ifdef ARDUINO
			if (sd.seekSet(fpos)) {
#else
			if (!_sys_fseek(file, fpos, 0)) {
#endif
				_RamFill(dmaAddr, 128, 0x1a);	// Fills the buffer with ^Z prior to reading
#ifdef ARDUINO
				bytesread = sd.read(&RAM[dmaAddr], 128);
#else
				bytesread = (uint8)_sys_fread(&RAM[dmaAddr], 1, 128, file);
#endif
				if (bytesread) {
					F->cr = record & 0x7F;
					F->ex = (record >> 7) & 0x1f;
					F->s1 = (record >> 12) & 0xff;
					F->s2 = (record >> 20) & 0xff;
					result = 0x00;
				} else {
					result = 0x01;
				}
			} else {
				result = 0x06;
			}
#ifdef ARDUINO
			sd.close();
#else
			_sys_fclose(file);
#endif
		} else {
			result = 0x10;
		}
	} else {
		_error(errSELECT);
	}
	return(result);
}

uint8 _WriteRand(uint16 fcbaddr)
{
	CPM_FCB* F = (CPM_FCB*)&RAM[fcbaddr];
	uint8 result = 0xff;
#ifdef ARDUINO
	SdFile sd;
	int32 file;
#else
	FILE* file;
#endif
	int32 record = F->r0 | (F->r1 << 8);
	long fpos = record * 128;

	if (_SelectDisk(F->dr)) {
		if (!RW) {
			_GetFile(fcbaddr, &filename[0]);
#ifdef ARDUINO
			file = sd.open((char*)filename, O_RDWR);
#else
			file = _sys_fopen_rw(&filename[0]);
#endif
			if (file != NULL) {
#ifdef ARDUINO
				if (sd.seekSet(fpos)) {
					if (sd.write(&RAM[dmaAddr], 128)) {
#else
				if (!_sys_fseek(file, fpos, 0)) {
					if (_sys_fwrite(&RAM[dmaAddr], 1, 128, file)) {
#endif
						F->cr = record & 0x7F;
						F->ex = (record >> 7) & 0x1f;
						F->s1 = (record >> 12) & 0xff;
						F->s2 = (record >> 20) & 0xff;
						result = 0x00;
					}
				} else {
					result = 0x06;
				}
#ifdef ARDUINO
				sd.close();
#else
				_sys_fclose(file);
#endif
			} else {
				result = 0x10;
			}
		} else {
			_error(errWRITEPROT);
		}
	} else {
		_error(errSELECT);
	}
	return(result);
}

uint8 _CheckSUB(void)
{
	return((_SearchFirst(BatchFCB, FALSE) == 0x00) ? 0xFF : 0x00);
}

