#include <X11/Xlib.h>
#include <stdio.h>
#include <X11/X.h>
#include <X11/Xutil.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>

//For closing the loop
static int keepRunning = 1;
//Relative position 
const float relx = 0.284375;
const float rely = 0.93055555555;
//For 1080p
const unsigned long p_values1080[4] = {0x1b2025, 0x30353a, 0x43484e, 0x3f4349};
//For 1440p
const unsigned long p_values1440[4] = {0x282d32, 0x383c41, 0x45494f, 0x41464b};

void ctrlCHandler(){
	keepRunning = 0;
}

struct getOffsetArgs{
	Display *display;
	Drawable window;
	int x;
	int y;
	int width;
	int height;
	unsigned long plane_mask;
	int format;
	int ret_offset;
};

static int trapped_error_code = 0;
static int (*old_error_handler) (Display *, XErrorEvent *);


static int error_handler(Display *display, XErrorEvent *error){
	trapped_error_code = error->error_code;
	return 0;
}

void trap_errors(void)
{
    trapped_error_code = 0;
    old_error_handler = XSetErrorHandler(error_handler);
}

//Returns the offset for the 4 pixels we're searching for, if they're not found returns -1 (we don't expect the HUD to shrink to lesss than 4 abilities)
//If the offset is negative we're probably looking at a creep, return -2
int findOffset(XImage *img){
	int original_x = relx * img->width;
	int original_y = rely * img->height;
	int *offset = NULL;
	int i;

	//Find the 4 pixels within the fixed position
	for(i = 0; i <= img->width; ++i){
		if(img->height < 1440){ //1080p
			if(XGetPixel(img,i,original_y) == p_values1080[0] && XGetPixel(img,i+1,original_y) == p_values1080[1] && XGetPixel(img,i+2,original_y) == p_values1080[2] && XGetPixel(img,i+3,original_y) == p_values1080[3]){
				offset = (int*) malloc(sizeof(int));
				*offset = original_x - i;
			}
		}
		else{ //1440p
			if(XGetPixel(img,i,original_y) == p_values1440[0] && XGetPixel(img,i+1,original_y) == p_values1440[1] && XGetPixel(img,i+2,original_y) == p_values1440[2] && XGetPixel(img,i+3,original_y) == p_values1440[3]){
				offset = (int*) malloc(sizeof(int));
				*offset = original_x - i;
			}
		}
	}
	if(offset == NULL){
		return -1;
	}
	else if(*offset < 0){
		return -2;
	}
	int ret = *offset;
	free(offset);
	return ret;
}

void *getOffsetThread(void *arguments){
	XImage *img;
	struct getOffsetArgs *args = (struct getOffsetArgs *) arguments;

	img = XGetImage(args->display, args->window, args->x, args->y, args->width, args->height, args->plane_mask, args->format);

	if(!img){
		printf("Couldn't get an image!\n");
		args->ret_offset = -1;
		return 0;
	}

	args->ret_offset = findOffset(img);

	//Free image
	if(img){
		XDestroyImage(img);
	}
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
	int retval;

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
	fclose(f);
	
	//if new_offset is -1 just use the empty mask	
	if(new_offset == -1){
		sprintf(command,"cp -f empty_mask.png mask.png");
	}
	//If new_offset is -2 use game mask
	else if(new_offset == -2){
		sprintf(command,"cp -f game_mask.png mask.png");
	}
	else{
		sprintf(command,"convert game_mask.png -define profile:skip=ICC -draw \'image over %d,%d,0,0 level_mask.png\' mask.png",x_offset - new_offset, y_offset); 
	}
	//Run the command
	retval = system(command);
	if(retval == 127)
		return retval;
	return 0;
}

void handleMaskErrors(int error){
	switch(error){
		case 1: printf("Please provide empty_mask.png\n");break;
		case 2: printf("Please provide game_mask.png\n");break;
		case 3: printf("Please provide level_mask.png\n");break;
		case 127: printf("Can't execute shell in child process");break;
	}
	return;
}



int main(int argc, char **argv){
	int i,status;
	Window *dota_window = NULL;
	char *window_name;
	XImage *img;
	/*DEBUG:  variables needed to print pixels
	 unsigned long pixels[4];
	int newx;
	int newy;
	*/
	int new_offset = -1;
	int last_offset = -2;
	//Whatever I don't care if X11 complains about something >:)
	trap_errors();
	//For CTRL-C management
	struct sigaction act;
	act.sa_handler = ctrlCHandler;
	memset(&act, 0, sizeof(act));
	sigaction(SIGINT, &act, NULL);
	int arg1 = atoi(argv[1]);
	int arg2 = atoi(argv[2]);


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
	pthread_t offsetThread;


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
		//Free memory (window name) since we don't need it anymore
		XFree(window_name);
	}

	if(!dota_window){
		printf("Dota 2 is not running\n");
		return 1;
	}

	//Get Window Attributes	
	XWindowAttributes attributes = {0};
	XGetWindowAttributes(display, *dota_window, &attributes);

	/*DEBUG: Relative pxiel position
	newx = (int) (attributes.width*relx);
	newy = (int) (attributes.height*rely);
	*/

	//Populate args for getting the image
	struct getOffsetArgs args; 
	args.display = display;
	args.window = *dota_window;
	args.x = 0;
	args.y = 0;
	args.width = attributes.width;
	args.height = attributes.height;
	args.plane_mask = AllPlanes;
	args.format = ZPixmap;

	//Main loop
	while(keepRunning){
		/*DEBUG: Print pixels
		for(i = 0; i < 4; ++i){
			pixels[i] = XGetPixel(img,newx+i,newy);
		}
		printf("Pixel1:%lx Pixel2:%lx Pixel3:%lx Pixel4:%lx\n",pixels[0],pixels[1],pixels[2],pixels[3]);
		*/

		img = XGetImage(args.display, args.window, args.x, args.y, args.width, args.height, args.plane_mask, args.format);

		if(!img){
			printf("Couldn't get an image!\n");
			args.ret_offset = -1;
		}


		//Free image
		if(img){
			new_offset = findOffset(img);
			XDestroyImage(img);
		}


		/* For Multithreading
		//Spawn thread
		if (pthread_create(&offsetThread, NULL, &getOffsetThread, (void *) &args) != 0){
			printf("Thread creation failed - skipping\n");
		}
		//Wait for thread
		pthread_join(offsetThread, NULL);
		new_offset = args.ret_offset;
		*/

		//Check if the offset changed, if it did create the new mask
		if(last_offset != new_offset){
			printf("OFFSET:%d\n",new_offset);
			last_offset = new_offset;
			handleMaskErrors(createMask(new_offset, arg1, arg2));
		}

		
		//Free X11 objects
		usleep(400000);
	}
	XFree(data);
	XCloseDisplay(display);

	return 0;
}
