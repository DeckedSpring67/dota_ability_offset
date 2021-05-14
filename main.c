#include <X11/Xlib.h>
#include <stdio.h>
#include <X11/X.h>
#include <X11/Xutil.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <png.h>
#include <unistd.h>
#include <time.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>


//Relative position 
const float relx = 0.284375;
const float rely = 0.93055555555;
//For 1080p
const unsigned long p_values1080[4] = {0x1b2025, 0x30353a, 0x43484e, 0x3f4349};
//For 1440p
const unsigned long p_values1440[4] = {0x282d32, 0x383c41, 0x45494f, 0x41464b};


static int trapped_error_code = 0;
static int (*old_error_handler) (Display *, XErrorEvent *);


static int error_handler(Display *display, XErrorEvent *error){
	trapped_error_code = error->error_code;
	return 0;
}

void
trap_errors(void)
{
    trapped_error_code = 0;
    old_error_handler = XSetErrorHandler(error_handler);
}

int
untrap_errors(void)
{
    XSetErrorHandler(old_error_handler);
    return trapped_error_code;
}

//Returns the offset for the 4 pixels we're searching for, if they're not found returns -1 (we don't expect the HUD to shrink to lesss than 4 abilities)
int findOffset(XImage *img){
	int original_x = relx * img->width;
	int original_y = rely * img->height;
	int i;

	//Find the 4 pixels within the fixed position
	for(i = 0; i <= original_x; ++i){
		if(img->height < 1440){ //1080p
			if(XGetPixel(img,i,original_y) == p_values1080[0] && XGetPixel(img,i+1,original_y) == p_values1080[1] && XGetPixel(img,i+2,original_y) == p_values1080[2] && XGetPixel(img,i+3,original_y) == p_values1080[3]){
				return (original_x - i);
			}
		}
		else{ //1440p
			if(XGetPixel(img,i,original_y) == p_values1440[0] && XGetPixel(img,i+1,original_y) == p_values1440[1] && XGetPixel(img,i+2,original_y) == p_values1440[2] && XGetPixel(img,i+3,original_y) == p_values1440[3]){
				return (original_x - i);
			}
		}
	}

	return -1;
}

//Return 0: success
//Return 1: missing empty_mask
//Return 2: missing game_mask 
//Return 3: missing level_mask 
int createMask(int new_offset, int x_offset, int y_offset){
	char *e_mask = "empty_mask.png";
	char *game_mask = "game_mask.png";
	char *level_mask = "level_mask.png";
	char command[1000];
	FILE *f;

	//Check if we have the needed files
	if(!(f = fopen(e_mask,"r"))){
		return 1;
	}
	if(!(f = fopen(game_mask,"r"))){
		return 2;
	}
	if(!(f = fopen(level_mask,"r"))){
		return 3;
	}
	
	//if new_offset is -1 just use the empty mask	
	if(new_offset < 0){
		sprintf(command,"cp -f empty_mask.png mask.png");
	}
	else{
		sprintf(command,"convert game_mask.png -draw \'image over %d,%d,0,0 level_mask.png\' mask.png",x_offset - new_offset, y_offset); 
	}
	//Run the command
	system(command);
	return 0;
}

void handleMaskErrors(int error){
	switch(error){
		case 1: printf("Please provide empty_mask.png\n");break;
		case 2: printf("Please provide game_mask.png\n");break;
		case 3: printf("Please provide level_mask.png\n");break;
	}
	return;
}

int main(int argc, char **argv){
	int i,status;
	Window *dota_window = NULL;
	char *window_name;
	unsigned long pixels[4];
	int newx;
	int newy;
	int new_offset = -1;
	int last_offset = -2;
	//For benchmarking purposes
	//clock_t start_time;
	//double elapsed_time;
	//Whatever I don't care if X11 complains about something >:)
	trap_errors();


	//Check for arguments
	if(argc < 3){
		printf("Usage: level_offset <X offset for level_mask> <Y offset for level mask>\n");
		return 1;
	}

	//Open Default Display
	Display *display = XOpenDisplay(NULL);

	//root Window
	Window root = DefaultRootWindow(display);

	//Get Atom
	Atom atom = XInternAtom(display, "_NET_CLIENT_LIST", true);

	Atom actualType;
    	int format;
    	unsigned long numItems;
    	unsigned long bytesAfter;
    	unsigned char *data ;
    	Window *list;
	XImage *img;
	Screen* screen;
	XShmSegmentInfo shminfo;
	Status s1;
	while(1){
	//	start_time = clock();
		//Get all the ACTIVE_WINDOWS
		XGetWindowProperty(display, root, atom, 0L, (~0L), false, AnyPropertyType, &actualType, &format, &numItems, &bytesAfter, &data);	

		//list contains all the Windows
		list = (Window *) data;

		//Get Dota Window
		for( i = 0; i < numItems; ++i){
			status = XFetchName(display, list[i], &window_name);
			if(status && (strstr(window_name,"Dota 2") != NULL)){
				dota_window = (list+i);
			}
			//Free memory since we don't need it anymore
			XFree(window_name);
		}

		if(!dota_window){
			printf("Dota 2 is not running\n");
			return 1;
		}

		//Get Window Attributes	
        	XWindowAttributes attributes = {0};
		XGetWindowAttributes(display, *dota_window, &attributes);

		screen = attributes.screen;
		//Create the image (empty)
		img = XShmCreateImage(display, DefaultVisualOfScreen(screen), DefaultDepthOfScreen(screen), ZPixmap, NULL, &shminfo, attributes.width, attributes.height);

		//Configure shm
	    	shminfo.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * img->height, IPC_CREAT|0777);
	    	shminfo.shmaddr = img->data = (char*)shmat(shminfo.shmid, 0, 0);
	    	shminfo.readOnly = False;

		//Attach shm
		s1 = XShmAttach(display, &shminfo);

		
		//Get the image from X11
		//NOTE: DOES NOT WORK ON XWAYLAND
		//img = XGetImage(display, *dota_window, 0,0, attributes.width, attributes.height, AllPlanes, ZPixmap);
		if(!XShmGetImage(display, *dota_window, img, 0, 0, 0x00ffffff)){
			printf("Couldn't get an image!\n");
		}

		//Relative pxiel position
		newx = (int) (attributes.width*relx);
		newy = (int) (attributes.height*rely);
		
		for(i = 0; i < 4; ++i){
			pixels[i] = XGetPixel(img,newx+i,newy);
		}


		//Get the new offset
		//printf("Pixel1:%lx Pixel2:%lx Pixel3:%lx Pixel4:%lx\n",pixels[0],pixels[1],pixels[2],pixels[3]);
		new_offset = findOffset(img);
		//printf("OFFSET: %d\n",new_offset);


		//Check if the offset changed, if it did create the new mask
		if(last_offset != new_offset){
			printf("OFFSET:%d\n",new_offset);
			last_offset = new_offset;
			handleMaskErrors(createMask(new_offset,atoi(argv[1]), atoi(argv[2])));
		}

		//Free image
		if(img){
			XDestroyImage(img);
		}
		//Free X11 objects
		XFree(data);
		//Benchmark stuff
		//elapsed_time = (double)(clock() - start_time) / CLOCKS_PER_SEC;		
		//printf("Done in %f seconds\n", elapsed_time);
		//sleep for 400ms
		usleep(400000);
	}
	XCloseDisplay(display);

	return 0;
}
