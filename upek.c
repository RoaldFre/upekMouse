#include <bsapi.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <string.h>
#include <signal.h>
#include <error.h>

#include <unistd.h> //debug


#define DEFAULT_FILE	"/dev/input/uinput"

#define DEVICE_NAME	"Upek fingerprint reader"
#define VERSION		1
#define	BUSTYPE		BUS_USB


#define OPERATION_ID          (0xbabe)  
		/* Magic number used to identify the navigation operation. */


#define DEF_Y_SCL	0.08
#define DEF_X_SCL	0.05
#define DEF_Y_F_FRICT	0.3
#define DEF_X_F_FRICT	0.6
#define DEF_Y_MOMENTUM	0.97
#define DEF_X_MOMENTUM	0.8

#define NUM_Y_AVERAGE	5
#define NUM_X_AVERAGE	5


/* Globals */
static int outFile;		/* fd of virtual input file */
static ABS_CONNECTION conn;	/* connection to fingerprint reader */
static struct input_event event;	/* saves me about 90 memsets per sec */

static struct {
	char *filename;
	float xScale;		/* Rescales the raw output */
	float yScale;		
	float xFingerFrict;	/* Grip coefficient of the finger */
	float yFingerFrict; 
	float xMomentum;	/* Momentum coefficient of the scroll wheel, */
	float yMomentum;	/*   basically: (1 - frictionCoefficient) */

} config;

int debug = 0;



/* Prototypes */
static void	click_button(__s32, int count);
static void	closeOutFile();
static int	connect_fingerprint(ABS_CONNECTION *conn);
static void	destrOutFile();
static int	disconnect_fingerprint(ABS_CONNECTION *conn);
static void	handle_click();
static void	handle_motion_axes(int, int dy);
static int	initOutFile();
static void	init_sigHandler();
static void	navigation_callback(const ABS_OPERATION*, ABS_DWORD, void* pMsgData);
static void	parse_options(int, char **argv);
static void	sigHandler(int sig);
static int	start_navigation(ABS_CONNECTION *conn);


static int connect_fingerprint(ABS_CONNECTION *conn)
{
	ABS_STATUS status;

	printf("Connecting to fingerprintreader\n");

	/* Initialize BSAPI and open session with fingerprint reader device. */
	ABSInitialize();
	status = ABSOpen("usb", conn);
	if(status != ABS_STATUS_OK) {
		fprintf(stderr, "[%ld] ABSOpen() failed.\n", (long)status);
	//usleep(1000000);
		return -1;
	}
	return 0;
}

static int disconnect_fingerprint(ABS_CONNECTION *conn)
{
	ABS_STATUS status;

	printf("Closing fingerprint connection\n");
	//usleep(1000000);
	status = ABSClose(*conn);
	if(status != ABS_STATUS_OK) {
		fprintf(stderr, "[%ld] ABSClose() failed.\n", (long)status);
	//usleep(1000000);
		return -1;
	}
	return 0;
}




static int initOutFile()
{
	int ret;
	struct uinput_user_dev uinp;	/* our input device information */

	/* Try opening the file */
	printf("Opening %s\n",config.filename);
	//usleep(1000000);
	outFile = open(config.filename, O_WRONLY | O_NDELAY);
	if (-1 == outFile){
		fprintf(stderr, "Error opening file %s (error %d)\n",
				config.filename, outFile);
		perror("open");
	//usleep(1000000);
		return -1;
	}

	/* Setup defice information */
	memset(&uinp, 0, sizeof(uinp));
	strncpy(uinp.name, DEVICE_NAME, UINPUT_MAX_NAME_SIZE);
	uinp.id.version = VERSION;
	uinp.id.bustype = BUSTYPE;
	if ( (int)sizeof(uinp) > write(outFile, &uinp, sizeof(uinp))){
		fprintf(stderr, "Error writing defice information\n");
	//usleep(1000000);
		closeOutFile();
		return -1;
	}

	
	/* Setup all the flags */
	ret  = ioctl(outFile, UI_SET_EVBIT, EV_KEY);
	ret |= ioctl(outFile, UI_SET_EVBIT, EV_REL);
	ret |= ioctl(outFile, UI_SET_RELBIT, REL_X);
	ret |= ioctl(outFile, UI_SET_RELBIT, REL_Y);
	ret |= ioctl(outFile, UI_SET_RELBIT, REL_WHEEL);
	ret |= ioctl(outFile, UI_SET_RELBIT, REL_HWHEEL);
	ret |= ioctl(outFile, UI_SET_KEYBIT, BTN_MOUSE);
	ret |= ioctl(outFile, UI_SET_KEYBIT, BTN_MIDDLE);
	ret |= ioctl(outFile, UI_SET_KEYBIT, BTN_RIGHT);
	
	ret |= ioctl(outFile, UI_DEV_CREATE);
	if (ret){
		perror("ioctl");
		fprintf(stderr, "ERROR: ioctl() failed\n");
	//usleep(1000000);
		closeOutFile();
		return -1;
	}

	return 0;
}


static void destrOutFile()
{
	printf("Unregistering event file\n");
	//usleep(1000000);
	if (-1 == ioctl(outFile, UI_DEV_DESTROY)){
		perror("ioctl");
		fprintf(stderr,"ERROR: Couldn't destroy the uinput device!\n");
	//usleep(1000000);
	}
	closeOutFile();
}

static void closeOutFile()
{
	printf("Closing outFile\n");
	//usleep(1000000);
	close(outFile);
}


static void handle_motion_phys(ABS_NAVIGATION_DATA *data)
{
	static float vx = 0;
	static float vy = 0;
	static float dxCumul = 0;
	static float dyCumul = 0;
	static int prevx[NUM_X_AVERAGE] = {0}; /* circular averageing buffer */
	static int prevy[NUM_Y_AVERAGE] = {0};
	static int ix = 0, iy = 0; /* index of x and y in averageing buffer */

	float vx_in, vy_in;
	int i;

	if (debug)
		printf("x: %d,\ty: %d\n", data->DeltaX, data->DeltaY);	

	ix = (ix + 1) % NUM_X_AVERAGE;
	iy = (iy + 1) % NUM_Y_AVERAGE;

	vx *= config.xMomentum;
	vy *= config.yMomentum;
	if (data->FingerPresent == ABS_TRUE){
		prevx[ix] = data->DeltaX;
		prevy[iy] = data->DeltaY;

		/* Input velocity from finger (averaged).
		 * The callback needs to be called at a fixed interval for 
		 * this to work. Luckily, this seems to be the case.  
		 * Frequency is in the range of 90Hz */
		vx_in = vy_in = 0;
		for (i=0; i<NUM_X_AVERAGE; i++)
			vx_in += prevx[i];
		for (i=0; i<NUM_Y_AVERAGE; i++)
			vy_in += prevy[i];
		
		vx += config.xFingerFrict * (vx_in / NUM_X_AVERAGE - vx);
		vy += config.yFingerFrict * (vy_in / NUM_Y_AVERAGE - vy);
	} else {
		prevx[ix] = 0;
		prevy[iy] = 0;
	}

	dxCumul += vx * config.xScale;
	dyCumul += vy * config.yScale;
	handle_motion_axes((int)(dxCumul), (int)(dyCumul));
	dxCumul -= (int)dxCumul;
	dyCumul -= (int)dyCumul;
}


static void handle_motion_axes(int dx, int dy)
{
	int ret;

	if (debug)
		printf("motion: \tdx: %d,\tdy: %d\n",dx,dy);

	if (dx){
		event.type = EV_REL;
		event.code = REL_HWHEEL;
		event.value = -dx; /* It somehow gets reversed somewhere >_< */
		ret = write(outFile, &event, sizeof event);
	}
	
	if (dy){
		event.type = EV_REL;
		event.code = REL_WHEEL;
		event.value = -dy; /* likewise */
		ret = write(outFile, &event, sizeof event);
	}

	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	ret = write(outFile, &event, sizeof event);
}

static void handle_click()
{
	if (debug)
		printf("Clicked\n");
	click_button(BTN_MIDDLE, 1);
}

static void click_button(__s32 button, int count){
	int i;
	int ret;

	if (!count)
		return;
	
	for (i=0; i<count; i++){
		event.type = EV_KEY;
		event.code = button;
		event.value = 1;
		ret = write(outFile, &event, sizeof event);

		event.type = EV_KEY;
		event.code = button;
		event.value = 0;
		ret = write(outFile, &event, sizeof event);
	}
}


/* This function gets called about 90 times a second on my computer, 
 * regardless of whether or not there is an actual event (eg. a click, a 
 * movement). */
static void navigation_callback(const ABS_OPERATION* pOperation, 
                   ABS_DWORD dwMsg, void* pMsgData)
{
	(void) pOperation; /* stop compiler from whining */
	ABS_NAVIGATION_DATA *data;

	switch(dwMsg) {
	case ABS_MSG_NAVIGATE_CHANGE:
		data = (ABS_NAVIGATION_DATA*) pMsgData;
		handle_motion_phys(data);
		break;
	case ABS_MSG_NAVIGATE_CLICK:
		handle_click();
		break;
	default:
		break;
	}
}


static int start_navigation(ABS_CONNECTION *conn)
{
	ABS_STATUS status;
	ABS_OPERATION operation;

	printf("Starting navigation\n");

	memset(&event, 0, sizeof(event)); /* zero it once, used by several 
					     functions */

	operation.Context = NULL;	/* parameter of callback function */
	operation.Callback = navigation_callback;  /* Pointer to callback */
	operation.Timeout = 0;		/*Timeout is ignored by ABSNavigate()*/
	operation.Flags = 0;
	operation.OperationID = OPERATION_ID;

	status = ABSNavigate(*conn, &operation, 0);

	/* After the navigation is over, it is either an error; or it has 
	 * been canceled from the main thread. ABSNavigate never returns 
	 * ABS_STATUS_OK. */
	if(status == ABS_STATUS_CANCELED){
		printf("Navigation got cancelled\n");
	//usleep(1000000);
		return 0;
	}

	/* Some devices do not support navigation so be more user 
	 * friendly and check this error specially */
	if(status == ABS_STATUS_NOT_SUPPORTED) {
		fprintf(stderr, "Error: Your hardware doesn't support "
				"navigation\n");
		return -1;
	} else {
		fprintf(stderr, "Error: An error occured during navigation\n");
		return -1;
	}
}




int main(int argc, char **argv)
{
	int ret;

	parse_options(argc, argv);

	if (0 != connect_fingerprint(&conn))
		return -1;

	init_sigHandler();

	if (0 != initOutFile()){
		disconnect_fingerprint(&conn);
		return -1;
	}

	ret = start_navigation(&conn);

	printf("Cleaning up\n");
	//usleep(1000000);
	disconnect_fingerprint(&conn);
	destrOutFile();

	return ret;
}

static void parse_options(int argc, char **argv)
{
	(void) argv;
	if (argc >= 2)
		debug = 1;

	config.filename = (char*) DEFAULT_FILE;
	
	config.xScale =		DEF_X_SCL; 
	config.yScale =		DEF_Y_SCL;    
	config.xFingerFrict =	DEF_X_F_FRICT;
	config.yFingerFrict =	DEF_Y_F_FRICT;
	config.xMomentum =	DEF_X_MOMENTUM;
	config.yMomentum =	DEF_Y_MOMENTUM; 
}

static void init_sigHandler()
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));

	sa.sa_handler = sigHandler;
	sigaction(SIGALRM, &sa, NULL);
	sigaction(SIGINT,  &sa, NULL);
}

static void sigHandler(int sig)
{
	ABS_STATUS status;

	printf("\nCaught signal %d. Exiting.\n", sig);
	//usleep(1000000);

	/* Cancel the operation, the navigation function will stop, and 
	 * main will exit cleanly */
	status = ABSCancelOperation(conn, OPERATION_ID);
	if (status == ABS_STATUS_OK){
		printf("Cancel command sent sucessfully\n");
	//usleep(1000000);
		return;
	}
	
	fprintf(stderr, "ERROR: Cancel failed! Error %d\n", status);
	//usleep(1000000);
	/* Might try doing it the hard way, just closing the connection 
	 * etc. Or the user should do it himself, retrying the signal or 
	 * sending a stronger signal.
	 * I'll just leave it to him. */
}

