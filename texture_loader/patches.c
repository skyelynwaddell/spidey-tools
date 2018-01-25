/*
 * Purpose: Contains all the patches
 * with each call to memory related functions
 * being documented to ease debugging
 *
 */

#include "patches.h"

#define Nop(add, size, reason) if(!NopMemory(add, size, reason)) return FALSE;
#define Set(add, size, buffer, reason) if(!SetMemory(add, size, buffer, reason)) return FALSE;
#define Hook(add, func, reason) if(!HookFunc(add, func, reason)) return FALSE;

static const unsigned char twoByteJmp = 0xEB;
BOOL FreadHook();
BOOL OpenFileFromDisk();
DWORD MyGetFileSize(FILE *fp);

BOOL DisableIntros(){

	Nop(0x004707BE, 0x004707C3 - 0x004707BE, "Disable wrapper for Bink functions.")
	return TRUE;
}

/************************************************
            
					FILE LOADER
            
 ************************************************/

typedef struct {
	char name[0x20];
	DWORD crc;
	DWORD compressed;
	DWORD fileOffset;
	DWORD uncompressedSize;
	DWORD compressedSize;
}PkrFile;

BOOL FileLoader(){

	Nop(0x00519376, 5,"Disable memory allocation")
	Set(0x0051938B, 1, &twoByteJmp, "Disable check after memory allocation")
	Hook(0x005193C5, (DWORD)FreadHook, "FreadWrapper patch")

	Nop(0x005193DC, 5, "Disable free after fread fail")
	Nop(0x0051941E, 5, "Disable CRC Check")
	Set(0x0051942D, 1, &twoByteJmp, "Disable jnz after CRC Check")

	return TRUE;
}

char *fileName;
char *fileDirectory;
unsigned char **buffer; //buffer where the file will be stored
PkrFile *curPkr = NULL;

//Typedefs
typedef void* (*newP)(DWORD size);
newP new = (void*)0x00529BA2;
typedef void* (*deleteP)(void *address);
deleteP delete = (void*)0x00529BB0;

//Recreate the original fread wrapper
typedef void* (*lockFileP)(FILE *fp);
lockFileP lockFile = (void*)0x0052BEBD;
typedef void* (*unlockFileP)(FILE *fp);
unlockFileP unlockFile = (void*)0x0052BF0F;
typedef void* (*freadP)(void *buffer, DWORD size, DWORD cnt, FILE *fp);
freadP freadSpidey = (void*)0x0052AA25;

BOOL OpenFileFromDisk(PVOID unused, DWORD compressedSize, DWORD unused1, FILE *pkr){

	static char path[64];
	strcpy(path, fileDirectory);
	strcat(path , fileName);
	FILE *fp = fopen(path, "rb");
	if(!fp){

		printf("Will open from the pkr: %s\n", fileName);
		*buffer = new(compressedSize);
		if(!(*buffer)){
			printf("Wasn't able to create a buffer for the file being read off the PKR\n");
			return FALSE;
		}

		lockFile(pkr);
		if(!freadSpidey(*buffer, compressedSize, 1, pkr)){
			printf("Couldn't read file off pkr: %s\n", fileName);
			delete(*buffer);
			return FALSE;
		}
		unlockFile(pkr);

		return TRUE;
	}

	DWORD fileSize = MyGetFileSize(fp);
	*buffer = new(fileSize);
	if(!(*buffer)){
		printf("Couldn't allocate space file: %s\n", fileName);
		fclose(fp);
		return FALSE;
	}

	if(!fread(*buffer, fileSize, 1, fp)){
		printf("Error reading file: %s\n", fileName);
		fclose(fp);
		delete(*buffer);
		return FALSE;
	}
	fclose(fp);

	printf("File has been loaded: %s\n", curPkr->name);
	curPkr->uncompressedSize = fileSize;
	curPkr->compressed = 0xFFFFFFFE;

	return TRUE;
}

DWORD MyGetFileSize(FILE *fp){

	fseek(fp, 0, SEEK_END);
	DWORD size = ftell(fp);
	rewind(fp);
	return size;
}
/************************************************
            
				TEXTURE LOADER
            
 ************************************************/

void* PVRIdHandler(DWORD id, DWORD width, DWORD height, DWORD type, DWORD texturebuffer, void *a6, void *a7, void *a8);
void* CreateTexturePVRInIdHook();
void* ConvertVQHook();

typedef void* (*createTexturePVRInIdP)(DWORD id, DWORD, DWORD , DWORD type, DWORD textureBUffer, void *a6, void *a7, void *a8);
createTexturePVRInIdP CreateTexturePVRInId = (void*)0x0050F6D0;

DWORD *EnviroList = (void*)0x6B2440;
DWORD psxId = 0;

typedef struct _List{
	DWORD key;
	DWORD value;
	struct _List *next;
}List;

List *texList = NULL;//list that contains the textures

BOOL AddToList(DWORD key, DWORD value){
	
	//Find an empty spot
	List **walker = &texList;	
	while(*walker){
		//the game frees old textures so need to this here
		if((*walker)->key == key){
			(*walker)->value = value;
			(*walker)->next = NULL;
			return TRUE;
		}
		walker = &((*walker)->next);
	}

	*walker = malloc(sizeof(List));
	if(!(*walker)){
		printf("There was a problem allocating a node for the tree");
		return FALSE;
	}
	
	//new entry
	(*walker)->key = key;
	(*walker)->value = value;
	(*walker)->next = NULL;
	return TRUE;
}

DWORD SearchKeyValue(DWORD key){

	List *walker = texList;
	while(walker){
		if(walker->key == key)
			return walker->value;

		walker = walker->next;
	}

	return 0;//Didnt find
}



BOOL TextureLoader(){

	Hook(0x0050F45F, (DWORD)CreateTexturePVRInIdHook, "Hooks a call to CreateTexturePVRInIdHook for the PSXPVR files")
	Hook(0x0050F951, (DWORD)ConvertVQHook, "Hooks ConvertVQToBmp")
	return TRUE;

}

void *PVRIdHandler(DWORD id, DWORD width, DWORD height, DWORD type, DWORD textureBuffer, void *a6, void *a7, void *a8){

	static char path[64];
	sprintf(path, "textures/%s/%08X.bmp\0", &EnviroList[0x11 * psxId], textureBuffer - EnviroList[0x11 * psxId + 6]);

	FILE *fp;
	fp = fopen(path, "rb");
	if(!fp)
		return CreateTexturePVRInId(id, width, height, type, textureBuffer, a6, a7, a8);

	//Guarantees there's no problems with the files
	DWORD textureSize = MyGetFileSize(fp);
	if(textureSize < width * height * 2 + 0xA){
		printf("There seems to be a problem with the texture %08X for %s\n", textureBuffer - EnviroList[0x11 * psxId + 6], &EnviroList[0x11 * psxId]);
		fclose(fp);
		return CreateTexturePVRInId(id, width, height, type, textureBuffer, a6, a7, a8);
	}

	//opens the bmp and reads the pixel info
	DWORD offset;
	fseek(fp, 0xA, SEEK_SET);
	if(!fread(&offset, 4, 1, fp)){
		fclose(fp);
		printf("Couldn't read offset %s\n", &EnviroList[0x11 * psxId]);
		return CreateTexturePVRInId(id, width, height, type, textureBuffer, a6, a7, a8);
	}

	fseek(fp, offset, SEEK_SET);
	void *texBuffer = new(width * height * 2);	
	if(!texBuffer){
		fclose(fp);
		printf("Couldn't allocate space for %s\n", &EnviroList[0x11 * psxId]);
		return CreateTexturePVRInId(id, width, height, type, textureBuffer, a6, a7, a8);
	}

	if(!fread(texBuffer, width * height * 2, 1, fp)){
		fclose(fp);
		delete(texBuffer);
		printf("Couldn't read offset %s\n", &EnviroList[0x11 * psxId]);
		return CreateTexturePVRInId(id, width, height, type, textureBuffer, a6, a7, a8);
	}

	fclose(fp);//Dont leak file pointers

	//ids get reused so cant use them as key, will use the texture buffer hoping no other uses the same
	if(!AddToList(textureBuffer ^ EnviroList[0x11 * psxId], (DWORD)texBuffer)){
		delete(texBuffer);
		return CreateTexturePVRInId(id, width, height, type, textureBuffer, a6, a7, a8);
	}

	return CreateTexturePVRInId(id, width, height, type, textureBuffer, a6, a7, a8);
}
