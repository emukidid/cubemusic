#ifndef ENEMY_H_
#define ENEMY_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <stdarg.h>

// Create enemy struct
typedef struct {
	int health;
	float x;
	float y;
	int width;
	int height;
	int direction;
	float speed;
	int movement_style;
} Enemy;

void enemy_init(int num);
void enemy_setdamage(Enemy *e, int damage);
int enemy_gethealth(Enemy *e);
void enemy_update();
void enemy_deinit();

#endif
