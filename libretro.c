/*
some resources used in writing this:
http://thiemonge.org/getting-started-with-uinput
http://cgit.freedesktop.org/~whot/testdevices
http://wilmer.gaa.st/main.php/uhat.html
http://www.einfochips.com/download/dash_jan_tip.pdf
*/

#include "libretro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <linux/uinput.h>

const static struct retro_system_info _info = {
	.library_name = "libretro2joy",
	.library_version = "v0",
	.need_fullpath = true,
	.valid_extensions = NULL,
};

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

//static uint16_t *frame_buf;
void retro_init(void)
{
	//frame_buf = calloc(320 * 240, sizeof(uint16_t));
}
void retro_deinit(void)
{
	//free(frame_buf);
	//frame_buf = NULL;
}

bool retro_load_game(const struct retro_game_info *info)
{
	(void)info;
	retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
	return true;
}

void retro_unload_game(void)
{
	retro_set_controller_port_device(0, RETRO_DEVICE_NONE);
}

void retro_reset(void)
{
}

#define NUM_PADS 8
int uinp_fd[NUM_PADS] = {-1, -1, -1, -1, -1, -1, -1, -1};
inline void close_uinp_fd(unsigned port) {
	close(uinp_fd[port]);
	uinp_fd[port] = -1;
}
#define ioctl_warn(fd, val, arg) { \
	if ( ioctl(fd, val, arg) ) \
		perror("ioctl " #val " " #arg); \
}
inline void ioctl_set_absbit(int fd, struct uinput_user_dev *p_uinp,
                             int axis, int low, int high) {
	ioctl_warn(fd, UI_SET_ABSBIT, axis);
	p_uinp->absmin[axis] = low;
	p_uinp->absmax[axis] = high;
}
void retro_set_controller_port_device(unsigned port, unsigned device)
{
	int i;

	printf("set_controller_port_device(%d, %d)\n", port, device);
	if ( device == RETRO_DEVICE_NONE ) {
		// unplug, if it's already plugged.
		if ( uinp_fd[port] != -1 ) {
			ioctl_warn(uinp_fd[port], UI_DEV_DESTROY, 0);
			close_uinp_fd(port);
		}
	} else if ( device != RETRO_DEVICE_JOYPAD ) {
		puts("device types other than JOYPAD not yet implemented.");
	} else if ( uinp_fd[port] != -1 ) {
		printf("port %d already has controller (fd %d).\n", port, uinp_fd[port]);
	} else if ( (uinp_fd[port] = open("/dev/uinput", O_WRONLY | O_NONBLOCK)) == -1 ) {
		perror("opening /dev/uinput");
	} else {
		/* Define our virtual input device. */
		struct uinput_user_dev uinp;
		memset(&uinp, 0, sizeof(uinp));
		snprintf(uinp.name, UINPUT_MAX_NAME_SIZE, "%s[%d]", _info.library_name, port);
		uinp.id.bustype = BUS_USB;
		uinp.id.vendor = 0xf00d;
		uinp.id.product = 0xbeef;
		uinp.id.version = 1;
		uinp.ff_effects_max = 0; // otherwise Bad Things happen in UI_DEV_CREATE
		// it's technically covered by the memset, but worth explicitly stating.

		/* Keycodes in the BTN_JOYSTICK range make this a joystick device. */
		ioctl_warn(uinp_fd[port], UI_SET_EVBIT, EV_KEY);
		for( i = 0; i < 12; ++i )
			ioctl_warn(uinp_fd[port], UI_SET_KEYBIT, BTN_JOYSTICK+i);
		/* analogue sticks and POV hat */
		ioctl_warn(uinp_fd[port], UI_SET_EVBIT, EV_ABS);
		ioctl_set_absbit(uinp_fd[port], &uinp, ABS_X, -32767, 32767);
		ioctl_set_absbit(uinp_fd[port], &uinp, ABS_Y, -32767, 32767);
		ioctl_set_absbit(uinp_fd[port], &uinp, ABS_Z, -32767, 32767);
		ioctl_set_absbit(uinp_fd[port], &uinp, ABS_RZ, -32767, 32767);
		ioctl_set_absbit(uinp_fd[port], &uinp, ABS_HAT0X, -1, 1);
		ioctl_set_absbit(uinp_fd[port], &uinp, ABS_HAT0Y, -1, 1);
		/* be able to sync when done sending events */
		ioctl_warn(uinp_fd[port], UI_SET_EVBIT, EV_SYN);

		/* Write device definition and create. */
		if ( write(uinp_fd[port], &uinp, sizeof(uinp)) != sizeof(uinp) ) {
			perror("writing uinput_user_dev");
			close_uinp_fd(port);
		} else if ( ioctl(uinp_fd[port], UI_DEV_CREATE) ) {
			perror("ioctl UI_DEV_CREATE");
			close_uinp_fd(port);
		}
	}
}

// Mimics the Logitech RumblePad 2 USB mappings.
const static int BUTTON_MAP[12] = {
	RETRO_DEVICE_ID_JOYPAD_Y,
	RETRO_DEVICE_ID_JOYPAD_B,
	RETRO_DEVICE_ID_JOYPAD_A,
	RETRO_DEVICE_ID_JOYPAD_X,
	RETRO_DEVICE_ID_JOYPAD_L,
	RETRO_DEVICE_ID_JOYPAD_R,
	RETRO_DEVICE_ID_JOYPAD_L2,
	RETRO_DEVICE_ID_JOYPAD_R2,
	RETRO_DEVICE_ID_JOYPAD_SELECT,
	RETRO_DEVICE_ID_JOYPAD_START,
	RETRO_DEVICE_ID_JOYPAD_L3,
	RETRO_DEVICE_ID_JOYPAD_R3,
};
const static int AXIS_MAP[4][3] = {
	{ ABS_X,  RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X },
	{ ABS_Y,  RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y },
	{ ABS_Z,  RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X },
	{ ABS_RZ, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y },
};
inline void send_event(int fd, int type, int code, int value) {
	struct input_event ev = { .type = type, .code = code, .value = value };
	if ( write(fd, &ev, sizeof(ev)) != sizeof(ev) ) {
		perror("writing input_event");
	}
}
void retro_run(void)
{
	int i, fd, port, value;
	input_poll_cb();
	for ( port = 0; port < NUM_PADS; ++port ) {
		if ( uinp_fd[port] != -1 ) {
			fd = uinp_fd[port];
			// buttons
			for ( i = 0; i < sizeof(BUTTON_MAP)/sizeof(BUTTON_MAP[0]); ++i ) {
				value = input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, BUTTON_MAP[i]);
				send_event(fd, EV_KEY, BTN_JOYSTICK + i, value);
			}
			// axes
			for ( i = 0; i < sizeof(AXIS_MAP)/sizeof(AXIS_MAP[0]); ++i ) {
				value = input_state_cb(port, RETRO_DEVICE_ANALOG, AXIS_MAP[i][1], AXIS_MAP[i][2]);
				send_event(fd, EV_ABS, AXIS_MAP[i][0], value);
			}
			// 'hat' axes
			value = !! input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
			value -= !! input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
			send_event(fd, EV_ABS, ABS_HAT0X, value);
			value = !! input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
			value -= !! input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
			send_event(fd, EV_ABS, ABS_HAT0Y, value);
			send_event(fd, EV_SYN, SYN_REPORT, 0);
		}
	}
}




// begin boilerplate

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
	(void)type;
	(void)info;
	(void)num;
	return false;
}

void retro_get_system_info(struct retro_system_info *info) { *info = _info; }

void retro_get_system_av_info(struct retro_system_av_info *info)
{
	info->timing = (struct retro_system_timing) {
		.fps = 60.0,
		.sample_rate = 22050.0,
	};

	info->geometry = (struct retro_game_geometry) {
		.base_width   = 320,
		.base_height  = 240,
		.max_width    = 320,
		.max_height   = 240,
		.aspect_ratio = 4.0 / 3.0,
	};
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

void retro_set_environment(retro_environment_t cb) { environ_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }

size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data_, size_t size) { (void)data_; (void)size; return true; }
bool retro_unserialize(const void *data_, size_t size) { (void)data_; (void)size; return true; }

void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }

void retro_cheat_reset(void) { }
void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }

