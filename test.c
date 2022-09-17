//Copyright (c) 2011-2020 <>< Charles Lohr - Under the MIT/x11 or NewBSD License you choose.
// NO WARRANTY! NO GUARANTEE OF SUPPORT! USE AT YOUR OWN RISK

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "os_generic.h"
#include <GLES3/gl3.h>
#include <asset_manager.h>
#include <asset_manager_jni.h>
#include <android_native_app_glue.h>
#include <android/sensor.h>
#include "CNFGAndroid.h"
#include <android/log.h>

#define CNFG_IMPLEMENTATION
#define CNFG3D

#include "CNFG.h"

//lib bass
#include <dlfcn.h>
#define BASSDEF(f) (*f)	// define the BASS functions as pointers
#include "bass.h"
void *g_libBASS = NULL;

#define Logs(...) ((void)__android_log_print(ANDROID_LOG_INFO, "smallapp", __VA_ARGS__))

uint32_t GetTickCountGame()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec*1000+tv.tv_usec/1000);
}

//read image
#include <stdint.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

typedef struct {
	int w;
	int h;
	int c;
	uint32_t* rdimg;
	unsigned int tex;
} image;

void rdimg(image* img, unsigned char* data) {
	uint32_t* rd;
	rd = malloc(sizeof(int) * img->w * img->h);
	uint32_t current = 0;

	for (int y = 0; y < img->h; y += 1) {
		for (int x = 0; x < img->w; x += 1) {
			current = 0;
			for (int i = 0; i < img->c; i++) {
				current = current << 8;
				current += (uint32_t)data[(y * img->w + x) * img->c + i];
			}
			for (int i = 0; i < 4 - img->c; i++) {
				if (current == 1) {
					current = 0x00 | current << 8;
					continue;
				}
				current = 0xff | current << 8;
			}
			rd[(y * img->w + x)] = current;
		}
	}
	img->rdimg = rd;
}

image* loadimagefromapk(char* name)
{
	Logs("> loadimagefromapk -> %s", name);
	int w = 0, h = 0, c = 0;
	unsigned char* pixels = 0;
	
	AAsset* file = AAssetManager_open(gapp->activity->assetManager, name, AASSET_MODE_BUFFER);
	if (file)
	{
		uint32_t size = AAsset_getLength(file);
		unsigned char* data = ( unsigned char * )malloc( size * sizeof( unsigned char ) );
		AAsset_read(file, data, size);
		AAsset_close(file);
		pixels = stbi_load_from_memory(data, size, &w, &h, &c, STBI_rgb_alpha);
	}
	else
	{
		Logs("loadimagefromapk -> %s not found!", name);
	}
	
	image* img;
	img = malloc(sizeof(image));
	img->w = w;
	img->h = h;
	img->c = c;

	if (pixels == NULL)
	{
		Logs("pixels is null");
		img->rdimg = NULL;
		return img;
	}

	// Faster way, but it doesn't seem to work. TODO FIXME
	/*if (c == 4) {
		img->rdimg = (unsigned int *)data;
		return img;
	}*/

	rdimg(img, pixels);
	stbi_image_free(pixels);

	return img;	
}

//test load image from path
image* loadimage(char* path)
{
	int w, h, c;

	unsigned char* data = stbi_load(path, &w, &h, &c, 0);

	image* img;
	img = malloc(sizeof(image));
	img->w = w;
	img->h = h;
	img->c = c;

	if (data == NULL)
	{
		//char buff[256];
		//sprintf(buff, "could not find image at path %s", path);
		img->rdimg = NULL;
		return img;
	}

	// Faster way, but it doesn't seem to work. TODO FIXME
	/*if (c == 4) {
		img->rdimg = (unsigned int *)data;
		return img;
	}*/

	rdimg(img, data);
	stbi_image_free(data);

	return img;
}

void RenderImage(unsigned int tex, int x, int y, int w, int h) {
	if (w == 0 || h == 0) return;

	CNFGFlushRender();

	glUseProgram(gRDBlitProg);
	glUniform4f(gRDBlitProgUX, 1.f / gRDLastResizeW, -1.f / gRDLastResizeH, -0.5f + x / (float)gRDLastResizeW, 0.5f - y / (float)gRDLastResizeH);
	glUniform1i(gRDBlitProgUT, 0);

	glBindTexture(GL_TEXTURE_2D, tex);

	float zrotx = 0;
	float zroty = 0;
	float brotx = w;
	float broty = h;
	float wrotx = w;
	float wroty = 0;
	float hrotx = 0;
	float hroty = h;

	const float verts[] = {
		zrotx, zroty, wrotx, wroty, brotx, broty,
		zrotx, zroty, brotx, broty, hrotx, hroty };
	static const uint8_t colors[] = {
		0,0,   255,0,  255,255,
		0,0,  255,255, 0,255 };

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(1, 2, GL_UNSIGNED_BYTE, GL_TRUE, 0, colors);

	glDrawArrays(GL_TRIANGLES, 0, 6);
}

float mountainangle;
float mountainoffsetx;
float mountainoffsety;

ASensorManager * sm;
const ASensor * as;
bool no_sensor_for_gyro = false;
ASensorEventQueue* aeq;
ALooper * l;

float accx, accy, accz;
int accs;

void AccCheck()
{
	if(no_sensor_for_gyro) {
		return;
	}

	ASensorEvent evt;
	do
	{
		ssize_t s = ASensorEventQueue_getEvents( aeq, &evt, 1 );
		if( s <= 0 ) break;
		accx = evt.vector.v[0];
		accy = evt.vector.v[1];
		accz = evt.vector.v[2];
		mountainangle /*degrees*/ -= accz;// * 3.1415 / 360.0;// / 100.0;
		mountainoffsety += accy;
		mountainoffsetx += accx;
		accs++;
	} while( 1 );
}

unsigned frames = 0;

void AndroidDisplayKeyboard(int pShow);

int lastbuttonx = 0;
int lastbuttony = 0;
int lastmotionx = 0;
int lastmotiony = 0;
int lastbid = 0;
int lastmask = 0;
int lastkey, lastkeydown;
int lastbDown = 0;

//static int keyboard_up;

void HandleKey( int keycode, int bDown )
{
	lastkey = keycode;
	lastkeydown = bDown;
	//if( keycode == 10 && !bDown ) { keyboard_up = 0; AndroidDisplayKeyboard( keyboard_up );  }
	//Logs("hkey %d %d", keycode, bDown);
	//if( keycode == 4 ) { AndroidSendToBack( 1 ); } //Handle Physical Back Button.
}

void HandleButton( int x, int y, int button, int bDown )
{
	lastbid = button;
	lastbuttonx = x;
	lastbuttony = y;
	lastbDown = bDown;

	//if( bDown ) { keyboard_up = !keyboard_up; AndroidDisplayKeyboard( keyboard_up ); }
	//Logs("handlebutton %d %d %d %d", x,y,button,bDown);
}

void HandleMotion( int x, int y, int mask )
{
	lastmask = mask;
	lastmotionx = x;
	lastmotiony = y;
	//Logs("HandleMotion %d %d %d", x,y,mask);
}

short DisplaySizeX, DisplaySizeY;

extern struct android_app * gapp;

void HandleDestroy()
{
	printf( "Destroying\n" );
	exit(10);
}

volatile int suspended;

void HandleSuspend()
{
	suspended = 1;
}

//image* imgbackground;
image* backgroud1;
image* backgroud2;
image* backgroud3;
image* backgroud4;
image* backgroud5;
image* backgroud6;
image* backgroud7;
image* column1;
image* column2;
image* bird;
image* browser;
image* vk;
image* github;
image* youtube;
image* tiktok;

void initialize_data()
{
	Logs("load 1 png");
	backgroud1 = loadimagefromapk("backgroud-1.png");
	backgroud1->tex = CNFGTexImage(backgroud1->rdimg, backgroud1->w, backgroud1->h);
	Logs("load 2 png");
	backgroud2 = loadimagefromapk("backgroud-2.png");
	backgroud2->tex = CNFGTexImage(backgroud2->rdimg, backgroud2->w, backgroud2->h);
	Logs("load 3 png");
	backgroud3 = loadimagefromapk("backgroud-3.png");
	backgroud3->tex = CNFGTexImage(backgroud3->rdimg, backgroud3->w, backgroud3->h);
	Logs("load 4 png");
	backgroud4 = loadimagefromapk("backgroud-4.png");
	backgroud4->tex = CNFGTexImage(backgroud4->rdimg, backgroud4->w, backgroud4->h);	
	Logs("load 5 png");
	backgroud5 = loadimagefromapk("backgroud-5.png");
	backgroud5->tex = CNFGTexImage(backgroud5->rdimg, backgroud5->w, backgroud5->h);
	Logs("load 6 png");
	backgroud6 = loadimagefromapk("backgroud-6.png");
	backgroud6->tex = CNFGTexImage(backgroud6->rdimg, backgroud6->w, backgroud6->h);
	Logs("load 7 png");
	backgroud7 = loadimagefromapk("backgroud-7.png");
	backgroud7->tex = CNFGTexImage(backgroud7->rdimg, backgroud7->w, backgroud7->h);
	Logs("load c1 png");
	column1 = loadimagefromapk("column-1.png");
	column1->tex = CNFGTexImage(column1->rdimg, column1->w, column1->h);
	Logs("load c2 png");
	column2 = loadimagefromapk("column-2.png");
	column2->tex = CNFGTexImage(column2->rdimg, column2->w, column2->h);
	
	bird = loadimagefromapk("bird.png");
	bird->tex = CNFGTexImage(bird->rdimg, bird->w, bird->h);
	
	browser = loadimagefromapk("browser.png");
	browser->tex = CNFGTexImage(browser->rdimg, browser->w, browser->h);
	vk = loadimagefromapk("vk.png");
	vk->tex = CNFGTexImage(vk->rdimg, vk->w, vk->h);
	github = loadimagefromapk("github.png");
	github->tex = CNFGTexImage(github->rdimg, github->w, github->h);
	youtube = loadimagefromapk("youtube.png");
	youtube->tex = CNFGTexImage(youtube->rdimg, youtube->w, youtube->h);
	tiktok = loadimagefromapk("tiktok.png");
	tiktok->tex = CNFGTexImage(tiktok->rdimg, tiktok->w, tiktok->h);
	
	Logs("all pics loaded!");
}


void HandleResume()
{
	suspended = 0;
	
	initialize_data();
}

int GetTextSizeX(char* text, int textsize)
{
	int* w = 0;
	int* h = 0;
	CNFGGetTextExtents(text, &w, &h, textsize);
	return w;
} 

int GetTextSizeY(char* text, int textsize)
{ 
	int* w = 0;
	int* h = 0;
	CNFGGetTextExtents(text, &w, &h, textsize);	
	return h;
} 

bool BirdJump(int x1, int y1, int x2, int y2)
{
	if(lastbDown == true)
	{
		if(lastbuttonx >= x1 && lastbuttonx <= x2)
		{
			if(lastbuttony >= y1 && lastbuttony <= y2)
			{
				HandleButton(lastbuttonx,lastbuttony,0,0);
				return true;
			}
			else return false;
		}
		else return false;
	}
	else return false;
}

bool DrawButtonText(uint32_t RGBA, short x1, short y1, short x2, short y2, uint32_t RGBA_text, char* text)
{
	CNFGColor( RGBA ); 
	CNFGTackRectangle( x1, y1, x2, y2 ); 
	
	if(strlen(text) != 0)
	{
		//outline 
		CNFGColor( 0x4CC4DEFF ); 
		CNFGPenX = (((x2 - x1)/2) + x1) - (GetTextSizeX(text,(x2 - x1) / 36) / 2) + 2; 
		CNFGPenY = (((y2 - y1)/2) + y1) - (GetTextSizeY(text,(x2 - x1) / 36) / 2) + 2;
		
		CNFGSetLineWidth( (x2 - x1) / 67.5 );
		CNFGDrawText( text, (x2 - x1) / 36 );
		//
		
		CNFGColor( RGBA_text ); 
		CNFGPenX = (((x2 - x1)/2) + x1) - (GetTextSizeX(text,(x2 - x1) / 36) / 2); 
		CNFGPenY = (((y2 - y1)/2) + y1) - (GetTextSizeY(text,(x2 - x1) / 36) / 2);
		
		CNFGSetLineWidth( (x2 - x1) / 67.5 );
		CNFGDrawText( text, (x2 - x1) / 36 );
	}
	
	if(lastbDown == true)
	{
		if(lastbuttonx >= x1 && lastbuttonx <= x2)
		{
			if(lastbuttony >= y1 && lastbuttony <= y2)
			{
				HandleButton(lastbuttonx,lastbuttony,0,0);
				return true;
			}
			else return false;
		}
		else return false;
	}
	else return false;
}

bool DrawButtonTextCustom(uint32_t RGBA, short x1, short y1, short x2, short y2, uint32_t RGBA_text, char* text, uint32_t RGBA_outline)
{
	CNFGColor( RGBA ); 
	CNFGTackRectangle( x1, y1, x2, y2 ); 
	
	//outline 
	CNFGColor( RGBA_outline ); 
	CNFGPenX = (((x2 - x1)/2) + x1) - (GetTextSizeX(text,(x2 - x1) / 36) / 2) + 2; 
	CNFGPenY = (((y2 - y1)/2) + y1) - (GetTextSizeY(text,(x2 - x1) / 36) / 2) + 2;
	
	CNFGSetLineWidth( (x2 - x1) / 67.5 );
	CNFGDrawText( text, (x2 - x1) / 36 );
	//
	
	CNFGColor( RGBA_text ); 
	CNFGPenX = (((x2 - x1)/2) + x1) - (GetTextSizeX(text,(x2 - x1) / 36) / 2); 
	CNFGPenY = (((y2 - y1)/2) + y1) - (GetTextSizeY(text,(x2 - x1) / 36) / 2);
	
	CNFGSetLineWidth( (x2 - x1) / 67.5 );
	CNFGDrawText( text, (x2 - x1) / 36 );
	
	if(lastbDown == true)
	{
		if(lastbuttonx >= x1 && lastbuttonx <= x2)
		{
			if(lastbuttony >= y1 && lastbuttony <= y2)
			{
				HandleButton(lastbuttonx,lastbuttony,0,0);
				return true;
			}
			else return false;
		}
		else return false;
	}
	else return false;
}

void DrawOutlineButton(uint32_t RGBA, short x1, short y1, short x2, short y2, int size)
{
	CNFGColor(RGBA);
	CNFGTackRectangle(x1 + size/2,y1 + size/2,x2 + size,y2 + size);
}

bool DrawButton(uint32_t RGBA, short x1, short y1, short x2, short y2)
{
	CNFGColor( RGBA ); 
	CNFGTackRectangle( x1, y1, x2, y2 ); 
	if(lastbDown == true)
	{
		if(lastbuttonx >= x1 && lastbuttonx <= x2)
		{
			if(lastbuttony >= y1 && lastbuttony <= y2)
			{
				HandleButton(lastbuttonx,lastbuttony,0,0);
				return true;
			}
			else return false;
		}
		else return false;
	}
	else return false;
}

void DrawRectangle(uint32_t RGBA, short x1, short y1, short x2, short y2)
{
	CNFGColor( RGBA ); 
	CNFGTackRectangle( x1, y1, x2, y2 ); 
}

void RenderText(uint32_t RGBA, char* text, short x, short y, int bold, int size, bool outline)
{
	if(outline)
	{
		CNFGColor(0x000000ff); 
		CNFGPenX = x + 1; 
		CNFGPenY = y + 2;	
		if(bold > 0)
		{
			CNFGSetLineWidth(bold);
		}
		CNFGDrawText( text, size);		
	}	
	
	CNFGColor( RGBA ); 
	CNFGPenX = x; 
	CNFGPenY = y;	
	if(bold > 0)
	{
		CNFGSetLineWidth(bold);
	}
	CNFGDrawText( text, size);
}

HSTREAM curr_sample;

void SoundPlay(const char* filename)
{
	AAsset* asset = AAssetManager_open(gapp->activity->assetManager,filename,AASSET_MODE_BUFFER);
	const void* data = AAsset_getBuffer(asset);
	size_t size = AAsset_getLength(asset);
 	unsigned char* m_EncodedBuffer = (unsigned char *)malloc(size);
    memcpy(m_EncodedBuffer,data,size);
    size_t m_EncodedBufferSize = size ;
    AAsset_close(asset);	

	BASS_SetConfig(BASS_CONFIG_GVOL_STREAM, 10000);
	BASS_SetConfig(BASS_CONFIG_NET_PLAYLIST, 1);
	BASS_SetConfig(BASS_CONFIG_NET_TIMEOUT, 10000); //ms

	HSTREAM hSample = BASS_StreamCreateFile(TRUE, m_EncodedBuffer, 0, m_EncodedBufferSize, BASS_SAMPLE_LOOP);

	curr_sample = hSample;

	BASS_ChannelPlay(hSample, true);
}

void StopSound()
{
	BASS_StreamFree(curr_sample);
}

void OpenUrl(const char* link)
{
	const struct JNINativeInterface * env = 0;
	const struct JNINativeInterface ** envptr = &env;
	const struct JNIInvokeInterface ** jniiptr = gapp->activity->vm;
	const struct JNIInvokeInterface * jnii = *jniiptr;
	
	jobject activity = gapp->activity->clazz;

	jnii->AttachCurrentThread( jniiptr, &envptr, NULL);
	env = (*envptr);

    // Retrieve class information
    jclass activityClass = env->FindClass(envptr,"android/app/Activity");
    jclass intentClass = env->FindClass(envptr,"android/content/Intent");
    jclass uriClass = env->FindClass(envptr,"android/net/Uri");

    // convert URL std::string to jstring
    jstring uriString = env->NewStringUTF(envptr,link);

    // call parse method
    jmethodID uriParse = env->GetStaticMethodID(envptr,uriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");

    // set URL in method
    jobject uri = env->CallStaticObjectMethod(envptr,uriClass, uriParse, uriString);

    // intent action
    jstring actionString = env->NewStringUTF(envptr,"android.intent.action.VIEW");

    // call the intent object constructor
    jmethodID newIntent = env->GetMethodID(envptr,intentClass, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V");

    // create the intent instance
    jobject intent = env->AllocObject(envptr,intentClass);

    // set intent constructor
    env->CallVoidMethod(envptr,intent, newIntent, actionString, uri);

    jmethodID startActivity = env->GetMethodID(envptr,activityClass, "startActivity", "(Landroid/content/Intent;)V");
    env->CallVoidMethod(envptr,activity, startActivity, intent);

    jnii->DetachCurrentThread( jniiptr );	
}

#define MENU 0
#define GAME 1
#define SHOP 2
#define INFO 3
#define GAME_OVER 4

int gamestate = MENU;

typedef struct Bird_ {
	int size;
	int posX;
	int posY;
	uint32_t UpdateTimer;
	int next_bird_step;
} Bird;

typedef struct Column_ {
	int posX;
	int posY;
	int width;
	int height;
	int offset;
} Column;

bool initbird = false;

int main()
{
	double ThisTime;
	double LastFPSTime = OGGetAbsoluteTime();
	int linesegs = 0;

	CNFGBGColor = 0x004040ff;
	CNFGSetupFullscreen( "CyberBird 2022", 0 );
	
	initialize_data();
	
	uint32_t update_background_button = 0;
	int step_update = 0;
	
	uint32_t back_houses_move = GetTickCountGame();
	float next_pos_back_houses = 0;

	uint32_t front_houses_move = GetTickCountGame();
	float next_pos_front_houses = 0;

	int StepUpdateBirdSync = 0;
	bool StepUpdateBirdTask = false;
	int BirdGravity = 0;

	uint32_t update_pos_columns = GetTickCountGame();
	int step_update_columns = 0;

	g_libBASS = dlopen("libbass.so", RTLD_NOW | RTLD_GLOBAL);
	if(g_libBASS == NULL) 
	{
		Logs("[libBASS] Error loading libbass.so");
		Logs("Error: %s\n", dlerror());
		return -1;
	} 
	else 
	{
		Logs("[libBASS] libbass.so loaded...");
	}	
	
	#define LOADBASSFUNCTION(f) *((void**)&f)=dlsym(g_libBASS, #f)
	LOADBASSFUNCTION(BASS_SetConfig);
	LOADBASSFUNCTION(BASS_GetConfig);
	LOADBASSFUNCTION(BASS_GetVersion);
	LOADBASSFUNCTION(BASS_ErrorGetCode);
	LOADBASSFUNCTION(BASS_Init);
	LOADBASSFUNCTION(BASS_SetVolume);
	LOADBASSFUNCTION(BASS_GetVolume);	
	LOADBASSFUNCTION(BASS_SetDevice);
	LOADBASSFUNCTION(BASS_GetDevice);
	LOADBASSFUNCTION(BASS_GetDeviceInfo);
	LOADBASSFUNCTION(BASS_Free);
	LOADBASSFUNCTION(BASS_GetInfo);
	LOADBASSFUNCTION(BASS_Update);
	LOADBASSFUNCTION(BASS_GetCPU);
	LOADBASSFUNCTION(BASS_Start);
	LOADBASSFUNCTION(BASS_Stop);
	LOADBASSFUNCTION(BASS_Pause);
	LOADBASSFUNCTION(BASS_PluginLoad);
	LOADBASSFUNCTION(BASS_PluginFree);
	LOADBASSFUNCTION(BASS_PluginGetInfo);
	LOADBASSFUNCTION(BASS_Set3DFactors);
	LOADBASSFUNCTION(BASS_Get3DFactors);
	LOADBASSFUNCTION(BASS_Set3DPosition);
	LOADBASSFUNCTION(BASS_Get3DPosition);
	LOADBASSFUNCTION(BASS_Apply3D);
	LOADBASSFUNCTION(BASS_MusicLoad);
	LOADBASSFUNCTION(BASS_MusicFree);
	LOADBASSFUNCTION(BASS_SampleLoad);
	LOADBASSFUNCTION(BASS_SampleCreate);
	LOADBASSFUNCTION(BASS_SampleFree);
	LOADBASSFUNCTION(BASS_SampleGetInfo);
	LOADBASSFUNCTION(BASS_SampleSetInfo);
	LOADBASSFUNCTION(BASS_SampleGetChannel);
	LOADBASSFUNCTION(BASS_SampleStop);
	LOADBASSFUNCTION(BASS_StreamCreate);
	LOADBASSFUNCTION(BASS_StreamCreateFile);
	LOADBASSFUNCTION(BASS_StreamCreateURL);
	LOADBASSFUNCTION(BASS_StreamCreateFileUser);
	LOADBASSFUNCTION(BASS_StreamFree);
	LOADBASSFUNCTION(BASS_StreamGetFilePosition);
	LOADBASSFUNCTION(BASS_RecordInit);
	LOADBASSFUNCTION(BASS_RecordSetDevice);
	LOADBASSFUNCTION(BASS_RecordFree);
	LOADBASSFUNCTION(BASS_RecordGetInfo);
	LOADBASSFUNCTION(BASS_RecordGetInputName);
	LOADBASSFUNCTION(BASS_RecordSetInput);
	LOADBASSFUNCTION(BASS_RecordGetInput);
	LOADBASSFUNCTION(BASS_RecordStart);
	LOADBASSFUNCTION(BASS_ChannelBytes2Seconds);
	LOADBASSFUNCTION(BASS_ChannelSeconds2Bytes);
	LOADBASSFUNCTION(BASS_ChannelGetDevice);
	LOADBASSFUNCTION(BASS_ChannelSetDevice);
	LOADBASSFUNCTION(BASS_ChannelIsActive);
	LOADBASSFUNCTION(BASS_ChannelGetInfo);
	LOADBASSFUNCTION(BASS_ChannelGetTags);
	LOADBASSFUNCTION(BASS_ChannelPlay);
	LOADBASSFUNCTION(BASS_ChannelStop);
	LOADBASSFUNCTION(BASS_ChannelPause);
	LOADBASSFUNCTION(BASS_ChannelIsSliding);
	LOADBASSFUNCTION(BASS_ChannelSet3DAttributes);
	LOADBASSFUNCTION(BASS_ChannelGet3DAttributes);
	LOADBASSFUNCTION(BASS_ChannelSet3DPosition);
	LOADBASSFUNCTION(BASS_ChannelGet3DPosition);
	LOADBASSFUNCTION(BASS_ChannelGetLength);
	LOADBASSFUNCTION(BASS_ChannelSetPosition);
	LOADBASSFUNCTION(BASS_ChannelGetPosition);
	LOADBASSFUNCTION(BASS_ChannelGetLevel);
	LOADBASSFUNCTION(BASS_ChannelGetData);
	LOADBASSFUNCTION(BASS_ChannelSetSync);
	LOADBASSFUNCTION(BASS_ChannelRemoveSync);
	LOADBASSFUNCTION(BASS_ChannelSetDSP);
	LOADBASSFUNCTION(BASS_ChannelRemoveDSP);
	LOADBASSFUNCTION(BASS_ChannelSetLink);
	LOADBASSFUNCTION(BASS_ChannelRemoveLink);
	LOADBASSFUNCTION(BASS_ChannelSetFX);
	LOADBASSFUNCTION(BASS_ChannelRemoveFX);
	LOADBASSFUNCTION(BASS_FXSetParameters);
	LOADBASSFUNCTION(BASS_FXGetParameters);	
	
	Logs("[libBASS] loading settings..");	
	
	if (BASS_Init(-1, 44100, 0, 0, NULL) != true) 
	{
		Logs("[libBASS] libbass.so is not initialized, error: %i", BASS_ErrorGetCode());
	}
	else
	{
		Logs("[libBASS] libbass.so successfully initialized");
	}	

	SoundPlay("cyberbird_theme1.mp3");

	while(1)
	{
		CNFGHandleInput();
		AccCheck();

		if( suspended ) { usleep(50000); continue; }

		CNFGClearFrame();
		CNFGColor( 0xFFFFFFFF );
		CNFGGetDimensions( &DisplaySizeX, &DisplaySizeY );

		Bird Cyber;

		if(!initbird)
		{
			//init Bird
			Cyber.size = DisplaySizeX / 7.2; //100px
			Cyber.posX = DisplaySizeX / 7.2; //100px
			Cyber.posY = (DisplaySizeY / 2) - (Cyber.size / 2); //center display minus half bird
			Cyber.UpdateTimer = 0;
			Cyber.next_bird_step = 0;
			initbird = true;
		}

		CNFGFlushRender();
		
		//render background
		RenderImage(backgroud1->tex, 0, 0, DisplaySizeX, DisplaySizeY);
		//render stars
		RenderImage(backgroud2->tex, 0, 0, DisplaySizeX, DisplaySizeY); 
		
		if(gamestate != GAME)
		{
			//render background houses
			RenderImage(backgroud3->tex, 0, 0, DisplaySizeX, DisplaySizeY);
			RenderImage(backgroud5->tex, 0, 0, DisplaySizeX, DisplaySizeY);
			//render front houses
			RenderImage(backgroud4->tex, 0, 0, DisplaySizeX, DisplaySizeY);
			RenderImage(backgroud6->tex, 0, 0, DisplaySizeX, DisplaySizeY);					
		}
		
		if(gamestate == MENU)
		{
			//Line up
			///DrawRectangle(0x98C47AD2, 0, DisplaySizeY / 38.4, DisplaySizeX, DisplaySizeY / 14.436);
			//Money Level
			///DrawButtonTextCustom(0xffffff00, DisplaySizeX / 8, DisplaySizeY / 38.4, DisplaySizeX / 2.2, DisplaySizeY / 14.436, 0xFFFFFFFF, "Money: 0$ l Level: 1", 0x000000ff);
			
			int GetSizeButtonY = DisplaySizeX / 5.4; //200 px
			int SpaceBetweenButtons = DisplaySizeX / 10.8; //100 px
			
			//start game
			//background outline
			DrawOutlineButton(0x4CC4DEFF, DisplaySizeX / 4, DisplaySizeY / 3, DisplaySizeX / 1.3333, (DisplaySizeY / 3) + GetSizeButtonY, 8);
			if(DrawButtonText(0xFFEF06ff, DisplaySizeX / 4, DisplaySizeY / 3, DisplaySizeX / 1.3333, (DisplaySizeY / 3) + GetSizeButtonY, 0x000000ff, "Start game"))
			{
				gamestate = GAME;
			}
			
			//Shop
			//background outline
			DrawOutlineButton(0x4CC4DEFF, DisplaySizeX / 4, (DisplaySizeY / 3) + GetSizeButtonY + SpaceBetweenButtons, DisplaySizeX / 1.3333, (DisplaySizeY / 3) + (GetSizeButtonY*2) + SpaceBetweenButtons, 8);
			if(DrawButtonText(0xFFEF06ff, DisplaySizeX / 4, (DisplaySizeY / 3) + GetSizeButtonY + SpaceBetweenButtons, DisplaySizeX / 1.3333, (DisplaySizeY / 3) + (GetSizeButtonY*2) + SpaceBetweenButtons, 0x000000ff, "Shop"))
			{
				gamestate = SHOP;
			}

			//info
			//background outline
			DrawOutlineButton(0x4CC4DEFF, DisplaySizeX / 4, (DisplaySizeY / 3) + (GetSizeButtonY*2) + (SpaceBetweenButtons*2), DisplaySizeX / 1.3333, (DisplaySizeY / 3) + (GetSizeButtonY*3) + (SpaceBetweenButtons*2), 8);
			if(DrawButtonText(0xFFEF06ff, DisplaySizeX / 4, (DisplaySizeY / 3) + (GetSizeButtonY*2) + (SpaceBetweenButtons*2), DisplaySizeX / 1.3333, (DisplaySizeY / 3) + (GetSizeButtonY*3) + (SpaceBetweenButtons*2), 0x000000ff, "Info"))
			{
				gamestate = INFO;
			}
		}
		else if(gamestate == GAME)
		{
			//render background houses
			
			if (GetTickCountGame() - back_houses_move > 25)
			{
				if (next_pos_back_houses >= DisplaySizeX)
					next_pos_back_houses = 0;
				else
					next_pos_back_houses += 1;

				back_houses_move = GetTickCountGame();
			}

			RenderImage(backgroud3->tex, 0 - next_pos_back_houses, 0, DisplaySizeX, DisplaySizeY);
			if (DisplaySizeX - next_pos_back_houses < DisplaySizeX)
			{
				RenderImage(backgroud3->tex, 0 - next_pos_back_houses + DisplaySizeX, 0, DisplaySizeX, DisplaySizeY);
			}
			RenderImage(backgroud5->tex, 0 - next_pos_back_houses, 0, DisplaySizeX, DisplaySizeY);
			if (DisplaySizeX - next_pos_back_houses < DisplaySizeX)
			{
				RenderImage(backgroud5->tex, 0 - next_pos_back_houses + DisplaySizeX, 0, DisplaySizeX, DisplaySizeY);
			}	

			if(GetTickCountGame() - front_houses_move > 1)
			{
				if(next_pos_front_houses >= DisplaySizeX)
					next_pos_front_houses = 0;
				else
					next_pos_front_houses += 1;

				front_houses_move = GetTickCountGame();	
			}	

			RenderImage(backgroud4->tex, 0 - next_pos_front_houses, 0, DisplaySizeX, DisplaySizeY);
			if (DisplaySizeX - next_pos_front_houses < DisplaySizeX)
			{
				RenderImage(backgroud4->tex, 0 - next_pos_front_houses + DisplaySizeX, 0, DisplaySizeX, DisplaySizeY);
			}
			RenderImage(backgroud6->tex, 0 - next_pos_front_houses, 0, DisplaySizeX, DisplaySizeY);
			if (DisplaySizeX - next_pos_front_houses < DisplaySizeX)
			{
				RenderImage(backgroud6->tex, 0 - next_pos_front_houses + DisplaySizeX, 0, DisplaySizeX, DisplaySizeY);
			}	

			if(BirdJump(0,0,DisplaySizeX, DisplaySizeY))
			{
				if(!StepUpdateBirdTask)
				{
					StepUpdateBirdTask = true;
				}
				else
				{
					StepUpdateBirdSync = 0;
					StepUpdateBirdTask = true;
				}					
			}

			//sync move bird
			if(StepUpdateBirdTask)
			{
				if(StepUpdateBirdSync < 70)
				{
					StepUpdateBirdSync++;
					for(int i = 0; i < 8; i++)
						Cyber.posY -= 1;
				}
				else
				{
					StepUpdateBirdTask = false;
					StepUpdateBirdSync = 0;
					BirdGravity = 5;
				}
			}

			if(BirdGravity == 0)
			{
				Cyber.posY += 4;
			}
			else
			{
				BirdGravity--;
			}

			//detect death bird
			int lines = DisplaySizeY / 8.5;
			if(Cyber.posY < lines/2 || Cyber.posY > DisplaySizeY - lines)
			{
				Logs("game over!");
			}

			//render columns
			if(GetTickCountGame() - update_pos_columns > 1)
			{
				step_update_columns += 2;
				update_pos_columns = GetTickCountGame();			
			} 
 
			RenderImage(column1->tex, DisplaySizeX - step_update_columns, 640, -200, -640);

			//render bird
			RenderImage(bird->tex, Cyber.posX, Cyber.posY, Cyber.size, Cyber.size);			

		}
		else if(gamestate == GAME_OVER)
		{


		}
		else if(gamestate == SHOP)
		{	
			char* text_shop = "Shop";
			DrawRectangle(0x98C47AD2,(DisplaySizeX / 2) - (GetTextSizeX(text_shop,DisplaySizeX / 90)/2) - 25, (DisplaySizeY / 6.4) - 25, (DisplaySizeX / 2) + (GetTextSizeX(text_shop,DisplaySizeX / 90)/2) + 25, (DisplaySizeY / 6.4) + GetTextSizeY(text_shop, DisplaySizeX / 90) + 25); 
			RenderText(0xFFFFFFFF, text_shop, (DisplaySizeX / 2) - (GetTextSizeX(text_shop,DisplaySizeX / 90)/2), DisplaySizeY / 6.4, DisplaySizeX / 216, DisplaySizeX / 90, true);
			
			//render image...
			char* shop_text = "In the new update...";
			
			RenderText(0xE7E7E7FF, shop_text, (DisplaySizeX / 2) - (GetTextSizeX(shop_text, DisplaySizeX / 154)/2), DisplaySizeY / 3, DisplaySizeX / 270, DisplaySizeX / 154, true);
			
			int GetSizeButtonY = DisplaySizeX / 5.4; //200 px
			DrawOutlineButton(0x4CC4DEFF, DisplaySizeX / 4, DisplaySizeY / 1.25, DisplaySizeX / 1.3333, (DisplaySizeY / 1.25) + GetSizeButtonY, 8);
			if(DrawButtonText(0xFFEF06ff, DisplaySizeX / 4, DisplaySizeY / 1.25, DisplaySizeX / 1.3333, (DisplaySizeY / 1.25) + GetSizeButtonY, 0x000000ff, "Back"))
			{
				gamestate = MENU;
			}			
		}
		else if(gamestate == INFO)
		{
			char* text_info = "Info";
			DrawRectangle(0x98C47AD2,(DisplaySizeX / 2) - (GetTextSizeX(text_info,DisplaySizeX / 90)/2) - 25, (DisplaySizeY / 6.4) - 25, (DisplaySizeX / 2) + (GetTextSizeX(text_info,DisplaySizeX / 90)/2) + 25, (DisplaySizeY / 6.4) + GetTextSizeY(text_info, DisplaySizeX / 90) + 25); 
			RenderText(0xFFFFFFFF, text_info, (DisplaySizeX / 2) - (GetTextSizeX(text_info,DisplaySizeX / 90)/2), DisplaySizeY / 6.4, DisplaySizeX / 216, DisplaySizeX / 90, true);			
			
			char* info_text[9];
			info_text[0] = "This game was written in pure C language";
			info_text[1] = "When using RawDraw graphics.";
			info_text[2] = "I hope to get a nomination";
			info_text[3] = "from Google as the smallest game.";
			info_text[4] = "I wanted to write a game without using Java.";
			info_text[5] = "Cyberpunk 2077 style game";
			info_text[6] = "Author: Vadim Boev Dev";
			info_text[7] = "Thanks for help me:";
			info_text[8] = "CnLohr, Tempa and other..";
			
			RenderText(0xE7E7E7FF, info_text[0], (DisplaySizeX / 2) - (GetTextSizeX(info_text[4], DisplaySizeX / 154)/2), DisplaySizeY / 3, DisplaySizeX / 270, DisplaySizeX / 154, true);
			for(int i = 1; i<9; i++)
			{
				RenderText(0xE7E7E7FF, info_text[i], (DisplaySizeX / 2) - (GetTextSizeX(info_text[4], DisplaySizeX / 154)/2), (DisplaySizeY / 3) + (GetTextSizeY(info_text[i],DisplaySizeX / 154)*i), DisplaySizeX / 270, DisplaySizeX / 154, true);
			}	

			short skipY = (DisplaySizeY / 3) + (GetTextSizeY(info_text[4],DisplaySizeX / 154)*10);
			short center = DisplaySizeX / 2;
			short sizepng = DisplaySizeX / 6.75;
			short space = DisplaySizeX / 7.2; //150 px
			short space_low = DisplaySizeX / 5.68421;
			
			int offset_rect = 5;
			if(step_update == 0)
			{
				if(GetTickCountGame() - update_background_button < 1000)
				{
					DrawRectangle(0x4CC4DEFF, center - (sizepng/2) - space - sizepng + offset_rect, skipY + offset_rect, center - (sizepng/2) - space + offset_rect, skipY + sizepng + offset_rect);
				}
				else
				{
					step_update = 1;
					update_background_button = GetTickCountGame();					
				}
			}
			else if(step_update == 1)
			{
				if(GetTickCountGame() - update_background_button < 1000)
				{
					DrawRectangle(0x4CC4DEFF, center - (sizepng/2) + offset_rect, skipY + offset_rect, center + (sizepng/2) + offset_rect, skipY + sizepng + offset_rect);
				}
				else
				{
					step_update = 2;
					update_background_button = GetTickCountGame();					
				}
			}
			else if(step_update == 2)
			{
				if(GetTickCountGame() - update_background_button < 1000)
				{				
					DrawRectangle(0x4CC4DEFF,center + (sizepng/2) + space + offset_rect, skipY + offset_rect, center + (sizepng/2) + space + sizepng + offset_rect, skipY + sizepng + offset_rect);
				}
				else
				{
					step_update = 3;
					update_background_button = GetTickCountGame();					
				}
			}
			else if(step_update == 3)
			{
				if(GetTickCountGame() - update_background_button < 1000)
				{				
					DrawRectangle(0x4CC4DEFF,center - space_low/2 - sizepng + offset_rect, skipY + space/2 + sizepng + offset_rect, center - space_low/2 + offset_rect, skipY + space/2 + sizepng*2 + offset_rect);
				}
				else
				{
					step_update = 4;
					update_background_button = GetTickCountGame();					
				}					
			}
			else if(step_update == 4)
			{
				if(GetTickCountGame() - update_background_button < 1000)
				{				
					DrawRectangle(0x4CC4DEFF,center + space_low/2 + offset_rect, skipY + space/2 + sizepng + offset_rect, center + space_low/2 + sizepng + offset_rect, skipY + space/2 + sizepng*2 + offset_rect);
				}
				else
				{
					step_update = 0;
					update_background_button = GetTickCountGame();				
				}	
			}

			RenderImage(browser->tex, center - (sizepng/2) - space - sizepng, skipY, sizepng, sizepng);
			if(DrawButtonText(0xffffff00, center - (sizepng/2) - space - sizepng, skipY, center - (sizepng/2) - space, skipY + sizepng, 0x00000000, ""))
			{
				DrawRectangle(0xE206FFd2, center - (sizepng/2) - space - sizepng, skipY, center - (sizepng/2) - space, skipY + sizepng);
				OpenUrl("https://boev.dev/");
			}

			RenderImage(vk->tex, center - (sizepng/2), skipY, sizepng, sizepng);
			if(DrawButtonText(0xffffff00, center - (sizepng/2), skipY, center + (sizepng/2), skipY + sizepng, 0x00000000, ""))
			{
				DrawRectangle(0xE206FFd2, center - (sizepng/2), skipY, center + (sizepng/2), skipY + sizepng);
				OpenUrl("https://vk.com/kronka_vk/");
			}
			RenderImage(github->tex, center + (sizepng/2) + space, skipY, sizepng, sizepng);
			if(DrawButtonText(0xffffff00, center + (sizepng/2) + space, skipY, center + (sizepng/2) + space + sizepng, skipY + sizepng, 0x00000000, ""))
			{
				DrawRectangle(0xE206FFd2, center + (sizepng/2) + space, skipY, center + (sizepng/2) + space + sizepng, skipY + sizepng);
				OpenUrl("https://github.com/Kronka/");
			}			

			RenderImage(youtube->tex, center - space_low/2 - sizepng, skipY + space/2 + sizepng, sizepng, sizepng);
			if(DrawButtonText(0xffffff00, center - space_low/2 - sizepng, skipY + space/2 + sizepng, center - space_low/2, skipY + space/2 + sizepng*2, 0x00000000, ""))
			{
				DrawRectangle(0xE206FFd2,center - space_low/2 - sizepng, skipY + space/2 + sizepng, center - space_low/2, skipY + space/2 + sizepng*2);
				OpenUrl("https://www.youtube.com/c/vadimboevdev/");
			}			
			
			RenderImage(tiktok->tex, center + space_low/2, skipY + space/2 + sizepng, sizepng, sizepng);
			if(DrawButtonText(0xffffff00, center + space_low/2, skipY + space/2 + sizepng, center + space_low/2 + sizepng, skipY + space/2 + sizepng*2, 0x00000000, ""))
			{
				DrawRectangle(0xE206FFd2,center + space_low/2, skipY + space/2 + sizepng, center + space_low/2 + sizepng, skipY + space/2 + sizepng*2);
				OpenUrl("https://www.tiktok.com/@kronka_vk/");
			}		
			
			int GetSizeButtonY = DisplaySizeX / 5.4; //200 px
			DrawOutlineButton(0x4CC4DEFF, DisplaySizeX / 4, DisplaySizeY / 1.25, DisplaySizeX / 1.3333, (DisplaySizeY / 1.25) + GetSizeButtonY, 8);
			if(DrawButtonText(0xFFEF06ff, DisplaySizeX / 4, DisplaySizeY / 1.25, DisplaySizeX / 1.3333, (DisplaySizeY / 1.25) + GetSizeButtonY, 0x000000ff, "Back"))
			{
				gamestate = MENU;
			}				
		}
		
		//render lines
		RenderImage(backgroud7->tex, 0, 0, DisplaySizeX, DisplaySizeY);

		frames++;
		//On Android, CNFGSwapBuffers must be called, and CNFGUpdateScreenWithBitmap does not have an implied framebuffer swap.
		CNFGSwapBuffers(); 

		ThisTime = OGGetAbsoluteTime();
		if( ThisTime > LastFPSTime + 1 )
		{
			printf( "FPS: %d\n", frames );
			frames = 0;
			linesegs = 0;
			LastFPSTime+=1;
		}

	}

	return(0);
}



/*
std::string getDeviceId(JNIEnv *env, ANativeActivity *activity)
{
    jclass classActivity = env->FindClass("android/app/NativeActivity");
    jclass context = env->FindClass("android/content/Context");
    jclass telephony = env->FindClass("android/telephony/TelephonyManager");

    jfieldID field = env->GetStaticFieldID(context, "TELEPHONY_SERVICE", "Ljava/lang/String;");
    jobject staticField = env->GetStaticObjectField(context, field);

    jmethodID getSS = env->GetMethodID(classActivity, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jobject objTel = env->CallObjectMethod(activity->clazz, getSS, staticField);
    jmethodID getId = env->GetMethodID(telephony, "getDeviceId", "()Ljava/lang/String;");
    jstring strId = (jstring)env->CallObjectMethod(objTel, getId);

    env->DeleteLocalRef(classActivity);
    env->DeleteLocalRef(context);
    env->DeleteLocalRef(telephony);
    return jstring2string(env, strId);
}

//<uses-permission android:name="android.permission.VIBRATE"/>
short vibrate(short duration)
{
    #if defined(__ANDROID__)
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    jclass clazz(env->GetObjectClass(activity));
    JavaVM* vm;
    env->GetJavaVM(&vm);

    // First, attach this thread to the main thread
    JavaVMAttachArgs attachargs;
    attachargs.version = JNI_VERSION_1_6;
    attachargs.name = "NativeThread";
    attachargs.group = NULL;
    jint res = vm->AttachCurrentThread(&env, &attachargs);

    if (res == JNI_ERR) return EXIT_FAILURE;

    // Retrieve class information
    jclass natact = env->FindClass("android/app/NativeActivity");
    jclass context = env->FindClass("android/content/Context");

    // Get the value of a constant
    jfieldID fid = env->GetStaticFieldID(context, "VIBRATOR_SERVICE", "Ljava/lang/String;");
    jobject svcstr = env->GetStaticObjectField(context, fid);

    // Get the method 'getSystemService' and call it
    jmethodID getss = env->GetMethodID(natact, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jobject vib_obj = env->CallObjectMethod(activity, getss, svcstr);

    // Get the object's class and retrieve the member name
    jclass vib_cls = env->GetObjectClass(vib_obj);
    jmethodID vibrate = env->GetMethodID(vib_cls, "vibrate", "(J)V");

    // Determine the timeframe
    jlong length = duration;

    // Bzzz!
    env->CallVoidMethod(vib_obj, vibrate, length);

    // Free references
    env->DeleteLocalRef(vib_obj);
    env->DeleteLocalRef(vib_cls);
    env->DeleteLocalRef(svcstr);
    env->DeleteLocalRef(context);
    env->DeleteLocalRef(natact);
    env->DeleteLocalRef(clazz);

    // Detach thread again
    // this line is comment because it cause a bug
    // vm->DetachCurrentThread();
    #elif defined(IS_ENGINE_HTML_5)
    EM_ASM_ARGS({
                navigator.vibrate($0);
                }, duration);
    #else
    is::showLog("Vibrate Called ! Time : " + is::numToStr(duration) + " ms");
    #endif

    return 1; // EXIT_SUCCESS;
}
*/