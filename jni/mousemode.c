/*
 * mousemode version 0.2.0
 *
 * Copyright (c) 2012 epian
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include "input.h"
#include "uinput.h"
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include "keycode.h"
#include "wiiuse.h"

#define INPUT_DIR "/dev/input"
#define UINPUT_DEVICE1 "/dev/uinput"
#define UINPUT_DEVICE2 "/dev/input/uinput"
#define MOUSE_MODE_DEVICE "mouse_mode_input"
#define DEFAULT_MOVE_SPEED 15000
#define MOVE_DISTANCE 4
#define WHEEL_SPEED 30000

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)	((array[LONG(bit)] >> OFF(bit)) & 1)

static int uifd = 0;
static int rel_x = 0;
static int rel_y = 0;
static int rel_wheel = 0;
static int do_loop = 1;
static float speed_x = 1.0f;
static int up = 0;
static int down = 0;
static int left = 0;
static int right = 0;
static int wheel_up = 0;
static int wheel_down = 0;
static int hs = 0, he = 0;
wiimote **wiimotes;
pthread_mutex_t mutex;

int count = 0;
typedef struct elapsed_time {
	double time;
	int type;
}elapsed_time;
elapsed_time time_buf[1024];
double start, end;

double gettimeofday_sec() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec * 1e-6;
}


//int size;
//extern int size;

void send_move_event(int rel_x, int rel_y)
{
	struct input_event event;
	if (uifd <= 0) {
		return;
	}
	
	pthread_mutex_lock(&mutex);

	gettimeofday(&event.time, NULL);
	event.type = EV_REL;
	event.code = REL_X;
	event.value = rel_x;
	write(uifd, &event, sizeof(event));

	event.type = EV_REL;
	event.code = REL_Y;
	event.value = rel_y;
	write(uifd, &event, sizeof(event));

	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	write(uifd, &event, sizeof(event));
	
	pthread_mutex_unlock(&mutex);
}

void send_key_event(int button, int value)
{
	struct input_event event;
	if (uifd <= 0) {
		return;
	}
	pthread_mutex_lock(&mutex);

	gettimeofday(&event.time, NULL);
	event.type = EV_KEY;
	event.code = button;
	event.value = value;
	write(uifd, &event, sizeof(event));

	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	write(uifd, &event, sizeof(event));
	
	pthread_mutex_unlock(&mutex);
}

void create_virtual_device()
{
	struct uinput_user_dev dev;
	memset(&dev, 0, sizeof(dev));
	strcpy(dev.name, MOUSE_MODE_DEVICE);
	write(uifd, &dev, sizeof(dev));

	/*For Mouse*/
	ioctl(uifd, UI_SET_EVBIT, EV_REL);
	ioctl(uifd, UI_SET_RELBIT, REL_X);
	ioctl(uifd, UI_SET_RELBIT, REL_Y);
	ioctl(uifd, UI_SET_RELBIT, REL_WHEEL);
	/*For Keyboard*/
	ioctl(uifd, UI_SET_EVBIT, EV_KEY);
	ioctl(uifd, UI_SET_KEYBIT, KEY_S);
	ioctl(uifd, UI_SET_KEYBIT, BTN_LEFT);
	ioctl(uifd, UI_SET_KEYBIT, BTN_RIGHT);

	ioctl(uifd, UI_SET_KEYBIT, KEY_ESC);
	ioctl(uifd, UI_SET_KEYBIT, KEY_ENTER);
	ioctl(uifd, UI_SET_KEYBIT, KEY_BACKSPACE);
	ioctl(uifd, UI_SET_KEYBIT, KEY_MENU);
	ioctl(uifd, UI_SET_KEYBIT, KEY_HOME);
	ioctl(uifd, UI_SET_KEYBIT, KEY_UP);
	ioctl(uifd, UI_SET_KEYBIT, KEY_DOWN);
	ioctl(uifd, UI_SET_KEYBIT, KEY_RIGHT);
	ioctl(uifd, UI_SET_KEYBIT, KEY_LEFT);
	ioctl(uifd, UI_SET_KEYBIT, KEY_SEARCH);
	ioctl(uifd, UI_SET_KEYBIT, KEY_R);
	ioctl(uifd, UI_SET_KEYBIT, KEY_L);
	ioctl(uifd, UI_DEV_CREATE, 0);
}

void destroy_virtual_device()
{
	ioctl(uifd, UI_DEV_DESTROY, 0);
}


void *move_thread(void *arg)
{
	while (do_loop) {
		if (rel_x != 0 || rel_y != 0) {
			send_move_event(rel_x, rel_y);
		}
		usleep(DEFAULT_MOVE_SPEED * speed_x);
	}
	return (void *) NULL;
}



int open_uinput() {
	struct stat st;
	if (stat(UINPUT_DEVICE1, &st) == 0 && S_ISCHR(st.st_mode)) {
		return open(UINPUT_DEVICE1, O_WRONLY);
	}
	if (stat(UINPUT_DEVICE2, &st) == 0 && S_ISCHR(st.st_mode)) {
		return open(UINPUT_DEVICE2, O_WRONLY);
	}
	return -1;
}

int get_lock() {
	char buf[1024];
	ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (len > -1) {
		buf[len] = '\0';
	} else {
		perror("Error reading link the self executable file\n");
		return -1;
	}

	int fd = open(buf, O_RDONLY);
	if (fd <= 0) {
		perror("Error opening the self executable file\n");
		return -1;
	}

	int lock = flock(fd, LOCK_EX | LOCK_NB);
	if (lock != 0) {
		perror("Error get lock\n");
		close(fd);
		return -1;
	}

	return fd;
}

int free_lock(int fd) {
	flock(fd, LOCK_UN);
	close(fd);
}

int key_code = 0;
void handle_event(struct wiimote_t *wm){
	//int key_code = 0;
	key_code = 0;
	struct nunchuk_t* nc = (nunchuk_t*)&wm->exp.nunchuk;
	//start = gettimeofday_sec();
	if(IS_RELEASED(wm, WIIMOTE_BUTTON_UP)){
		key_code = KEY_UP;
	}
	else if(IS_RELEASED(wm, WIIMOTE_BUTTON_DOWN)){
		key_code = KEY_DOWN;
	}
	else if(IS_RELEASED(wm, WIIMOTE_BUTTON_LEFT)){
		key_code = KEY_LEFT;
	}
	else if(IS_RELEASED(wm, WIIMOTE_BUTTON_RIGHT)){
		key_code = KEY_RIGHT;
	}
	else if(IS_RELEASED(wm, WIIMOTE_BUTTON_A)){
		key_code = KEY_ENTER;
	}
	else if(IS_RELEASED(wm, WIIMOTE_BUTTON_B)){
		key_code = KEY_ESC;
	}
	else if(IS_RELEASED(wm, WIIMOTE_BUTTON_MINUS)){
		key_code = KEY_BACKSPACE;
	}
	else if(IS_RELEASED(wm, WIIMOTE_BUTTON_PLUS)){
		//key_code = KEY_;
	}
	else if(IS_RELEASED(wm, WIIMOTE_BUTTON_ONE)){
		key_code = KEY_SEARCH;
	}
	else if(IS_RELEASED(wm, WIIMOTE_BUTTON_TWO)){
		key_code = KEY_ESC;
	}
	else if(IS_RELEASED(wm, WIIMOTE_BUTTON_HOME)){
		key_code = KEY_HOME;
	}
	else if(IS_RELEASED(nc, NUNCHUK_BUTTON_C)){
		key_code = BTN_LEFT;
	}
	else if(IS_RELEASED(nc, NUNCHUK_BUTTON_Z)){
		key_code = BTN_RIGHT;
	}
	if(key_code != 0){
		send_key_event(key_code, 1);
		send_key_event(key_code, 0);
	}
}

void handle_disconnect(wiimote* wm){
	printf("--DISCONNECT WIIREMOTE--\n\n");
	//printf("SENDED DATA = %d \n", size);
	int i;
	for(i = 0; i < count; i++){
		if (time_buf[i].type != 0)
			printf("%d, %f\n", time_buf[i].type, time_buf[i].time);	
	}
	exit(1);
}




void wiiuse_event_loop(){

	while(1){
		start = gettimeofday_sec();
		if(wiiuse_poll(wiimotes,1)){
			switch(wiimotes[0]->event){
				case WIIUSE_EVENT:
					handle_event(wiimotes[0]);
					end = gettimeofday_sec();
					time_buf[count].time = end - start;
					time_buf[count].type = key_code;
					count++;
					break;
				case WIIUSE_DISCONNECT:
					//return;
					handle_disconnect(wiimotes[0]);
					break;
			}
		}
	}
}


void run_wiiuse(void){
	wiiuse_event_loop();
	return;
}

int main(int argc, char *argv[])
{
	int lock_fd = get_lock();
	if (lock_fd < 0) {
		return -1;
	}

	uifd = open_uinput();
	if (uifd <= 0) {
		perror("Error opening the uinput device.");
		free_lock(lock_fd);
		return -1;
	}
	create_virtual_device();

	int found, connect;
	wiimotes = wiiuse_init(1);
	found = wiiuse_find(wiimotes, 1, 5);
	if(!found){
		printf("no Wiimote found\n");
		return 0;
	}

	connect = wiiuse_connect(wiimotes, 1);
	if(connect)
		printf("connected to %i wiimtoes(of %i found).\n", connect, found);
	else{
		printf("Faild to connect to any wiimote\n");
		return 0;
	}
	wiiuse_set_leds(wiimotes[0], WIIMOTE_LED_1);
	wiiuse_motion_sensing(wiimotes[0], 0);
	wiiuse_set_ir(wiimotes[0], 0);
	wiiuse_rumble(wiimotes[0], 1);
	usleep(200000);
	wiiuse_rumble(wiimotes[0],0);
	run_wiiuse();

	destroy_virtual_device();

	close(uifd);
	free_lock(lock_fd);
}
