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

static int uinp_fd[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
void retro_set_controller_port_device(unsigned port, unsigned device)
{
	int i;
	struct uinput_user_dev uinp;

	printf("set_controller_port_device(%d, %d)\n", port, device);
	if ( device == RETRO_DEVICE_NONE ) {
		// unplug, if it's already plugged.
		if ( uinp_fd[port] != -1 ) {
			if ( ioctl(uinp_fd[port], UI_DEV_DESTROY) )
				perror("ioctl UI_DEV_DESTROY");
			close(uinp_fd[port]);
			uinp_fd[port] = -1;
		}
		return;
	}

	if ( device != RETRO_DEVICE_JOYPAD ) {
		puts("device types other than JOYPAD not yet implemented.");
		return;
	}

	if ( uinp_fd[port] != -1 ) {
		printf("port %d already has controller (fd %d).\n", port, uinp_fd[port]);
		return;
	}

	uinp_fd[port] = open( "/dev/uinput", O_WRONLY | O_NONBLOCK );
	if ( uinp_fd[port] == -1 ) {
		perror("opening /dev/uinput");
		return;
	}

	/* Keycodes in the BTN_JOYSTICK range make this a joystick device. */
	if ( ioctl(uinp_fd[port], UI_SET_EVBIT, EV_KEY) )
		perror("ioctl UI_SET_EVBIT");
	for( i = 0; i < 16; i ++ )
		if ( ioctl(uinp_fd[port], UI_SET_KEYBIT, BTN_JOYSTICK + i) )
			perror("ioctl UI_SET_KEYBIT");

	/* Define our virtual input device. */
	snprintf(uinp.name, UINPUT_MAX_NAME_SIZE, "%s[%d]", _info.library_name, port);
	uinp.id.bustype = BUS_USB;
	uinp.id.vendor = 0xf00d;
	uinp.id.product = 0xbeef;
	uinp.id.version = 1;
	uinp.ff_effects_max = 0; // otherwise Bad Things happen in UI_DEV_CREATE.

	if ( write( uinp_fd[port], &uinp, sizeof(uinp) ) != sizeof(uinp) ) {
		perror("writing uinput_user_dev");
		close(uinp_fd[port]);
		uinp_fd[port] = -1;
		return;
	}
	if ( ioctl(uinp_fd[port], UI_DEV_CREATE) ) {
		perror("ioctl UI_DEV_CREATE");
		close(uinp_fd[port]);
		uinp_fd[port] = -1;
		return;
	}
}

void retro_run(void)
{
	int i, port;
	input_poll_cb();
	for ( port = 0; port < 8; ++port ) {
		if ( uinp_fd[port] == -1 ) { continue; }
		for ( i = 0; i < 16; ++i ) {
			struct input_event ev = {
				.type = EV_KEY,
				.code = BTN_JOYSTICK + i,
				.value = input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, i),
			};
			if ( write(uinp_fd[port], &ev, sizeof(ev)) != sizeof(ev) ) {
				perror( "write" );
			}
		}
		//input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
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

